#pragma once

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
    ENGINE_INSTRUMENT_PRESET_COUNT
} EngineInstrumentPresetId;

typedef enum {
    ENGINE_INSTRUMENT_PARAM_LEVEL = 0,
    ENGINE_INSTRUMENT_PARAM_TONE,
    ENGINE_INSTRUMENT_PARAM_ATTACK_MS,
    ENGINE_INSTRUMENT_PARAM_RELEASE_MS,
    ENGINE_INSTRUMENT_PARAM_COUNT
} EngineInstrumentParamId;

typedef struct {
    const char* id;
    const char* display_name;
    const char* unit;
    float min_value;
    float max_value;
    float default_value;
} EngineInstrumentParamSpec;

typedef struct {
    float level;
    float tone;
    float attack_ms;
    float release_ms;
} EngineInstrumentParams;

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_display_name(EngineInstrumentPresetId preset);
bool engine_instrument_preset_from_id_string(const char* id, EngineInstrumentPresetId* out_preset);
int engine_instrument_preset_count(void);
int engine_instrument_param_count(void);
bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec);
EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset);
EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params);
float engine_instrument_params_get(EngineInstrumentParams params, EngineInstrumentParamId param);
EngineInstrumentParams engine_instrument_params_set(EngineInstrumentPresetId preset,
                                                    EngineInstrumentParams params,
                                                    EngineInstrumentParamId param,
                                                    float value);
EngineInstrumentSource* engine_instrument_source_create(void);
void engine_instrument_source_destroy(EngineInstrumentSource* instrument);
bool engine_instrument_source_set_midi_clip(EngineInstrumentSource* instrument,
                                            uint64_t timeline_start_frame,
                                            uint64_t clip_duration_frames,
                                            EngineInstrumentPresetId preset,
                                            EngineInstrumentParams params,
                                            const EngineMidiNote* notes,
                                            int note_count);
void engine_instrument_source_reset(void* userdata, int sample_rate, int channels);
void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_instrument_source_ops(EngineGraphSourceOps* ops);
