#include "app_state.h"
#include "daw/routing_actions.h"
#include "daw/midi_actions.h"
#include <string.h>

// Captures complete routing only from the current application's owner thread.
DawActionStatus daw_action_get_routing(const AppState* state, EngineRoutingSnapshot* out) {
    if (!state || !state->engine || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    return engine_routing_capture(state->engine, out) ? DAW_ACTION_UNCHANGED : DAW_ACTION_ENGINE_REJECTED;
}

// Refuses stale or conflicting work before validating and reserving one complete routing command.
DawActionStatus daw_action_set_routing(AppState* state, const EngineRoutingSnapshot* expected,
                                     const EngineRoutingSnapshot* next, EngineRoutingSnapshot* out) {
    if (!expected || !next || !out) return DAW_ACTION_INVALID;
    EngineRoutingSnapshot current;
    DawActionStatus status = daw_action_get_routing(state, &current);
    if (status != DAW_ACTION_UNCHANGED) return status;
    if (!engine_routing_snapshot_equal(expected, &current)) return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(state)) return DAW_ACTION_BUSY;
    if (!engine_routing_validate(state->engine, next)) return DAW_ACTION_INVALID;
    if (engine_routing_snapshot_equal(&current, next)) { *out = current; return DAW_ACTION_UNCHANGED; }
    DawRoutingHistory history = {.before = current, .after = *next};
    UndoCommand command = {.type = UNDO_CMD_ROUTING};
    command.data.routing = &history;
    if (!undo_manager_begin_drag(&state->undo, &command)) return DAW_ACTION_HISTORY_UNAVAILABLE;
    DawRoutingHistory* reserved = state->undo.active_drag.data.routing;
    if (!engine_routing_set(state->engine, &reserved->before, &reserved->after)) {
        undo_manager_cancel_drag(&state->undo);
        return DAW_ACTION_ENGINE_REJECTED;
    }
    *out = reserved->after;
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ?
        DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
}

// Creates one trailing return without a separately visible ordinary-lane intermediate revision.
DawActionStatus daw_action_create_return(AppState* state, const EngineRoutingSnapshot* expected,
                                        EngineRoutingSnapshot* out) {
    if (!expected || !out) return DAW_ACTION_INVALID;
    EngineRoutingSnapshot current;
    DawActionStatus status = daw_action_get_routing(state, &current);
    if (status != DAW_ACTION_UNCHANGED) return status;
    if (!engine_routing_snapshot_equal(expected, &current)) return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(state)) return DAW_ACTION_BUSY;
    int returns = 0;
    for (int t = 0; t < current.track_count; ++t) if (current.tracks[t].is_return) ++returns;
    if (returns >= ENGINE_RETURN_CAP || current.track_count >= ENGINE_ROUTING_TRACK_CAP) return DAW_ACTION_INVALID;
    DawRoutingHistory history = {.before = current, .after = current, .creates_return = true};
    UndoCommand command = {.type = UNDO_CMD_ROUTING}; command.data.routing = &history;
    if (!undo_manager_begin_drag(&state->undo, &command)) return DAW_ACTION_HISTORY_UNAVAILABLE;
    DawRoutingHistory* reserved = state->undo.active_drag.data.routing;
    uint64_t identity;
    if (!engine_append_return(state->engine, 0, &identity)) {
        undo_manager_cancel_drag(&state->undo); return DAW_ACTION_ENGINE_REJECTED;
    }
    reserved->after.tracks[reserved->after.track_count++] = (EngineRoutingTrack){.track_id = identity, .is_return = true};
    undo_created_track_capture(&engine_get_tracks(state->engine)[current.track_count], &reserved->created);
    *out = reserved->after;
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
}

// Protects later authored content before creation undo is allowed to remove the owned return.
static bool created_return_unchanged(const AppState* state, const DawRoutingHistory* history) {
    int index = history->before.track_count;
    const EngineTrack* track = &engine_get_tracks(state->engine)[index];
    UndoCreatedTrack current; undo_created_track_capture(track, &current);
    FxMasterSnapshot effects;
    return track->is_return && !track->clip_count && !track->midi_instrument_automation_lane_count &&
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
        !engine_routing_snapshot_equal(expected, &current) || daw_midi_actions_busy(state)) return false;
    if (history->creates_return) {
        if (history->before.track_count < 0 || history->before.track_count >= ENGINE_ROUTING_TRACK_CAP ||
            history->after.track_count != history->before.track_count + 1 ||
            history->created.runtime_id != history->after.tracks[history->before.track_count].track_id) return false;
        EngineRoutingSnapshot prefix = history->after;
        const EngineRoutingTrack* added = &prefix.tracks[history->before.track_count];
        if (!added->is_return) return false;
        for (int send = 0; send < ENGINE_SEND_CAP; ++send)
            if (added->sends[send].return_id || added->sends[send].gain) return false;
        --prefix.track_count;
        if (!engine_routing_snapshot_equal(&prefix, &history->before)) return false;
        if (!after) return created_return_unchanged(state, history) && engine_remove_track(state->engine, history->before.track_count);
        uint64_t identity;
        return engine_append_return(state->engine, history->created.runtime_id, &identity);
    }
    return engine_routing_set(state->engine, &current, value);
}

// Consumes only routing history at the current cursor after validating the complete current capture.
DawActionStatus daw_action_routing_history(AppState* state, const EngineRoutingSnapshot* expected,
                                         bool redo, EngineRoutingSnapshot* out) {
    if (!expected || !out) return DAW_ACTION_INVALID;
    EngineRoutingSnapshot current;
    DawActionStatus status = daw_action_get_routing(state, &current);
    if (status != DAW_ACTION_UNCHANGED) return status;
    if (!engine_routing_snapshot_equal(expected, &current)) return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(state)) return DAW_ACTION_BUSY;
    int count = redo ? state->undo.redo_count : state->undo.undo_count;
    const UndoCommand* stack = redo ? state->undo.redo_stack : state->undo.undo_stack;
    if (!count || stack[count - 1].type != UNDO_CMD_ROUTING) return DAW_ACTION_HISTORY_UNAVAILABLE;
    EngineRoutingSnapshot desired = redo ? stack[count - 1].data.routing->after : stack[count - 1].data.routing->before;
    if (!(redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state)))
        return DAW_ACTION_ENGINE_REJECTED;
    *out = desired;
    return DAW_ACTION_APPLIED;
}
