#include "input/automation_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "undo/undo_manager.h"
#include "session_automation.h"

#include <stdlib.h>
#include <string.h>

bool automation_snapshot_from_clip(const EngineClip* clip,
                                   SessionAutomationLane** out_lanes,
                                   int* out_lane_count) {
    if (!out_lanes || !out_lane_count) {
        return false;
    }
    *out_lanes = NULL;
    *out_lane_count = 0;
    if (!clip || !clip->automation_lanes || clip->automation_lane_count <= 0) {
        return true;
    }
    SessionAutomationLane* lanes = (SessionAutomationLane*)calloc((size_t)clip->automation_lane_count,
                                                                   sizeof(SessionAutomationLane));
    if (!lanes) {
        return false;
    }
    for (int l = 0; l < clip->automation_lane_count; ++l) {
        const EngineAutomationLane* src = &clip->automation_lanes[l];
        lanes[l].target = src->target;
        lanes[l].domain = src->domain;
        lanes[l].before = src->before;
        lanes[l].after = src->after;
        lanes[l].point_count = src->point_count;
        if (src->point_count > 0) {
            lanes[l].points = (SessionAutomationPoint*)calloc((size_t)src->point_count,
                                                              sizeof(SessionAutomationPoint));
            if (!lanes[l].points) {
                int count = l + 1;
                for (int j = 0; j < count; ++j) {
                    free(lanes[j].points);
                    lanes[j].points = NULL;
                    lanes[j].point_count = 0;
                }
                free(lanes);
                return false;
            }
            for (int p = 0; p < src->point_count; ++p) {
                lanes[l].points[p].frame = src->points[p].frame;
                lanes[l].points[p].value = src->points[p].value;
                lanes[l].points[p].curve = src->points[p].curve;
            }
        }
    }
    *out_lanes = lanes;
    *out_lane_count = clip->automation_lane_count;
    return true;
}

void automation_snapshot_free(SessionAutomationLane** lanes, int* lane_count) {
    if (!lanes || !*lanes || !lane_count) {
        return;
    }
    for (int l = 0; l < *lane_count; ++l) {
        free((*lanes)[l].points);
        (*lanes)[l].points = NULL;
        (*lanes)[l].point_count = 0;
    }
    free(*lanes);
    *lanes = NULL;
    *lane_count = 0;
}

static EngineClip* automation_get_clip(AppState* state, int track_index, int clip_index) {
    if (!state || !state->engine) {
        return NULL;
    }
    EngineTrack* tracks = (EngineTrack*)engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return NULL;
    }
    EngineTrack* track = &tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[clip_index];
}

bool automation_begin_edit(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return false;
    }
    EngineClip* clip = automation_get_clip(state, track_index, clip_index);
    if (!clip) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_AUTOMATION_EDIT;
    cmd.data.automation_edit.track_index = track_index;
    cmd.data.automation_edit.clip_index = clip_index;
    cmd.data.automation_edit.track_runtime_id = engine_get_tracks(state->engine)[track_index].runtime_id;
    cmd.data.automation_edit.clip_creation_id = clip->creation_index;
    if (!automation_snapshot_from_clip(clip,
                                       &cmd.data.automation_edit.before_lanes,
                                       &cmd.data.automation_edit.before_lane_count)) {
        automation_snapshot_free(&cmd.data.automation_edit.before_lanes,
                                 &cmd.data.automation_edit.before_lane_count);
        return false;
    }
    if (!automation_snapshot_from_clip(clip, &cmd.data.automation_edit.after_lanes,
                                       &cmd.data.automation_edit.after_lane_count)) {
        automation_snapshot_free(&cmd.data.automation_edit.before_lanes, &cmd.data.automation_edit.before_lane_count);
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    automation_snapshot_free(&cmd.data.automation_edit.before_lanes, &cmd.data.automation_edit.before_lane_count);
    automation_snapshot_free(&cmd.data.automation_edit.after_lanes, &cmd.data.automation_edit.after_lane_count);
    if (ok) state->automation_ui.history_serial = state->undo.drag_serial;
    return ok;
}

// Compares authored snapshots field by field so padding cannot create false edits.
static bool snapshots_equal(const UndoAutomationEdit* edit) {
    if (edit->before_lane_count != edit->after_lane_count) return false;
    for (int l = 0; l < edit->before_lane_count; ++l) {
        const SessionAutomationLane* a = &edit->before_lanes[l];
        const SessionAutomationLane* b = &edit->after_lanes[l];
        if (a->target != b->target || a->domain != b->domain || a->before != b->before ||
            a->after != b->after || a->point_count != b->point_count) return false;
        for (int p = 0; p < a->point_count; ++p)
            if (a->points[p].frame != b->points[p].frame || a->points[p].value != b->points[p].value ||
                a->points[p].curve != b->points[p].curve) return false;
    }
    return true;
}

// Completes only the reservation owned by this automation gesture, without taking a new snapshot.
bool automation_commit_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid || state->undo.active_drag.type != UNDO_CMD_AUTOMATION_EDIT ||
        state->automation_ui.history_serial != state->undo.drag_serial) return false;
    if (snapshots_equal(&state->undo.active_drag.data.automation_edit)) {
        undo_manager_cancel_drag(&state->undo);
        state->automation_ui.history_serial = 0;
        return true;
    }
    bool ok = undo_manager_commit_drag(&state->undo, &state->undo.active_drag);
    if (ok) state->automation_ui.history_serial = 0;
    return ok;
}

// Discards an owned reservation before publication or retains its last accepted edit as undoable history.
void automation_cancel_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid || state->undo.active_drag.type != UNDO_CMD_AUTOMATION_EDIT ||
        state->automation_ui.history_serial != state->undo.drag_serial) return;
    UndoAutomationEdit* edit = &state->undo.active_drag.data.automation_edit;
    int track = -1, clip = -1;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; tracks && t < engine_get_track_count(state->engine); ++t)
        if (tracks[t].runtime_id == edit->track_runtime_id)
            for (int c = 0; c < tracks[t].clip_count; ++c)
                if (tracks[t].clips[c].creation_index == edit->clip_creation_id) { track = t; clip = c; }
    if (track >= 0 && session_apply_clip_automation(state->engine, track, clip, edit->before_lanes, edit->before_lane_count)) {
        undo_manager_cancel_drag(&state->undo);
        state->automation_ui.history_serial = 0;
    } else automation_commit_edit(state);
}
