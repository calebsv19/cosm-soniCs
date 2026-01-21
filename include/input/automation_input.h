#pragma once

#include <stdbool.h>

#include "session.h"

struct AppState;
struct EngineClip;

// Copies automation lanes from an engine clip into session lanes.
bool automation_snapshot_from_clip(const struct EngineClip* clip,
                                   SessionAutomationLane** out_lanes,
                                   int* out_lane_count);
// Frees a snapshot of session automation lanes.
void automation_snapshot_free(SessionAutomationLane** lanes, int* lane_count);
// Begins an automation edit undo drag for a clip.
bool automation_begin_edit(struct AppState* state, int track_index, int clip_index);
// Commits an automation edit undo drag with the current clip state.
bool automation_commit_edit(struct AppState* state);
// Cancels any active automation edit drag.
void automation_cancel_edit(struct AppState* state);
