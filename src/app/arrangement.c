#include "daw/arrangement.h"
#include "app_state.h"
#include "undo/undo_manager.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
// Reports explicit arrangement outcomes without borrowing mutable project data.
static DawMidiResult result(DawMidiStatus status) {
    const char* messages[]={"Arrangement committed as one undo step.","Arrangement unchanged.","Invalid arrangement, overlap, note bounds or capacity.",
        "Arrangement needs the DAW owner thread.","Arrangement target no longer belongs to this project.","Arrangement content or timing changed; refresh before editing.",
        "Stop transport/recording/Test and finish the current edit.","Arrangement history could not be reserved.","Arrangement publication failed; original project retained."};
    return (DawMidiResult){status,messages[status],0};
}
// Bounds complete copied notes and automation before retaining any runtime/media content.
static bool bounded(const AppState* state,const DawMidiLayout* layout) {
    if(layout->clip_count>DAW_ARRANGEMENT_CLIP_CAP)return false;int notes=0,points=0;
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    for(int t=0;t<layout->track_count;++t)for(int c=0;c<tracks[t].clip_count;++c){const EngineClip* clip=&tracks[t].clips[c];
        if(clip->midi_notes.note_count<0 || clip->midi_notes.note_count>DAW_ARRANGEMENT_NOTE_CAP-notes)return false;notes+=clip->midi_notes.note_count;
        for(int l=0;l<clip->automation_lane_count;++l){int n=clip->automation_lanes[l].point_count;if(n<0 || n>DAW_ARRANGEMENT_POINT_CAP-points)return false;points+=n;}
    }
    return true;
}
// Captures every track's authored clip content while leaving transport, mixer and effects untouched.
DawMidiResult daw_action_get_arrangement(const AppState* state,DawArrangementSnapshot* out) {
    if(!out || out->content)return result(DAW_MIDI_INVALID);DawMidiResult got=daw_action_get_midi_layout(state,&out->layout);
    if(got.status!=DAW_MIDI_UNCHANGED)return got;if(!bounded(state,&out->layout))return result(DAW_MIDI_INVALID);
    if(out->layout.track_count){int tracks[DAW_MIDI_LAYOUT_TRACK_CAP];for(int i=0;i<out->layout.track_count;++i)tracks[i]=i;
        out->content=engine_clip_content_capture(state->engine,tracks,out->layout.track_count);if(!out->content)return result(DAW_MIDI_ENGINE_REJECTED);}
    return result(DAW_MIDI_UNCHANGED);
}
// Requires exact layout/tempo plus all captured authored content rather than a lossy wire fingerprint.
bool daw_arrangement_snapshot_current(const AppState* state,const DawArrangementSnapshot* expected) {
    if(!expected)return false;DawMidiLayout* now=malloc(sizeof(*now));if(!now)return false;
    bool matches=daw_action_get_midi_layout(state,now).status==DAW_MIDI_UNCHANGED && !memcmp(now,&expected->layout,sizeof(*now)) &&
        (expected->layout.track_count?engine_clip_content_matches(state->engine,expected->content):expected->content==NULL);
    free(now);return matches;
}
// Retires independently retained precondition content without touching live authored data.
void daw_arrangement_snapshot_free(DawArrangementSnapshot* snapshot) {
    if(!snapshot)return;engine_clip_content_release(snapshot->content);memset(snapshot,0,sizeof(*snapshot));
}
// Copies current region fields into a complete private engine transform.
static EngineClipTransform transform(const EngineClip* clip) {
    return (EngineClipTransform){clip->timeline_start_frames,clip->offset_frames,clip->duration_frames,clip->fade_in_frames,clip->fade_out_frames,
        clip->fade_in_curve,clip->fade_out_curve,clip->gain,clip->instrument_preset,clip->instrument_params,clip->instrument_inherits_track,
        clip->midi_notes.notes,clip->midi_notes.note_count};
}
// Checks full batch destination placement before history reservation and engine preparation.
static bool placement(const AppState* state,const EngineArrangementEdit* edits,int count) {
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    for(int i=0;i<count;++i){if(edits[i].op==ENGINE_ARRANGE_DELETE)continue;uint64_t start=edits[i].transform.start_frame,duration=edits[i].transform.duration_frames;
        if(!duration || start>INT64_MAX || duration>(uint64_t)INT64_MAX-start)return false;
        const EngineTrack* track=&tracks[edits[i].destination_track];
        for(int c=0;c<track->clip_count;++c){const EngineClip* clip=&track->clips[c];bool removed=false;
            for(int j=0;j<count;++j)if(edits[j].clip_id==clip->creation_index && edits[j].op!=ENGINE_ARRANGE_DUPLICATE)removed=true;
            if(removed)continue;
            if(clip->timeline_start_frames>INT64_MAX || clip->duration_frames>(uint64_t)INT64_MAX-clip->timeline_start_frames ||
                (start<clip->timeline_start_frames+clip->duration_frames && clip->timeline_start_frames<start+duration))return false;
        }
        for(int j=0;j<i;++j)if(edits[j].op!=ENGINE_ARRANGE_DELETE && edits[j].destination_track==edits[i].destination_track &&
            start<edits[j].transform.start_frame+edits[j].transform.duration_frames && edits[j].transform.start_frame<start+duration)return false;
    }
    return true;
}
// Prepares every operation against exact content and publishes all accepted edits with one reserved history entry.
DawMidiResult daw_action_arrange(AppState* state,const DawArrangementSnapshot* expected,const DawArrangementEdit* edits,int count,DawActionTarget* outputs) {
    if(!state || !state->engine || !expected || !edits || !outputs || count<1 || count>DAW_ARRANGEMENT_BATCH_CAP)return result(DAW_MIDI_INVALID);
    if(!engine_is_control_thread(state->engine))return result(DAW_MIDI_WRONG_THREAD);
    if(daw_midi_actions_busy(state))return result(DAW_MIDI_BUSY);
    if(!daw_arrangement_snapshot_current(state,expected))return result(DAW_MIDI_STATE_CONFLICT);
    EngineArrangementEdit prepared[DAW_ARRANGEMENT_BATCH_CAP]={0};uint64_t ids[DAW_ARRANGEMENT_BATCH_CAP]={0};int total=expected->layout.clip_count;bool changed=false;
    const EngineTrack* tracks=engine_get_tracks(state->engine);int note_total=0,point_total=0;
    for(int t=0;t<expected->layout.track_count;++t)for(int c=0;c<tracks[t].clip_count;++c){note_total+=tracks[t].clips[c].midi_notes.note_count;for(int l=0;l<tracks[t].clips[c].automation_lane_count;++l)point_total+=tracks[t].clips[c].automation_lanes[l].point_count;}
    for(int i=0;i<count;++i){int t,c;DawActionStatus resolved=daw_action_target_resolve(state,edits[i].target,&t,&c);
        if(resolved!=DAW_ACTION_UNCHANGED)return result(DAW_MIDI_STALE_TARGET);
        if(edits[i].target.kind!=DAW_ACTION_TARGET_MIDI_REGION || edits[i].operation<ENGINE_ARRANGE_DUPLICATE || edits[i].operation>ENGINE_ARRANGE_DELETE)return result(DAW_MIDI_INVALID);
        for(int j=0;j<i;++j)if(edits[j].target.clip_id==edits[i].target.clip_id)return result(DAW_MIDI_INVALID);
        const EngineClip* clip=&tracks[t].clips[c];int destination=t;
        if(edits[i].destination_track_id){destination=-1;for(int k=0;k<engine_get_track_count(state->engine);++k)if(tracks[k].runtime_id==edits[i].destination_track_id)destination=k;if(destination<0)return result(DAW_MIDI_STALE_TARGET);}
        prepared[i]=(EngineArrangementEdit){edits[i].operation,clip->creation_index,destination,transform(clip)};
        int direction=edits[i].operation==ENGINE_ARRANGE_DUPLICATE?1:edits[i].operation==ENGINE_ARRANGE_DELETE?-1:0;
        note_total+=direction*clip->midi_notes.note_count;for(int l=0;l<clip->automation_lane_count;++l)point_total+=direction*clip->automation_lanes[l].point_count;
        if(edits[i].operation==ENGINE_ARRANGE_DELETE){if(edits[i].destination_track_id || edits[i].start_frame || edits[i].duration_frames)return result(DAW_MIDI_INVALID);--total;changed=true;continue;}
        if(edits[i].operation==ENGINE_ARRANGE_TRIM){
            if(edits[i].destination_track_id || edits[i].start_frame || clip->offset_frames || !edits[i].duration_frames || edits[i].duration_frames>clip->duration_frames)return result(DAW_MIDI_INVALID);
            for(int n=0;n<clip->midi_notes.note_count;++n)if(clip->midi_notes.notes[n].start_frame>edits[i].duration_frames || clip->midi_notes.notes[n].duration_frames>edits[i].duration_frames-clip->midi_notes.notes[n].start_frame)return result(DAW_MIDI_INVALID);
            prepared[i].transform.duration_frames=edits[i].duration_frames;changed|=edits[i].duration_frames!=clip->duration_frames;continue;
        }
        if(edits[i].duration_frames)return result(DAW_MIDI_INVALID);prepared[i].transform.start_frame=edits[i].start_frame;
        if(destination!=t && clip->instrument_inherits_track){DawActionResult sound=daw_action_get_instrument(state,edits[i].target);if(!sound.has_snapshot)return result(DAW_MIDI_ENGINE_REJECTED);
            prepared[i].transform.instrument_preset=sound.snapshot.effective_preset;prepared[i].transform.instrument_params=sound.snapshot.effective_params;prepared[i].transform.instrument_inherits_track=false;}
        if(edits[i].operation==ENGINE_ARRANGE_DUPLICATE){++total;changed=true;}else changed|=destination!=t || edits[i].start_frame!=clip->timeline_start_frames;
    }
    if(total>DAW_ARRANGEMENT_CLIP_CAP || note_total>DAW_ARRANGEMENT_NOTE_CAP || point_total>DAW_ARRANGEMENT_POINT_CAP || !placement(state,prepared,count))return result(DAW_MIDI_INVALID);
    if(!changed){for(int i=0;i<count;++i)outputs[i]=edits[i].target;return result(DAW_MIDI_UNCHANGED);}
    DawArrangementHistory* history=calloc(1,sizeof(*history));if(!history)return result(DAW_MIDI_ENGINE_REJECTED);history->before=expected->layout;history->count=count;
    for(int i=0;i<count;++i)history->input_ids[i]=edits[i].target.clip_id;
    UndoCommand command={.type=UNDO_CMD_ARRANGEMENT};command.data.arrangement=history;
    if(!undo_manager_begin_drag(&state->undo,&command)){free(history);return result(DAW_MIDI_HISTORY_UNAVAILABLE);}free(history);
    UndoCommand* pending=&state->undo.active_drag;
    if(!engine_arrangement_transaction(state->engine,prepared,count,ids,&pending->clip_content_before,&pending->clip_content_after)){undo_manager_cancel_drag(&state->undo);return result(DAW_MIDI_ENGINE_REJECTED);}
    history=pending->data.arrangement;history->after=history->before;
    // Deterministic layout readback is prepared from accepted identities; no allocation follows publication.
    daw_action_get_midi_layout(state,&history->after);
    for(int i=0;i<count;++i){history->output_ids[i]=ids[i];outputs[i]=(DawActionTarget){0};if(ids[i]){
        for(int t=0;t<engine_get_track_count(state->engine);++t)for(int c=0;c<engine_get_tracks(state->engine)[t].clip_count;++c)
            if(engine_get_tracks(state->engine)[t].clips[c].creation_index==ids[i])daw_instrument_target_capture(state,t,c,&outputs[i]);}}
    return result(undo_manager_commit_drag(&state->undo,pending)?DAW_MIDI_APPLIED:DAW_MIDI_HISTORY_UNAVAILABLE);
}
// Restores complete affected content only after layout and opposing authored content still match exactly.
bool daw_arrangement_history_restore(AppState* state,const UndoCommand* command,bool after) {
    if(!state || !state->engine || !command || !command->data.arrangement || !engine_is_control_thread(state->engine) || daw_midi_actions_busy(state))return false;
    const DawArrangementHistory* history=command->data.arrangement;DawMidiLayout* current=malloc(sizeof(*current));if(!current)return false;
    const DawMidiLayout* expected=after?&history->before:&history->after;
    bool matches=daw_action_get_midi_layout(state,current).status==DAW_MIDI_UNCHANGED && !memcmp(current,expected,sizeof(*current));free(current);
    return matches && engine_clip_content_matches(state->engine,after?command->clip_content_before:command->clip_content_after) &&
        engine_clip_content_restore(state->engine,after?command->clip_content_after:command->clip_content_before);
}
