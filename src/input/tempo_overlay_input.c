#include "input/tempo_overlay_input.h"

#include "app_state.h"
#include "undo/undo_manager.h"

#include <stdlib.h>
#include <string.h>

// Captures a snapshot of the tempo map for undo/redo.
static bool tempo_map_snapshot(const TempoMap* map, TempoEvent** out_events, int* out_count) {
    if (!out_events || !out_count) {
        return false;
    }
    *out_events = NULL;
    *out_count = 0;
    if (!map || map->event_count <= 0 || !map->events) {
        return true;
    }
    TempoEvent* events = (TempoEvent*)calloc((size_t)map->event_count, sizeof(TempoEvent));
    if (!events) {
        return false;
    }
    memcpy(events, map->events, sizeof(TempoEvent) * (size_t)map->event_count);
    *out_events = events;
    *out_count = map->event_count;
    return true;
}

static void tempo_map_snapshot_free(TempoEvent** events, int* count) {
    if (!events || !*events || !count) {
        return;
    }
    free(*events);
    *events = NULL;
    *count = 0;
}

static bool tempo_map_snapshot_matches(const TempoEvent* a, int a_count, const TempoEvent* b, int b_count) {
    if (a_count != b_count) {
        return false;
    }
    if (a_count == 0) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return memcmp(a, b, sizeof(TempoEvent) * (size_t)a_count) == 0;
}

bool tempo_overlay_begin_edit(AppState* state) {
    if (!state) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_TEMPO_MAP_EDIT;
    if (!tempo_map_snapshot(&state->tempo_map,
                            &cmd.data.tempo_map_edit.before_events,
                            &cmd.data.tempo_map_edit.before_event_count)) {
        tempo_map_snapshot_free(&cmd.data.tempo_map_edit.before_events,
                                &cmd.data.tempo_map_edit.before_event_count);
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    tempo_map_snapshot_free(&cmd.data.tempo_map_edit.before_events,
                            &cmd.data.tempo_map_edit.before_event_count);
    return ok;
}

bool tempo_overlay_commit_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid) {
        return false;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    if (cmd->type != UNDO_CMD_TEMPO_MAP_EDIT) {
        return false;
    }
    tempo_map_snapshot_free(&cmd->data.tempo_map_edit.after_events,
                            &cmd->data.tempo_map_edit.after_event_count);
    if (!tempo_map_snapshot(&state->tempo_map,
                            &cmd->data.tempo_map_edit.after_events,
                            &cmd->data.tempo_map_edit.after_event_count)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    if (tempo_map_snapshot_matches(cmd->data.tempo_map_edit.before_events,
                                   cmd->data.tempo_map_edit.before_event_count,
                                   cmd->data.tempo_map_edit.after_events,
                                   cmd->data.tempo_map_edit.after_event_count)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    return undo_manager_commit_drag(&state->undo, cmd);
}

void tempo_overlay_cancel_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid) {
        return;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    if (cmd->type != UNDO_CMD_TEMPO_MAP_EDIT) {
        return;
    }
    undo_manager_cancel_drag(&state->undo);
}
