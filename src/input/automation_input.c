#include "input/automation_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "undo/undo_manager.h"

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
    if (!automation_snapshot_from_clip(clip,
                                       &cmd.data.automation_edit.before_lanes,
                                       &cmd.data.automation_edit.before_lane_count)) {
        automation_snapshot_free(&cmd.data.automation_edit.before_lanes,
                                 &cmd.data.automation_edit.before_lane_count);
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    automation_snapshot_free(&cmd.data.automation_edit.before_lanes,
                             &cmd.data.automation_edit.before_lane_count);
    return ok;
}

bool automation_commit_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid) {
        return false;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    if (cmd->type != UNDO_CMD_AUTOMATION_EDIT) {
        return false;
    }
    EngineClip* clip = automation_get_clip(state,
                                           cmd->data.automation_edit.track_index,
                                           cmd->data.automation_edit.clip_index);
    if (!clip) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    automation_snapshot_free(&cmd->data.automation_edit.after_lanes,
                             &cmd->data.automation_edit.after_lane_count);
    if (!automation_snapshot_from_clip(clip,
                                       &cmd->data.automation_edit.after_lanes,
                                       &cmd->data.automation_edit.after_lane_count)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    return undo_manager_commit_drag(&state->undo, cmd);
}

void automation_cancel_edit(AppState* state) {
    if (!state || !state->undo.active_drag_valid) {
        return;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    if (cmd->type != UNDO_CMD_AUTOMATION_EDIT) {
        return;
    }
    undo_manager_cancel_drag(&state->undo);
}
