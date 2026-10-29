#pragma once

#include <stdbool.h>
#include <stdint.h>

// Defines the automation targets supported by the engine.
typedef enum {
    ENGINE_AUTOMATION_TARGET_VOLUME = 0,
    ENGINE_AUTOMATION_TARGET_PAN,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_ATTACK_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_DECAY_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_OSC_MIX,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_OSC2_DETUNE,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUB_MIX,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_DRIVE,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_RATE,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_DEPTH,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_CUTOFF_HZ,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_RESONANCE_Q,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_ENV_OCTAVES,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_DECAY_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_LFO_HZ,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_LFO_OCTAVES,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_BODY_DECAY_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_PITCH_ENV_SEMITONES,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_PITCH_DECAY_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_NOISE_MIX,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_NOISE_DECAY_MS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_CLICK_MIX,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_DYNAMICS,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_VELOCITY_DEPTH,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN_PEDAL,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_STRING_RESONANCE,
    ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_NOISE,
    ENGINE_AUTOMATION_TARGET_COUNT
} EngineAutomationTarget;

// Defines the outgoing interpolation shape of a point; zero preserves legacy linear segments.
typedef enum {
    ENGINE_AUTOMATION_CURVE_LINEAR = 0,
    ENGINE_AUTOMATION_CURVE_HOLD,
    ENGINE_AUTOMATION_CURVE_SMOOTH,
    ENGINE_AUTOMATION_CURVE_COUNT
} EngineAutomationCurve;

// Defines whether values use legacy additive offsets or explicit physical target units.
typedef enum {
    ENGINE_AUTOMATION_DOMAIN_OFFSET = 0,
    ENGINE_AUTOMATION_DOMAIN_PHYSICAL,
    ENGINE_AUTOMATION_DOMAIN_COUNT
} EngineAutomationDomain;

// Defines extrapolation at a region boundary; zero preserves the legacy neutral endpoint ramp.
typedef enum {
    ENGINE_AUTOMATION_BOUNDARY_BASELINE = 0,
    ENGINE_AUTOMATION_BOUNDARY_HOLD,
    ENGINE_AUTOMATION_BOUNDARY_COUNT
} EngineAutomationBoundary;

#define ENGINE_AUTOMATION_POINT_LIMIT 65536

// Stores a frame, lane-domain value, and outgoing segment curve.

typedef struct {
    uint64_t frame;
    float value;
    EngineAutomationCurve curve;
} EngineAutomationPoint;

// Stores a lane of automation points for a specific target.
typedef struct {
    EngineAutomationTarget target;
    EngineAutomationPoint* points;
    int point_count;
    int point_capacity;
    EngineAutomationDomain domain;
    EngineAutomationBoundary before;
    EngineAutomationBoundary after;
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
// Returns true when the target controls a MIDI instrument parameter.
bool engine_automation_target_is_instrument_param(EngineAutomationTarget target);
// Returns a compact timeline label for the target.
const char* engine_automation_target_display_label(EngineAutomationTarget target);

// Validates domain, endpoint policy, finite values, curve kinds and strictly ordered frame bounds.
bool engine_automation_lane_validate(const EngineAutomationLane* lane, uint64_t region_frames);
// Returns the neutral value in the lane's declared units.
float engine_automation_lane_baseline(const EngineAutomationLane* lane);
// Converts an authored value to the legacy delta consumed by playback.
float engine_automation_value_to_delta(const EngineAutomationLane* lane, float value);
// Evaluates the authored curve without converting its declared units.
float engine_automation_lane_eval_value(const EngineAutomationLane* lane, uint64_t frame, uint64_t region_frames);
// Returns a stable unit label for physical or legacy lane values.
const char* engine_automation_lane_unit(const EngineAutomationLane* lane);
