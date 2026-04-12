#pragma once

#include "engine/engine.h"

void engine_clip_init_automation(EngineClip* clip);
bool engine_clip_copy_automation(const EngineClip* src, EngineClip* dst);
bool engine_clip_set_automation_lanes_internal(EngineClip* clip,
                                               const EngineAutomationLane* lanes,
                                               int lane_count);
bool engine_clip_snapshot_automation(const EngineClip* clip,
                                     EngineAutomationLane** out_lanes,
                                     int* out_lane_count);
