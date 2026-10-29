#include "app_state.h"
#include "daw/routing_actions.h"
#include "daw/midi_actions.h"
#include <string.h>
#include <stdlib.h>

// Classifies a pure fold transition without permitting role, parent, order, send or lifetime changes.
static bool fold_only(const EngineRoutingSnapshot* a,const EngineRoutingSnapshot* b) {
    if(!a || !b || a->track_count!=b->track_count || a->generation!=b->generation)return false;
    EngineRoutingSnapshot copy=*b;
    for(int i=0;i<copy.track_count;++i)copy.tracks[i].folded=a->tracks[i].folded;
    return engine_routing_snapshot_equal(a,&copy);
}
// Permits view exploration during transport playback while preserving recording/gesture/export guards.
static bool routing_busy(const AppState* s,const EngineRoutingSnapshot* a,const EngineRoutingSnapshot* b) {
    if(!fold_only(a,b))return daw_midi_actions_busy(s);
    return s->undo.active_drag_valid || s->bounce_active || daw_audio_recording_is_active(&s->audio_recording) ||
        s->midi_editor_ui.qwerty_test_enabled || s->midi_editor_ui.qwerty_record_armed || s->midi_editor_ui.drag_active ||
        s->midi_editor_ui.marquee_active || s->midi_editor_ui.note_press_pending || s->midi_editor_ui.shift_note_pending;
}

// Captures complete routing only from the current application's owner thread.
DawActionStatus daw_action_get_routing(const AppState* state, EngineRoutingSnapshot* out) {
    if (!state || !state->engine || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    return engine_routing_capture(state->engine, out) ? DAW_ACTION_UNCHANGED : DAW_ACTION_ENGINE_REJECTED;
}

// Refuses stale or conflicting work before validating and reserving one complete routing command.
DawActionStatus daw_action_set_routing(AppState* state, const EngineRoutingSnapshot* expected,
                                     const EngineRoutingSnapshot* next, EngineRoutingSnapshot* out) {
    if (!state || !state->engine || !expected || !next || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    DawRoutingHistory* history = calloc(1, sizeof(*history));
    if (!history) return DAW_ACTION_HISTORY_UNAVAILABLE;
    DawActionStatus status = daw_action_get_routing(state, &history->before);
    if (status != DAW_ACTION_UNCHANGED) goto done;
    if (!engine_routing_snapshot_equal(expected, &history->before)) { status = DAW_ACTION_STALE_TARGET; goto done; }
    if (routing_busy(state, &history->before, next)) { status = DAW_ACTION_BUSY; goto done; }
    if (!engine_routing_validate(state->engine, next)) { status = DAW_ACTION_INVALID; goto done; }
    if (engine_routing_snapshot_equal(&history->before, next)) { *out = history->before; goto done; }
    history->after = *next;
    UndoCommand command = {.type = UNDO_CMD_ROUTING}; command.data.routing = history;
    if (!undo_manager_begin_drag(&state->undo, &command)) { status = DAW_ACTION_HISTORY_UNAVAILABLE; goto done; }
    DawRoutingHistory* reserved = state->undo.active_drag.data.routing;
    if (!engine_routing_set(state->engine, &reserved->before, &reserved->after)) {
        undo_manager_cancel_drag(&state->undo); status = DAW_ACTION_ENGINE_REJECTED; goto done;
    }
    *out = reserved->after;
    status = undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
done:
    free(history);
    return status;
}

// Creates one trailing return without a separately visible ordinary-lane intermediate revision.
static DawActionStatus create_bus(AppState* state, const EngineRoutingSnapshot* expected,
                                  const char* name, EngineRoutingSnapshot* out) {
    if (!state || !state->engine || !expected || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    DawRoutingHistory* history = calloc(1, sizeof(*history));
    if (!history) return DAW_ACTION_HISTORY_UNAVAILABLE;
    EngineRoutingSnapshot* current = &history->before;
    DawActionStatus status = daw_action_get_routing(state, current);
    if (status != DAW_ACTION_UNCHANGED) goto done;
    if (!engine_routing_snapshot_equal(expected, current)) { status = DAW_ACTION_STALE_TARGET; goto done; }
    if (daw_midi_actions_busy(state)) { status = DAW_ACTION_BUSY; goto done; }
    int returns = 0;
    for (int t = 0; t < current->track_count; ++t) if (current->tracks[t].is_return) ++returns;
    if ((!name && returns >= ENGINE_RETURN_CAP) || current->track_count >= ENGINE_ROUTING_TRACK_CAP) { status = DAW_ACTION_INVALID; goto done; }
    history->after = *current; history->creates_return = true;
    UndoCommand command = {.type = UNDO_CMD_ROUTING}; command.data.routing = history;
    if (!undo_manager_begin_drag(&state->undo, &command)) { status = DAW_ACTION_HISTORY_UNAVAILABLE; goto done; }
    DawRoutingHistory* reserved = state->undo.active_drag.data.routing;
    uint64_t identity;
    if (!(name ? engine_append_group(state->engine, 0, name, &identity) : engine_append_return(state->engine, 0, &identity))) {
        undo_manager_cancel_drag(&state->undo); status = DAW_ACTION_ENGINE_REJECTED; goto done;
    }
    reserved->after.tracks[reserved->after.track_count++] = (EngineRoutingTrack){.track_id = identity, .is_return = !name, .is_group = name != NULL};
    undo_created_track_capture(&engine_get_tracks(state->engine)[current->track_count], &reserved->created);
    *out = reserved->after;
    status = undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
done:
    free(history);
    return status;
}

// Creates a named group with one reserved shared-history transition.
DawActionStatus daw_action_create_group(AppState* state,const EngineRoutingSnapshot* expected,const char* name,EngineRoutingSnapshot* out) {
    if(!name || !name[0] || strlen(name)>=ENGINE_CLIP_NAME_MAX)return DAW_ACTION_INVALID;
    return create_bus(state,expected,name,out);
}
// Retains the existing return-creation API and its history contract.
DawActionStatus daw_action_create_return(AppState* state,const EngineRoutingSnapshot* expected,EngineRoutingSnapshot* out) {
    return create_bus(state,expected,NULL,out);
}

// Protects later authored content before creation undo is allowed to remove the owned return.
static bool created_return_unchanged(const AppState* state, const DawRoutingHistory* history) {
    int index = history->before.track_count;
    const EngineTrack* track = &engine_get_tracks(state->engine)[index];
    UndoCreatedTrack current; undo_created_track_capture(track, &current);
    FxMasterSnapshot effects;
    return (track->is_return || track->is_group) && !track->clip_count && !track->midi_instrument_automation_lane_count &&
        !memcmp(&current, &history->created, sizeof(current)) &&
        engine_fx_track_snapshot(state->engine, index, &effects) && !effects.count;
}

// Restores routing without overwriting unrelated lane content, mixer or processor settings.
bool daw_routing_history_restore(AppState* state, const DawRoutingHistory* history, bool after) {
    if (!history) return false;
    const EngineRoutingSnapshot* expected = after ? &history->before : &history->after;
    const EngineRoutingSnapshot* value = after ? &history->after : &history->before;
    EngineRoutingSnapshot current;
    if (daw_action_get_routing(state, &current) != DAW_ACTION_UNCHANGED ||
        !engine_routing_snapshot_equal(expected, &current) ||
        (history->creates_return ? daw_midi_actions_busy(state) : routing_busy(state,expected,value))) return false;
    if (history->creates_return) {
        if (history->before.track_count < 0 || history->before.track_count >= ENGINE_ROUTING_TRACK_CAP ||
            history->after.track_count != history->before.track_count + 1 ||
            history->created.runtime_id != history->after.tracks[history->before.track_count].track_id) return false;
        EngineRoutingSnapshot prefix = history->after;
        const EngineRoutingTrack* added = &prefix.tracks[history->before.track_count];
        if ((!added->is_return && !added->is_group) || added->parent_id || added->folded || added->sibling_order) return false;
        for (int send = 0; send < ENGINE_SEND_CAP; ++send)
            if (added->sends[send].return_id || added->sends[send].gain) return false;
        --prefix.track_count;
        if (!engine_routing_snapshot_equal(&prefix, &history->before)) return false;
        if (!after) return created_return_unchanged(state, history) && engine_remove_track(state->engine, history->before.track_count);
        uint64_t identity;
        return added->is_group ? engine_append_group(state->engine,history->created.runtime_id,history->created.name,&identity) :
            engine_append_return(state->engine, history->created.runtime_id, &identity);
    }
    return engine_routing_set(state->engine, &current, value);
}

// Consumes only routing history at the current cursor after validating the complete current capture.
DawActionStatus daw_action_routing_history(AppState* state, const EngineRoutingSnapshot* expected,
                                         bool redo, EngineRoutingSnapshot* out) {
    if (!state || !state->engine || !expected || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    EngineRoutingSnapshot* current = malloc(sizeof(*current));
    if (!current) return DAW_ACTION_HISTORY_UNAVAILABLE;
    DawActionStatus status = daw_action_get_routing(state, current);
    if (status != DAW_ACTION_UNCHANGED) goto done;
    if (!engine_routing_snapshot_equal(expected, current)) { status = DAW_ACTION_STALE_TARGET; goto done; }
    int count = redo ? state->undo.redo_count : state->undo.undo_count;
    const UndoCommand* stack = redo ? state->undo.redo_stack : state->undo.undo_stack;
    if (!count || stack[count - 1].type != UNDO_CMD_ROUTING) { status = DAW_ACTION_HISTORY_UNAVAILABLE; goto done; }
    if (routing_busy(state, &stack[count-1].data.routing->before, &stack[count-1].data.routing->after)) { status = DAW_ACTION_BUSY; goto done; }
    *current = redo ? stack[count - 1].data.routing->after : stack[count - 1].data.routing->before;
    if (!(redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state))) { status = DAW_ACTION_ENGINE_REJECTED; goto done; }
    *out = *current; status = DAW_ACTION_APPLIED;
done:
    free(current);
    return status;
}
