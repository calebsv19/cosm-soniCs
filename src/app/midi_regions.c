#include "daw/midi_regions.h"
#include "app_state.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Produces a bounded region outcome without allocating or borrowing project storage.
static DawMidiResult result(DawMidiStatus status) {
    static const char* messages[] = {"MIDI region change committed as one undo step.", "MIDI region unchanged.",
        "Invalid region bounds, overlap, or capacity.", "Action requires the DAW control thread.", "Region or track no longer belongs to this project.",
        "Project placement, notes, bounds or tempo changed; refresh before editing.", "Stop playback, recording and Test; finish the current edit first.",
        "Region edit not applied: undo history could not be reserved.", "Engine rejected region publication; project retained."};
    return (DawMidiResult){status,messages[status],0};
}
// Copies a bounded exact layout before structural actions inspect mutable project data.
DawMidiResult daw_action_get_midi_layout(const AppState* state, DawMidiLayout* out) {
    if (!out || !state || !state->engine) return result(DAW_MIDI_INVALID);
    if (!engine_is_control_thread(state->engine)) return result(DAW_MIDI_WRONG_THREAD);
    int count = engine_get_track_count(state->engine);
    if (count < 0 || count > DAW_MIDI_LAYOUT_TRACK_CAP || state->tempo_map.event_count <= 0 ||
        state->tempo_map.event_count > DAW_MIDI_TEMPO_EVENT_CAP || !state->tempo_map.events ||
        !isfinite(state->tempo_map.sample_rate) || state->tempo_map.sample_rate <= 0) return result(DAW_MIDI_INVALID);
    memset(out,0,sizeof(*out)); out->generation = engine_instance_id(state->engine); out->track_count=count;
    out->sample_rate=state->tempo_map.sample_rate; out->tempo_count=state->tempo_map.event_count;
    for (int i=0;i<out->tempo_count;++i) {
        TempoEvent event=state->tempo_map.events[i];
        if (!isfinite(event.beat) || event.beat<0 || (i==0 && event.beat!=0) || (i && event.beat<=out->tempo[i-1].beat) ||
            !isfinite(event.bpm) || event.bpm<20 || event.bpm>300) return result(DAW_MIDI_INVALID);
        out->tempo[i].beat=event.beat; out->tempo[i].bpm=event.bpm;
    }
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    for (int t=0;t<count;++t) {
        out->track_ids[t]=tracks[t].runtime_id;
        if (tracks[t].clip_count<0 || tracks[t].clip_count>DAW_MIDI_LAYOUT_CLIP_CAP-out->clip_count) return result(DAW_MIDI_INVALID);
        for (int c=0;c<tracks[t].clip_count;++c) {
            const EngineClip* clip=&tracks[t].clips[c]; DawMidiPlacement* row=&out->clips[out->clip_count++];
            row->track_id=tracks[t].runtime_id;row->clip_id=clip->creation_index;row->start=clip->timeline_start_frames;
            row->duration=clip->duration_frames;row->offset=clip->offset_frames;row->kind=clip->kind;
        }
    }
    return result(DAW_MIDI_UNCHANGED);
}
// Refuses frame overflow and half-open overlaps on the affected lane while allowing touching boundaries.
static bool fits(const AppState* state, int t, uint64_t ignore, uint64_t start, uint64_t duration) {
    if (!duration || start>(uint64_t)INT64_MAX || duration>(uint64_t)INT64_MAX-start) return false;
    if (t==engine_get_track_count(state->engine)) return true;
    const EngineTrack* track=&engine_get_tracks(state->engine)[t];
    for (int c=0;c<track->clip_count;++c) {
        const EngineClip* clip=&track->clips[c];if(clip->creation_index==ignore)continue;
        if (clip->timeline_start_frames>UINT64_MAX-clip->duration_frames) return false;
        if (start<clip->timeline_start_frames+clip->duration_frames && clip->timeline_start_frames<start+duration) return false;
    }
    return true;
}
// Captures only instrument metadata changed when the first MIDI region enables a track.
static DawInstrumentState track_sound(const EngineTrack* track) {
    return (DawInstrumentState){.enabled=track->midi_instrument_enabled,.preset=track->midi_instrument_preset,.params=track->midi_instrument_params};
}
// Compares complete stored track sound values without struct padding.
static bool sound_equal(DawInstrumentState a, DawInstrumentState b) {
    if (a.enabled!=b.enabled || a.preset!=b.preset) return false;
    for(int i=0;i<ENGINE_INSTRUMENT_PARAM_COUNT;++i)
        if(engine_instrument_params_get(a.params,(EngineInstrumentParamId)i)!=engine_instrument_params_get(b.params,(EngineInstrumentParamId)i))return false;
    return true;
}
// Copies one empty region publication into pre-reserved history before returning its stable handle.
DawMidiResult daw_action_create_midi_region(AppState* state,const DawMidiLayout* expected,const DawActionTarget* target,
                                          uint64_t start,uint64_t duration,DawActionTarget* out) {
    if(!expected || !out)return result(DAW_MIDI_INVALID);
    DawMidiLayout* current=malloc(sizeof(*current));if(!current)return result(DAW_MIDI_ENGINE_REJECTED);
    DawMidiResult got=daw_action_get_midi_layout(state,current);
    if(got.status!=DAW_MIDI_UNCHANGED){free(current);return got;}
    bool match=!memcmp(expected,current,sizeof(*current));free(current);
    if(!match)return result(DAW_MIDI_STATE_CONFLICT);
    if(daw_midi_actions_busy(state))return result(DAW_MIDI_BUSY);
    int t=engine_get_track_count(state->engine),c=-1;
    if(target){
        DawActionStatus status=daw_action_target_resolve(state,*target,&t,&c);
        if(status!=DAW_ACTION_UNCHANGED)return result(status==DAW_ACTION_STALE_TARGET?DAW_MIDI_STALE_TARGET:DAW_MIDI_INVALID);
        if(target->kind!=DAW_ACTION_TARGET_TRACK)return result(DAW_MIDI_INVALID);
    } else if(t>=DAW_MIDI_LAYOUT_TRACK_CAP)return result(DAW_MIDI_INVALID);
    if(expected->clip_count>=DAW_MIDI_LAYOUT_CLIP_CAP || !fits(state,t,0,start,duration))return result(DAW_MIDI_INVALID);
    UndoCommand command={.type=UNDO_CMD_MIDI_REGION};
    UndoMidiRegion* edit=&command.data.midi_region;
    edit->created=true;edit->new_track=!target;edit->created_track_index=t;edit->start=start;edit->after_duration=duration;
    if(target)edit->track_before=track_sound(&engine_get_tracks(state->engine)[t]);
    if(!undo_manager_begin_drag(&state->undo,&command))return result(DAW_MIDI_HISTORY_UNAVAILABLE);
    if(!engine_add_midi_clip_to_track(state->engine,t,start,duration,&c)){
        undo_manager_cancel_drag(&state->undo);return result(DAW_MIDI_ENGINE_REJECTED);
    }
    edit=&state->undo.active_drag.data.midi_region;
    daw_instrument_target_capture(state,t,c,&edit->target);edit->track_after=track_sound(&engine_get_tracks(state->engine)[t]);
    undo_created_track_capture(&engine_get_tracks(state->engine)[t],&edit->created_track);
    edit->created_eq=engine_get_tracks(state->engine)[t].track_eq.curve;
    *out=edit->target;
    if(!undo_manager_commit_drag(&state->undo,&state->undo.active_drag))return result(DAW_MIDI_HISTORY_UNAVAILABLE);
    return result(DAW_MIDI_APPLIED);
}
// Extends the right edge only after exact note/tempo validation and one independent history reservation.
DawMidiResult daw_action_extend_midi_region(AppState* state,const DawMidiSnapshot* expected,uint64_t duration) {
    if(!expected)return result(DAW_MIDI_INVALID);
    DawMidiSnapshot current={0};DawMidiResult got=daw_action_get_midi(state,expected->target,&current);
    if(got.status!=DAW_MIDI_UNCHANGED)return got;
    DawMidiStatus status=DAW_MIDI_UNCHANGED;int t,c;
    if(!daw_midi_snapshot_equal(expected,&current)){status=DAW_MIDI_STATE_CONFLICT;goto done;}
    if(daw_midi_actions_busy(state)){status=DAW_MIDI_BUSY;goto done;}
    daw_action_target_resolve(state,current.target,&t,&c);
    if(current.offset_frames || duration<current.duration_frames || !fits(state,t,current.target.clip_id,current.start_frame,duration)){status=DAW_MIDI_INVALID;goto done;}
    if(duration==current.duration_frames)goto done;
    UndoCommand command={.type=UNDO_CMD_MIDI_REGION};
    command.data.midi_region=(UndoMidiRegion){.target=current.target,.start=current.start_frame,.before_duration=current.duration_frames,
        .after_duration=duration,.notes=current.notes,.note_count=current.note_count};
    if(!undo_manager_begin_drag(&state->undo,&command)){status=DAW_MIDI_HISTORY_UNAVAILABLE;goto done;}
    if(!engine_clip_set_region(state->engine,t,c,0,duration)){
        undo_manager_cancel_drag(&state->undo);status=DAW_MIDI_ENGINE_REJECTED;goto done;
    }
    status=undo_manager_commit_drag(&state->undo,&state->undo.active_drag)?DAW_MIDI_APPLIED:DAW_MIDI_HISTORY_UNAVAILABLE;
done:
    daw_midi_snapshot_free(&current);return result(status);
}
// Verifies captured notes before history changes region bounds or removes a creation.
static bool notes_equal(const EngineClip* clip,const UndoMidiRegion* edit) {
    if(clip->midi_notes.note_count!=edit->note_count)return false;
    for(int i=0;i<edit->note_count;++i){const EngineMidiNote* a=&clip->midi_notes.notes[i];const EngineMidiNote* b=&edit->notes[i];
        if(a->start_frame!=b->start_frame || a->duration_frames!=b->duration_frames || a->note!=b->note || a->velocity!=b->velocity)return false;}
    return true;
}
// Removes only pristine creations and replays exact stable identities without overwriting unrelated work.
bool daw_midi_region_history_restore(AppState* state,UndoMidiRegion* edit,bool after) {
    if(!state || !state->engine || !edit || !engine_is_control_thread(state->engine) ||
        edit->target.project_generation!=engine_instance_id(state->engine) || daw_midi_actions_busy(state))return false;
    int t=-1,c=-1;const EngineTrack* tracks=engine_get_tracks(state->engine);
    for(int i=0;i<engine_get_track_count(state->engine);++i)if(tracks[i].runtime_id==edit->target.track_id)t=i;
    if(edit->created && after){
        if(edit->new_track){if(t!=-1 || engine_get_track_count(state->engine)!=edit->created_track_index)return false;t=edit->created_track_index;}
        else if(t<0 || !sound_equal(track_sound(&tracks[t]),edit->track_before))return false;
        if(!fits(state,t,0,edit->start,edit->after_duration))return false;
        return engine_add_midi_clip_with_identity(state->engine,t,edit->start,edit->after_duration,edit->target.clip_id,
                                                  edit->new_track?edit->target.track_id:0,NULL);
    }
    if(daw_action_target_resolve(state,edit->target,&t,&c)!=DAW_ACTION_UNCHANGED)return false;
    tracks=engine_get_tracks(state->engine);const EngineClip* clip=&tracks[t].clips[c];
    if(clip->timeline_start_frames!=edit->start || clip->offset_frames || !notes_equal(clip,edit))return false;
    if(!edit->created){
        uint64_t expected=after?edit->before_duration:edit->after_duration,next=after?edit->after_duration:edit->before_duration;
        return clip->duration_frames==expected && fits(state,t,edit->target.clip_id,edit->start,next) && engine_clip_set_region(state->engine,t,c,0,next);
    }
    if(clip->duration_frames!=edit->after_duration || !sound_equal(track_sound(&tracks[t]),edit->track_after) ||
        !clip->instrument_inherits_track || clip->instrument_preset!=edit->track_after.preset || clip->gain!=1 ||
        clip->fade_in_frames || clip->fade_out_frames || strcmp(clip->name,"MIDI Region") || clip->automation_lane_count!=1 ||
        clip->automation_lanes[0].target!=ENGINE_AUTOMATION_TARGET_VOLUME || clip->automation_lanes[0].point_count)return false;
    for(int i=0;i<ENGINE_INSTRUMENT_PARAM_COUNT;++i)
        if(engine_instrument_params_get(clip->instrument_params,(EngineInstrumentParamId)i)!=engine_instrument_params_get(edit->track_after.params,(EngineInstrumentParamId)i))return false;
    if(edit->new_track){
        UndoCreatedTrack current;undo_created_track_capture(&tracks[t],&current);FxMasterSnapshot fx;
        if(t!=engine_get_track_count(state->engine)-1 || tracks[t].clip_count!=1 ||
            memcmp(&current,&edit->created_track,sizeof(current)) || memcmp(&tracks[t].track_eq.curve,&edit->created_eq,sizeof(edit->created_eq)) ||
            tracks[t].midi_instrument_automation_lane_count || !engine_fx_track_snapshot(state->engine,t,&fx) || fx.count)return false;
        return engine_remove_track(state->engine,t);
    }
    return engine_remove_midi_clip_restore_track(state->engine,t,c,edit->track_before.enabled,edit->track_before.preset,edit->track_before.params);
}
