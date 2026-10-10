#include "daw/midi_actions.h"
#include "app_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Creates an allocation-free result with a static reason suitable for UI and later adapters.
static DawMidiResult result(DawMidiStatus status, int count) {
    const char* messages[] = {"MIDI pattern committed as one undo step.", "MIDI notes unchanged.",
        "Invalid notes, duplicate event, or pattern exceeds region bounds.", "Action requires the DAW control thread.",
        "MIDI target no longer belongs to this project.", "Region notes, bounds or tempo changed; refresh before editing.",
        "Stop playback, recording and keyboard Test; finish the current edit first.",
        "MIDI edit not applied: undo history could not be reserved.", "Engine rejected MIDI publication; original notes retained."};
    return (DawMidiResult){status, messages[status], count};
}
// Resolves only a MIDI region through the existing common generation/identity contract.
static DawMidiStatus resolve(const AppState* state, DawActionTarget target, int* track, int* clip) {
    DawActionStatus status = daw_action_target_resolve(state, target, track, clip);
    if (status == DAW_ACTION_WRONG_THREAD) return DAW_MIDI_WRONG_THREAD;
    if (status == DAW_ACTION_STALE_TARGET) return DAW_MIDI_STALE_TARGET;
    return status == DAW_ACTION_UNCHANGED && target.kind == DAW_ACTION_TARGET_MIDI_REGION ? DAW_MIDI_UNCHANGED : DAW_MIDI_INVALID;
}
// Compares semantic note values without reading uninitialized struct padding.
static bool equal_notes(const EngineMidiNote* a, int na, const EngineMidiNote* b, int nb) {
    if (na != nb || na < 0 || na > ENGINE_MIDI_NOTE_CAP || (na && (!a || !b))) return false;
    for (int i = 0; i < na; ++i) {
        if (a[i].start_frame != b[i].start_frame || a[i].duration_frames != b[i].duration_frames ||
            a[i].note != b[i].note || a[i].velocity != b[i].velocity) return false;
    }
    return true;
}
// Compares the full captured tempo map used to translate beat requests to region-relative frames.
static bool equal_tempo(const TempoMap* a, const TempoMap* b) {
    if (a->event_count < 0 || b->event_count < 0 || (a->event_count && (!a->events || !b->events))) return false;
    if (a->sample_rate != b->sample_rate || a->event_count != b->event_count) return false;
    for (int i = 0; i < a->event_count; ++i) {
        if (a->events[i].beat != b->events[i].beat || a->events[i].bpm != b->events[i].bpm) return false;
    }
    return true;
}
// Copies a complete bounded read without retaining pointers into mutable engine storage.
DawMidiResult daw_action_get_midi(const AppState* state, DawActionTarget target, DawMidiSnapshot* out) {
    if (!out || out->notes || out->tempo.events) return result(DAW_MIDI_INVALID, 0);
    int t, c;
    DawMidiStatus status = resolve(state, target, &t, &c);
    if (status != DAW_MIDI_UNCHANGED) return result(status, 0);
    const EngineClip* clip = &engine_get_tracks(state->engine)[t].clips[c];
    if (clip->midi_notes.note_count < 0 || clip->midi_notes.note_count > ENGINE_MIDI_NOTE_CAP ||
        state->tempo_map.event_count <= 0 || state->tempo_map.event_count > DAW_MIDI_TEMPO_EVENT_CAP || !state->tempo_map.events ||
        !isfinite(state->tempo_map.sample_rate) || state->tempo_map.sample_rate <= 0) return result(DAW_MIDI_INVALID, 0);
    for (int i=0;i<state->tempo_map.event_count;++i) {
        TempoEvent event=state->tempo_map.events[i];
        if(!isfinite(event.beat) || event.beat<0 || (i==0 && event.beat!=0) || (i>0 && event.beat<=state->tempo_map.events[i-1].beat) || !isfinite(event.bpm) || event.bpm<20 || event.bpm>300) return result(DAW_MIDI_INVALID,0);
    }
    memset(out, 0, sizeof(*out));
    out->target = target; out->start_frame = clip->timeline_start_frames;
    out->duration_frames = clip->duration_frames; out->offset_frames = clip->offset_frames;
    out->tempo.sample_rate = state->tempo_map.sample_rate;
    out->tempo.events=malloc((size_t)state->tempo_map.event_count*sizeof(*out->tempo.events));
    if (!out->tempo.events) {
        daw_midi_snapshot_free(out); return result(DAW_MIDI_ENGINE_REJECTED, 0);
    }
    out->tempo.event_count=out->tempo.event_capacity=state->tempo_map.event_count;
    memcpy(out->tempo.events,state->tempo_map.events,(size_t)out->tempo.event_count*sizeof(*out->tempo.events));
    out->note_count = clip->midi_notes.note_count;
    if (out->note_count) {
        out->notes = malloc((size_t)out->note_count * sizeof(*out->notes));
        if (!out->notes) { daw_midi_snapshot_free(out); return result(DAW_MIDI_ENGINE_REJECTED, 0); }
        memcpy(out->notes, clip->midi_notes.notes, (size_t)out->note_count * sizeof(*out->notes));
    }
    return result(DAW_MIDI_UNCHANGED, out->note_count);
}
// Retires only the independent storage owned by one copied snapshot.
void daw_midi_snapshot_free(DawMidiSnapshot* snapshot) {
    if (!snapshot) return;
    free(snapshot->notes); tempo_map_free(&snapshot->tempo); memset(snapshot, 0, sizeof(*snapshot));
}
// Validates frame subtraction before addition and refuses exact duplicate note events.
static bool valid_notes(const EngineMidiNote* notes, int count, uint64_t duration) {
    if (count < 0 || count > ENGINE_MIDI_NOTE_CAP || (count && !notes)) return false;
    for (int i = 0; i < count; ++i) {
        if (!engine_midi_note_is_valid(&notes[i]) || notes[i].start_frame > duration ||
            notes[i].duration_frames > duration - notes[i].start_frame) return false;
        for (int j = 0; j < i; ++j) {
            if (notes[i].start_frame==notes[j].start_frame && notes[i].duration_frames==notes[j].duration_frames && notes[i].note==notes[j].note) return false;
        }
    }
    return true;
}
// Clears note selection only when the edited region owns the currently displayed selection.
static void clear_note_selection(AppState* state, DawActionTarget target) {
    if (state->midi_editor_ui.selected_clip_creation_index != target.clip_id) return;
    state->midi_editor_ui.selected_note_index = -1;
    memset(state->midi_editor_ui.selected_note_indices, 0, sizeof(state->midi_editor_ui.selected_note_indices));
    if(state->midi_editor_ui.hover_clip_creation_index==target.clip_id)state->midi_editor_ui.hover_note_valid=false;
}
// Protects stopped creative edits and replay from recording or pending continuous editor ownership.
bool daw_midi_actions_busy(const AppState* state) {
    return state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording) ||
        engine_transport_requested_playing(state->engine) || state->midi_editor_ui.qwerty_test_enabled || state->midi_editor_ui.qwerty_record_armed ||
        state->midi_editor_ui.drag_active || state->midi_editor_ui.marquee_active || state->midi_editor_ui.note_press_pending || state->midi_editor_ui.shift_note_pending;
}
// Atomically applies append/replacement after exact state validation and preallocated history.
DawMidiResult daw_action_edit_midi(AppState* state, const DawMidiRequest* request) {
    if (!request || !request->expected || (request->mode != DAW_MIDI_APPEND && request->mode != DAW_MIDI_REPLACE)) return result(DAW_MIDI_INVALID, 0);
    const DawMidiSnapshot* expected = request->expected;
    DawMidiSnapshot current = {0};
    DawMidiResult read = daw_action_get_midi(state, expected->target, &current);
    if (read.status != DAW_MIDI_UNCHANGED) return read;
    DawMidiStatus status = DAW_MIDI_UNCHANGED;
    EngineMidiNoteList merged = {0};
    if (daw_midi_actions_busy(state)) { status = DAW_MIDI_BUSY; goto done; }
    if (expected->start_frame != current.start_frame || expected->duration_frames != current.duration_frames || expected->offset_frames != current.offset_frames ||
        !equal_notes(expected->notes, expected->note_count, current.notes, current.note_count) || !equal_tempo(&expected->tempo, &current.tempo)) { status = DAW_MIDI_STATE_CONFLICT; goto done; }
    if (!valid_notes(request->notes, request->note_count, current.duration_frames)) { status = DAW_MIDI_INVALID; goto done; }
    int base_count = request->mode == DAW_MIDI_APPEND ? current.note_count : 0;
    if (request->note_count > ENGINE_MIDI_NOTE_CAP - base_count) { status = DAW_MIDI_INVALID; goto done; }
    int count = base_count + request->note_count;
    EngineMidiNote* combined = count ? malloc((size_t)count * sizeof(*combined)) : NULL;
    if (count && !combined) { status = DAW_MIDI_ENGINE_REJECTED; goto done; }
    if (base_count) memcpy(combined, current.notes, (size_t)base_count * sizeof(*combined));
    if (request->note_count) memcpy(combined + base_count, request->notes, (size_t)request->note_count * sizeof(*combined));
    bool prepared = engine_midi_note_list_set(&merged, combined, count);
    free(combined);
    if (!prepared) { status = DAW_MIDI_ENGINE_REJECTED; goto done; }
    if (!valid_notes(merged.notes, merged.note_count, current.duration_frames)) { status = DAW_MIDI_INVALID; goto done; }
    if (equal_notes(current.notes, current.note_count, merged.notes, merged.note_count)) goto done;
    UndoCommand command = {.type = UNDO_CMD_MIDI_NOTE_EDIT};
    command.data.midi_note_edit = (UndoMidiNoteEdit){.target = current.target, .clip_start_frame = current.start_frame,
        .clip_duration_frames = current.duration_frames, .clip_offset_frames = current.offset_frames,
        .clip_creation_index = current.target.clip_id, .before_note_count = current.note_count, .before_notes = current.notes,
        .after_note_count = merged.note_count, .after_notes = merged.notes};
    if (!undo_manager_begin_drag(&state->undo, &command)) { status = DAW_MIDI_HISTORY_UNAVAILABLE; goto done; }
    int t, c;
    if (resolve(state, current.target, &t, &c) != DAW_MIDI_UNCHANGED || !engine_clip_midi_set_notes(state->engine, t, c, merged.notes, merged.note_count)) {
        undo_manager_cancel_drag(&state->undo); status = DAW_MIDI_ENGINE_REJECTED; goto done;
    }
    // The owner has not yielded and final history capacity was reserved before publication.
    if (!undo_manager_commit_drag(&state->undo, &state->undo.active_drag)) { status = DAW_MIDI_HISTORY_UNAVAILABLE; goto done; }
    clear_note_selection(state, current.target);
    status = DAW_MIDI_APPLIED;
done:
    read = result(status, status == DAW_MIDI_APPLIED ? merged.note_count : current.note_count);
    engine_midi_note_list_free(&merged); daw_midi_snapshot_free(&current); return read;
}
// Refuses stale replay rather than redirecting history to a reused array slot or overwriting new notes.
bool daw_midi_history_restore(AppState* state, const UndoMidiNoteEdit* edit, bool after) {
    int t, c;
    if (!edit || resolve(state, edit->target, &t, &c) != DAW_MIDI_UNCHANGED) return false;
    if(daw_midi_actions_busy(state))return false;
    const EngineClip* clip = &engine_get_tracks(state->engine)[t].clips[c];
    const EngineMidiNote* expected = after ? edit->before_notes : edit->after_notes;
    int expected_count = after ? edit->before_note_count : edit->after_note_count;
    if (clip->timeline_start_frames != edit->clip_start_frame || clip->duration_frames != edit->clip_duration_frames || clip->offset_frames != edit->clip_offset_frames ||
        !equal_notes(clip->midi_notes.notes, clip->midi_notes.note_count, expected, expected_count)) return false;
    bool ok = engine_clip_midi_set_notes(state->engine, t, c, after ? edit->after_notes : edit->before_notes, after ? edit->after_note_count : edit->before_note_count);
    if (ok) clear_note_selection(state, edit->target);
    return ok;
}

// Compares independently copied states using exact semantic fields rather than padding or wire rounding.
bool daw_midi_snapshot_equal(const DawMidiSnapshot* a, const DawMidiSnapshot* b) {
    return a && b && a->target.project_generation==b->target.project_generation && a->target.track_id==b->target.track_id &&
        a->target.clip_id==b->target.clip_id && a->target.kind==b->target.kind && a->start_frame==b->start_frame &&
        a->duration_frames==b->duration_frames && a->offset_frames==b->offset_frames &&
        equal_notes(a->notes,a->note_count,b->notes,b->note_count) && equal_tempo(&a->tempo,&b->tempo);
}
