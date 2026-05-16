#pragma once

#include "engine/automation.h"
#include "engine/graph.h"
#include "engine/midi.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct EngineInstrumentSource EngineInstrumentSource;

typedef enum {
    ENGINE_INSTRUMENT_PRESET_PURE_SINE = 0,
    ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE,
    ENGINE_INSTRUMENT_PRESET_SAW_LEAD,
    ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS,
    ENGINE_INSTRUMENT_PRESET_SYNTH_LAB,
    ENGINE_INSTRUMENT_PRESET_SOFT_PAD,
    ENGINE_INSTRUMENT_PRESET_PLUCK,
    ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD,
    ENGINE_INSTRUMENT_PRESET_WARM_KEYS,
    ENGINE_INSTRUMENT_PRESET_SUB_DRONE,
    ENGINE_INSTRUMENT_PRESET_COUNT
} EngineInstrumentPresetId;

typedef enum {
    ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC = 0,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT
} EngineInstrumentPresetCategoryId;

typedef enum {
    ENGINE_INSTRUMENT_PARAM_LEVEL = 0,
    ENGINE_INSTRUMENT_PARAM_TONE,
    ENGINE_INSTRUMENT_PARAM_ATTACK_MS,
    ENGINE_INSTRUMENT_PARAM_RELEASE_MS,
    ENGINE_INSTRUMENT_PARAM_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_SUSTAIN,
    ENGINE_INSTRUMENT_PARAM_OSC_MIX,
    ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE,
    ENGINE_INSTRUMENT_PARAM_SUB_MIX,
    ENGINE_INSTRUMENT_PARAM_DRIVE,
    ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE,
    ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH,
    ENGINE_INSTRUMENT_PARAM_COUNT
} EngineInstrumentParamId;

typedef enum {
    ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT = 0,
    ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR,
    ENGINE_INSTRUMENT_PARAM_GROUP_TONE,
    ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE,
    ENGINE_INSTRUMENT_PARAM_GROUP_MOD,
    ENGINE_INSTRUMENT_PARAM_GROUP_COUNT
} EngineInstrumentParamGroupId;

typedef struct {
    const char* id;
    const char* display_name;
    const char* unit;
    float min_value;
    float max_value;
    float default_value;
    EngineInstrumentParamGroupId group;
    int display_order;
} EngineInstrumentParamSpec;

typedef struct {
    float level;
    float tone;
    float attack_ms;
    float release_ms;
    float decay_ms;
    float sustain;
    float osc_mix;
    float osc2_detune;
    float sub_mix;
    float drive;
    float vibrato_rate;
    float vibrato_depth;
} EngineInstrumentParams;

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_display_name(EngineInstrumentPresetId preset);
EngineInstrumentPresetCategoryId engine_instrument_preset_category(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_category_display_name(EngineInstrumentPresetCategoryId category);
bool engine_instrument_preset_from_id_string(const char* id, EngineInstrumentPresetId* out_preset);
int engine_instrument_preset_count(void);
int engine_instrument_preset_category_count(void);
int engine_instrument_param_count(void);
int engine_instrument_param_group_count(void);
const char* engine_instrument_param_group_display_name(EngineInstrumentParamGroupId group);
bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec);
bool engine_instrument_param_from_id_string(const char* id, EngineInstrumentParamId* out_param);
int engine_instrument_preset_param_count(EngineInstrumentPresetId preset);
bool engine_instrument_preset_param_id_at(EngineInstrumentPresetId preset,
                                          int index,
                                          EngineInstrumentParamId* out_param);
EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset);
EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params);
float engine_instrument_params_get(EngineInstrumentParams params, EngineInstrumentParamId param);
EngineInstrumentParams engine_instrument_params_set(EngineInstrumentPresetId preset,
                                                    EngineInstrumentParams params,
                                                    EngineInstrumentParamId param,
                                                    float value);
bool engine_instrument_param_from_automation_target(EngineAutomationTarget target,
                                                    EngineInstrumentParamId* out_param);
EngineAutomationTarget engine_instrument_param_automation_target(EngineInstrumentParamId param);
EngineInstrumentParams engine_instrument_params_apply_automation_delta(EngineInstrumentPresetId preset,
                                                                       EngineInstrumentParams params,
                                                                       EngineInstrumentParamId param,
                                                                       float delta);
EngineInstrumentSource* engine_instrument_source_create(void);
void engine_instrument_source_destroy(EngineInstrumentSource* instrument);
bool engine_instrument_source_set_midi_clip(EngineInstrumentSource* instrument,
                                            uint64_t timeline_start_frame,
                                            uint64_t clip_duration_frames,
                                            EngineInstrumentPresetId preset,
                                            EngineInstrumentParams params,
                                            const EngineMidiNote* notes,
                                            int note_count,
                                            const EngineAutomationLane* track_automation_lanes,
                                            int track_automation_lane_count,
                                            const EngineAutomationLane* automation_lanes,
                                            int automation_lane_count);
void engine_instrument_source_reset(void* userdata, int sample_rate, int channels);
void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_instrument_source_ops(EngineGraphSourceOps* ops);
