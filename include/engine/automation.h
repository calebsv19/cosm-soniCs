#pragma once

#include <stdbool.h>
#include <stdint.h>

// Defines the automation targets supported by the engine.
typedef enum {
    ENGINE_AUTOMATION_TARGET_VOLUME = 0,
    ENGINE_AUTOMATION_TARGET_PAN,
    ENGINE_AUTOMATION_TARGET_COUNT
} EngineAutomationTarget;

// Stores a single automation point at a frame with a normalized value.
typedef struct {
    uint64_t frame;
    float value;
} EngineAutomationPoint;

// Stores a lane of automation points for a specific target.
typedef struct {
    EngineAutomationTarget target;
    EngineAutomationPoint* points;
    int point_count;
    int point_capacity;
} EngineAutomationLane;

// Resets an automation lane to a target with no points.
void engine_automation_lane_init(EngineAutomationLane* lane, EngineAutomationTarget target);
// Releases memory owned by an automation lane.
void engine_automation_lane_free(EngineAutomationLane* lane);
// Deep-copies an automation lane into another lane.
bool engine_automation_lane_copy(const EngineAutomationLane* src, EngineAutomationLane* dst);
// Replaces a lane's points with a deep copy of the provided list.
bool engine_automation_lane_set_points(EngineAutomationLane* lane, const EngineAutomationPoint* points, int count);
// Inserts or replaces a point and returns its index.
bool engine_automation_lane_insert_point(EngineAutomationLane* lane,
                                         uint64_t frame,
                                         float value,
                                         int* out_index);
// Updates an existing point and returns its new index after sorting.
bool engine_automation_lane_update_point(EngineAutomationLane* lane,
                                         int point_index,
                                         uint64_t frame,
                                         float value,
                                         int* out_index);
// Removes a point at the given index.
bool engine_automation_lane_remove_point(EngineAutomationLane* lane, int point_index);
// Evaluates the automation value at a frame using linear interpolation with baseline endpoints.
float engine_automation_lane_eval(const EngineAutomationLane* lane, uint64_t frame, uint64_t clip_frames);
