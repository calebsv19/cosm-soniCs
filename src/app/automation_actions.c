#include "daw/automation_actions.h"
#include "app_state.h"
#include "input/automation_input.h"
#include <stdlib.h>

// Finds a currently valid region before preparing a command.
static const EngineClip* get_clip(AppState* state, int track, int clip) {
    if (!state || !state->engine || track < 0 || track >= engine_get_track_count(state->engine)) return NULL;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    return tracks && clip >= 0 && clip < tracks[track].clip_count ? &tracks[track].clips[clip] : NULL;
}

// Prepares the after snapshot before publication and transfers reserved history without release allocation.
bool daw_automation_replace_lanes(AppState* state, int track, int clip, const EngineAutomationLane* lanes, int count) {
    const EngineClip* current = get_clip(state, track, clip);
    if (!current || count < 0 || count > ENGINE_AUTOMATION_TARGET_COUNT || (count && !lanes)) return false;
    for (int i = 0; i < count; ++i)
        if (!engine_automation_lane_validate(&lanes[i], current->duration_frames)) return false;
    EngineClip candidate = *current;
    candidate.automation_lanes = (EngineAutomationLane*)lanes;
    candidate.automation_lane_count = count;
    SessionAutomationLane* after = NULL;
    int after_count = 0;
    if (!automation_snapshot_from_clip(&candidate, &after, &after_count)) return false;
    bool discrete = !state->undo.active_drag_valid;
    if (discrete && !automation_begin_edit(state, track, clip)) {
        automation_snapshot_free(&after, &after_count);
        return false;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    bool owned = state->undo.active_drag_valid && cmd->type == UNDO_CMD_AUTOMATION_EDIT &&
        state->automation_ui.history_serial == state->undo.drag_serial &&
        cmd->data.automation_edit.track_runtime_id == tracks[track].runtime_id &&
        cmd->data.automation_edit.clip_creation_id == current->creation_index;
    if (!owned || !engine_clip_set_automation_lanes(state->engine, track, clip, lanes, count)) {
        automation_snapshot_free(&after, &after_count);
        if (discrete) { undo_manager_cancel_drag(&state->undo); state->automation_ui.history_serial = 0; }
        return false;
    }
    automation_snapshot_free(&cmd->data.automation_edit.after_lanes, &cmd->data.automation_edit.after_lane_count);
    cmd->data.automation_edit.after_lanes = after;
    cmd->data.automation_edit.after_lane_count = after_count;
    return !discrete || automation_commit_edit(state);
}

// Copies region content into private lane storage so refused point edits never leak into playback.
static bool edit_point(AppState* state, int track, int clip, EngineAutomationTarget target,
                       int point, uint64_t frame, float value, int* out_index, int operation) {
    const EngineClip* current = get_clip(state, track, clip);
    if (!current || target < 0 || target >= ENGINE_AUTOMATION_TARGET_COUNT) return false;
    int count = current->automation_lane_count;
    EngineAutomationLane* lanes = calloc((size_t)count + 1, sizeof(*lanes));
    if (!lanes) return false;
    int selected = -1;
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        if (!engine_automation_lane_copy(&current->automation_lanes[i], &lanes[i])) { ok = false; break; }
        if (lanes[i].target == target) selected = i;
    }
    if (ok && selected < 0 && operation == 0) {
        selected = count++;
        engine_automation_lane_init(&lanes[selected], target);
    }
    int index = point;
    if (ok && selected >= 0) {
        EngineAutomationLane* lane = &lanes[selected];
        if (operation == 3) {
            ok = point >= 0 && point < lane->point_count && value >= 0 && value < ENGINE_AUTOMATION_CURVE_COUNT;
            if (ok) lane->points[point].curve = (EngineAutomationCurve)(int)value;
        } else if (operation == 4) {
            ok = value >= 0 && value < ENGINE_AUTOMATION_BOUNDARY_COUNT;
            if (ok && point == 0) lane->before = (EngineAutomationBoundary)(int)value;
            else if (ok) lane->after = (EngineAutomationBoundary)(int)value;
        } else {
            ok = operation == 0 ? engine_automation_lane_insert_point(lane, frame, value, &index) :
                 operation == 1 ? engine_automation_lane_update_point(lane, point, frame, value, &index) :
                                  engine_automation_lane_remove_point(lane, point);
        }
        if (ok) ok = daw_automation_replace_lanes(state, track, clip, lanes, count);
    } else ok = false;
    for (int i = 0; i < count; ++i) engine_automation_lane_free(&lanes[i]);
    free(lanes);
    if (ok && out_index) *out_index = index;
    return ok;
}

// Adds a point without exposing partially prepared history or lane content.
bool daw_automation_add_point(AppState* state, int track, int clip, EngineAutomationTarget target, uint64_t frame, float value, int* index) {
    return edit_point(state, track, clip, target, -1, frame, value, index, 0);
}
// Moves a point through the common region command.
bool daw_automation_update_point(AppState* state, int track, int clip, EngineAutomationTarget target, int point, uint64_t frame, float value, int* index) {
    return edit_point(state, track, clip, target, point, frame, value, index, 1);
}
// Deletes a point as one undoable command.
bool daw_automation_remove_point(AppState* state, int track, int clip, EngineAutomationTarget target, int point) {
    return edit_point(state, track, clip, target, point, 0, 0, NULL, 2);
}

// Applies outgoing point shape with allocation and publication refusal retaining the original lane.
bool daw_automation_set_curve(AppState* state, int track, int clip, EngineAutomationTarget target, int point, EngineAutomationCurve curve) {
    return edit_point(state, track, clip, target, point, 0, (float)curve, NULL, 3);
}
// Applies a single boundary policy without discarding another lane's metadata.
bool daw_automation_set_boundary(AppState* state, int track, int clip, EngineAutomationTarget target, bool before, EngineAutomationBoundary boundary) {
    return edit_point(state, track, clip, target, before ? 0 : 1, 0, (float)boundary, NULL, 4);
}
