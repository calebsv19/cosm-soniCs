#include "engine/instrument.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct EngineInstrumentSource {
    uint64_t timeline_start_frame;
    uint64_t clip_duration_frames;
    EngineMidiNote* notes;
    int note_count;
    int note_capacity;
    EngineAutomationLane* track_automation_lanes;
    int track_automation_lane_count;
    int track_automation_lane_capacity;
    EngineAutomationLane* automation_lanes;
    int automation_lane_count;
    int automation_lane_capacity;
    EngineInstrumentPresetId preset;
    EngineInstrumentParams params;
    int sample_rate;
    int channels;
};

static void instrument_reset_internal(EngineInstrumentSource* instrument, int sample_rate, int channels) {
    if (!instrument) {
        return;
    }
    instrument->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    instrument->channels = channels > 0 ? channels : 2;
}

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset) {
    if (preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return preset;
}

const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        return "synth_lab";
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return "soft_pad";
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return "pluck";
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return "bright_lead";
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return "warm_keys";
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return "sub_drone";
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return "soft_square";
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        return "saw_lead";
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        return "simple_bass";
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return "pure_sine";
    }
}

const char* engine_instrument_preset_display_name(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        return "Custom Synth";
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return "Soft Pad";
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return "Pluck";
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return "Bright Lead";
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return "Warm Keys";
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return "Sub Drone";
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return "Soft Square";
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        return "Saw Lead";
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        return "Simple Bass";
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return "Pure Sine";
    }
}

EngineInstrumentPresetCategoryId engine_instrument_preset_category(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS;
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD;
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS;
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS;
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK;
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC;
    }
}

const char* engine_instrument_preset_category_display_name(EngineInstrumentPresetCategoryId category) {
    switch (category) {
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC:
        return "Basic";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS:
        return "Bass";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD:
        return "Lead";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS:
        return "Keys";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS:
        return "Pads";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK:
        return "Pluck";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT:
    default:
        return "";
    }
}

bool engine_instrument_preset_from_id_string(const char* id, EngineInstrumentPresetId* out_preset) {
    if (!id || !out_preset) {
        return false;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        if (strcmp(id, engine_instrument_preset_id_string(preset)) == 0) {
            *out_preset = preset;
            return true;
        }
    }
    return false;
}

int engine_instrument_preset_count(void) {
    return ENGINE_INSTRUMENT_PRESET_COUNT;
}

int engine_instrument_preset_category_count(void) {
    return ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
}

int engine_instrument_param_count(void) {
    return ENGINE_INSTRUMENT_PARAM_COUNT;
}

int engine_instrument_param_group_count(void) {
    return ENGINE_INSTRUMENT_PARAM_GROUP_COUNT;
}

const char* engine_instrument_param_group_display_name(EngineInstrumentParamGroupId group) {
    switch (group) {
    case ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT:
        return "Output";
    case ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR:
        return "Osc";
    case ENGINE_INSTRUMENT_PARAM_GROUP_TONE:
        return "Tone";
    case ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE:
        return "Env";
    case ENGINE_INSTRUMENT_PARAM_GROUP_MOD:
        return "Mod";
    case ENGINE_INSTRUMENT_PARAM_GROUP_COUNT:
    default:
        return "";
    }
}

bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec) {
    if (!out_spec) {
        return false;
    }
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        *out_spec = (EngineInstrumentParamSpec){"level", "Level", "", 0.0f, 1.5f, 1.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        *out_spec = (EngineInstrumentParamSpec){"tone", "Tone", "", 0.0f, 1.0f, 0.5f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        *out_spec = (EngineInstrumentParamSpec){"attack_ms", "Attack", "ms", 0.0f, 250.0f, 4.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        *out_spec = (EngineInstrumentParamSpec){"release_ms", "Release", "ms", 0.0f, 500.0f, 18.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 3};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        *out_spec = (EngineInstrumentParamSpec){"decay_ms", "Decay", "ms", 0.0f, 800.0f, 120.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        *out_spec = (EngineInstrumentParamSpec){"sustain", "Sustain", "", 0.0f, 1.0f, 0.78f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 2};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        *out_spec = (EngineInstrumentParamSpec){"osc_mix", "Osc Mix", "", 0.0f, 1.0f, 0.45f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        *out_spec = (EngineInstrumentParamSpec){"osc2_detune", "Detune", "ct", -24.0f, 24.0f, 7.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        *out_spec = (EngineInstrumentParamSpec){"sub_mix", "Sub", "", 0.0f, 1.0f, 0.22f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 2};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        *out_spec = (EngineInstrumentParamSpec){"drive", "Drive", "", 0.0f, 1.0f, 0.18f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_rate", "Rate", "Hz", 0.0f, 12.0f, 5.2f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_depth", "Depth", "ct", 0.0f, 60.0f, 0.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        return false;
    }
}

bool engine_instrument_param_from_id_string(const char* id, EngineInstrumentParamId* out_param) {
    if (!id || !out_param) {
        return false;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec = {0};
        if (engine_instrument_param_spec((EngineInstrumentParamId)i, &spec) &&
            spec.id && strcmp(id, spec.id) == 0) {
            *out_param = (EngineInstrumentParamId)i;
            return true;
        }
    }
    return false;
}

int engine_instrument_preset_param_count(EngineInstrumentPresetId preset) {
    (void)preset;
    return ENGINE_INSTRUMENT_PARAM_COUNT;
}

bool engine_instrument_preset_param_id_at(EngineInstrumentPresetId preset,
                                          int index,
                                          EngineInstrumentParamId* out_param) {
    (void)preset;
    if (!out_param || index < 0 || index >= ENGINE_INSTRUMENT_PARAM_COUNT) {
        return false;
    }
    *out_param = (EngineInstrumentParamId)index;
    return true;
}

static float instrument_clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset) {
    EngineInstrumentParams params = {
        .level = 1.0f,
        .tone = 0.5f,
        .attack_ms = 4.0f,
        .release_ms = 18.0f,
        .decay_ms = 120.0f,
        .sustain = 1.0f,
        .osc_mix = 0.0f,
        .osc2_detune = 0.0f,
        .sub_mix = 0.0f,
        .drive = 0.0f,
        .vibrato_rate = 5.2f,
        .vibrato_depth = 0.0f
    };
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        params.tone = 0.62f;
        params.attack_ms = 3.0f;
        params.release_ms = 24.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        params.level = 0.72f;
        params.tone = 0.64f;
        params.attack_ms = 4.0f;
        params.release_ms = 60.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        params.level = 0.86f;
        params.tone = 0.30f;
        params.attack_ms = 8.0f;
        params.release_ms = 100.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        params.level = 0.72f;
        params.tone = 0.54f;
        params.attack_ms = 8.0f;
        params.decay_ms = 180.0f;
        params.sustain = 0.68f;
        params.release_ms = 140.0f;
        params.osc_mix = 0.42f;
        params.osc2_detune = 5.0f;
        params.sub_mix = 0.16f;
        params.drive = 0.12f;
        params.vibrato_rate = 5.2f;
        params.vibrato_depth = 4.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        params.level = 0.62f;
        params.tone = 0.44f;
        params.attack_ms = 160.0f;
        params.decay_ms = 420.0f;
        params.sustain = 0.86f;
        params.release_ms = 420.0f;
        params.osc_mix = 0.22f;
        params.osc2_detune = 4.0f;
        params.sub_mix = 0.10f;
        params.drive = 0.02f;
        params.vibrato_rate = 4.1f;
        params.vibrato_depth = 3.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        params.level = 0.72f;
        params.tone = 0.58f;
        params.attack_ms = 2.0f;
        params.decay_ms = 150.0f;
        params.sustain = 0.14f;
        params.release_ms = 110.0f;
        params.osc_mix = 0.28f;
        params.osc2_detune = 2.0f;
        params.sub_mix = 0.0f;
        params.drive = 0.04f;
        params.vibrato_rate = 5.2f;
        params.vibrato_depth = 0.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        params.level = 0.64f;
        params.tone = 0.72f;
        params.attack_ms = 3.0f;
        params.decay_ms = 150.0f;
        params.sustain = 0.66f;
        params.release_ms = 150.0f;
        params.osc_mix = 0.64f;
        params.osc2_detune = 7.0f;
        params.sub_mix = 0.04f;
        params.drive = 0.14f;
        params.vibrato_rate = 5.8f;
        params.vibrato_depth = 5.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        params.level = 0.70f;
        params.tone = 0.38f;
        params.attack_ms = 12.0f;
        params.decay_ms = 240.0f;
        params.sustain = 0.58f;
        params.release_ms = 220.0f;
        params.osc_mix = 0.14f;
        params.osc2_detune = -3.0f;
        params.sub_mix = 0.08f;
        params.drive = 0.03f;
        params.vibrato_rate = 4.8f;
        params.vibrato_depth = 1.5f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        params.level = 0.60f;
        params.tone = 0.22f;
        params.attack_ms = 120.0f;
        params.decay_ms = 520.0f;
        params.sustain = 0.92f;
        params.release_ms = 480.0f;
        params.osc_mix = 0.10f;
        params.osc2_detune = -5.0f;
        params.sub_mix = 0.70f;
        params.drive = 0.06f;
        params.vibrato_rate = 2.2f;
        params.vibrato_depth = 2.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        break;
    }
    return engine_instrument_params_sanitize(preset, params);
}

EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params) {
    (void)preset;
    EngineInstrumentParamSpec spec = {0};
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_LEVEL, &spec)) {
        params.level = instrument_clamp_float(params.level, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_TONE, &spec)) {
        params.tone = instrument_clamp_float(params.tone, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_ATTACK_MS, &spec)) {
        params.attack_ms = instrument_clamp_float(params.attack_ms, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_RELEASE_MS, &spec)) {
        params.release_ms = instrument_clamp_float(params.release_ms, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_DECAY_MS, &spec)) {
        params.decay_ms = instrument_clamp_float(params.decay_ms, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_SUSTAIN, &spec)) {
        params.sustain = instrument_clamp_float(params.sustain, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_OSC_MIX, &spec)) {
        params.osc_mix = instrument_clamp_float(params.osc_mix, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE, &spec)) {
        params.osc2_detune = instrument_clamp_float(params.osc2_detune, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_SUB_MIX, &spec)) {
        params.sub_mix = instrument_clamp_float(params.sub_mix, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_DRIVE, &spec)) {
        params.drive = instrument_clamp_float(params.drive, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE, &spec)) {
        params.vibrato_rate = instrument_clamp_float(params.vibrato_rate, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH, &spec)) {
        params.vibrato_depth = instrument_clamp_float(params.vibrato_depth, spec.min_value, spec.max_value);
    }
    return params;
}

float engine_instrument_params_get(EngineInstrumentParams params, EngineInstrumentParamId param) {
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        return params.level;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        return params.tone;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        return params.attack_ms;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        return params.release_ms;
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        return params.decay_ms;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        return params.sustain;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        return params.osc_mix;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        return params.osc2_detune;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        return params.sub_mix;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        return params.drive;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        return params.vibrato_rate;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        return params.vibrato_depth;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        return 0.0f;
    }
}

EngineInstrumentParams engine_instrument_params_set(EngineInstrumentPresetId preset,
                                                    EngineInstrumentParams params,
                                                    EngineInstrumentParamId param,
                                                    float value) {
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        params.level = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        params.tone = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        params.attack_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        params.release_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        params.decay_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        params.sustain = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        params.osc_mix = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        params.osc2_detune = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        params.sub_mix = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        params.drive = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        params.vibrato_rate = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        params.vibrato_depth = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        break;
    }
    return engine_instrument_params_sanitize(preset, params);
}

bool engine_instrument_param_from_automation_target(EngineAutomationTarget target,
                                                    EngineInstrumentParamId* out_param) {
    if (!out_param || !engine_automation_target_is_instrument_param(target)) {
        return false;
    }
    int param = (int)target - (int)ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL;
    if (param < 0 || param >= ENGINE_INSTRUMENT_PARAM_COUNT) {
        return false;
    }
    *out_param = (EngineInstrumentParamId)param;
    return true;
}

EngineAutomationTarget engine_instrument_param_automation_target(EngineInstrumentParamId param) {
    if (param < 0 || param >= ENGINE_INSTRUMENT_PARAM_COUNT) {
        return ENGINE_AUTOMATION_TARGET_COUNT;
    }
    return (EngineAutomationTarget)((int)ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL + (int)param);
}

EngineInstrumentParams engine_instrument_params_apply_automation_delta(EngineInstrumentPresetId preset,
                                                                       EngineInstrumentParams params,
                                                                       EngineInstrumentParamId param,
                                                                       float delta) {
    EngineInstrumentParamSpec spec = {0};
    if (!engine_instrument_param_spec(param, &spec)) {
        return engine_instrument_params_sanitize(preset, params);
    }
    float range = spec.max_value - spec.min_value;
    if (range <= 0.0f) {
        return engine_instrument_params_sanitize(preset, params);
    }
    float current = engine_instrument_params_get(params, param);
    float normalized = (current - spec.min_value) / range;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    if (delta < -1.0f) delta = -1.0f;
    if (delta > 1.0f) delta = 1.0f;
    normalized += delta * 0.5f;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return engine_instrument_params_set(preset, params, param, spec.min_value + normalized * range);
}

static void instrument_clear_automation_lanes(EngineAutomationLane** lanes, int* lane_count, int* lane_capacity) {
    if (!lanes || !*lanes) {
        return;
    }
    int count = lane_count ? *lane_count : 0;
    for (int i = 0; i < count; ++i) {
        engine_automation_lane_free(&(*lanes)[i]);
    }
    free(*lanes);
    *lanes = NULL;
    if (lane_count) {
        *lane_count = 0;
    }
    if (lane_capacity) {
        *lane_capacity = 0;
    }
}

static bool instrument_copy_automation_lanes(EngineAutomationLane** dst_lanes,
                                             int* dst_lane_count,
                                             int* dst_lane_capacity,
                                             const EngineAutomationLane* lanes,
                                             int lane_count) {
    if (!dst_lanes || !dst_lane_count || !dst_lane_capacity) {
        return false;
    }
    instrument_clear_automation_lanes(dst_lanes, dst_lane_count, dst_lane_capacity);
    if (!lanes || lane_count <= 0) {
        return true;
    }
    *dst_lanes = (EngineAutomationLane*)calloc((size_t)lane_count, sizeof(EngineAutomationLane));
    if (!*dst_lanes) {
        return false;
    }
    *dst_lane_capacity = lane_count;
    *dst_lane_count = lane_count;
    for (int i = 0; i < lane_count; ++i) {
        engine_automation_lane_init(&(*dst_lanes)[i], lanes[i].target);
        if (!engine_automation_lane_copy(&lanes[i], &(*dst_lanes)[i])) {
            instrument_clear_automation_lanes(dst_lanes, dst_lane_count, dst_lane_capacity);
            return false;
        }
    }
    return true;
}

EngineInstrumentSource* engine_instrument_source_create(void) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)calloc(1, sizeof(EngineInstrumentSource));
    if (!instrument) {
        return NULL;
    }
    instrument->preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    instrument->params = engine_instrument_default_params(instrument->preset);
    instrument_reset_internal(instrument, 48000, 2);
    return instrument;
}

void engine_instrument_source_destroy(EngineInstrumentSource* instrument) {
    if (!instrument) {
        return;
    }
    free(instrument->notes);
    instrument->notes = NULL;
    instrument->note_count = 0;
    instrument->note_capacity = 0;
    instrument_clear_automation_lanes(&instrument->track_automation_lanes,
                                      &instrument->track_automation_lane_count,
                                      &instrument->track_automation_lane_capacity);
    instrument_clear_automation_lanes(&instrument->automation_lanes,
                                      &instrument->automation_lane_count,
                                      &instrument->automation_lane_capacity);
    free(instrument);
}

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
                                            int automation_lane_count) {
    if (!instrument || note_count < 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (note_count > 0 && !notes) {
        return false;
    }
    if (note_count > instrument->note_capacity) {
        EngineMidiNote* resized = (EngineMidiNote*)realloc(instrument->notes,
                                                           sizeof(EngineMidiNote) * (size_t)note_count);
        if (!resized) {
            return false;
        }
        instrument->notes = resized;
        instrument->note_capacity = note_count;
    }
    if (note_count > 0) {
        memcpy(instrument->notes, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    }
    instrument->note_count = note_count;
    instrument->timeline_start_frame = timeline_start_frame;
    instrument->clip_duration_frames = clip_duration_frames;
    instrument->preset = engine_instrument_preset_clamp(preset);
    instrument->params = engine_instrument_params_sanitize(instrument->preset, params);
    if (!instrument_copy_automation_lanes(&instrument->track_automation_lanes,
                                          &instrument->track_automation_lane_count,
                                          &instrument->track_automation_lane_capacity,
                                          track_automation_lanes,
                                          track_automation_lane_count)) {
        return false;
    }
    return instrument_copy_automation_lanes(&instrument->automation_lanes,
                                            &instrument->automation_lane_count,
                                            &instrument->automation_lane_capacity,
                                            automation_lanes,
                                            automation_lane_count);
}

void engine_instrument_source_reset(void* userdata, int sample_rate, int channels) {
    instrument_reset_internal((EngineInstrumentSource*)userdata, sample_rate, channels);
}

static double midi_note_frequency(uint8_t note) {
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

static uint64_t instrument_ms_to_frames(float ms, int sample_rate) {
    if (ms <= 0.0f || sample_rate <= 0) {
        return 0;
    }
    double frames = ((double)ms * (double)sample_rate) / 1000.0;
    if (frames < 1.0) {
        return 1;
    }
    if (frames > (double)UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t)(frames + 0.5);
}

static float note_envelope(uint64_t rel,
                           uint64_t duration,
                           const EngineInstrumentParams* params,
                           int sample_rate) {
    if (duration == 0 || rel >= duration) {
        return 0.0f;
    }
    uint64_t attack = params ? instrument_ms_to_frames(params->attack_ms, sample_rate) : 64;
    uint64_t decay = params ? instrument_ms_to_frames(params->decay_ms, sample_rate) : 0;
    uint64_t release = params ? instrument_ms_to_frames(params->release_ms, sample_rate) : 64;
    if (attack > 0 && rel < attack) {
        return (float)rel / (float)attack;
    }
    float sustain = params ? params->sustain : 1.0f;
    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;
    uint64_t decay_start = attack;
    if (decay > 0 && rel < decay_start + decay && rel >= decay_start) {
        float t = (float)(rel - decay_start) / (float)decay;
        return 1.0f + (sustain - 1.0f) * t;
    }
    uint64_t remaining = duration - rel;
    float scale = sustain;
    if (release > 0 && remaining < release) {
        scale *= (float)remaining / (float)release;
    }
    return scale;
}

static float preset_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params, double phase, double freq) {
    double sine = sin(phase);
    double tone = params.tone;
    if (tone < 0.0) tone = 0.0;
    if (tone > 1.0) tone = 1.0;
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return (float)(tanh(sine * (1.2 + tone * 3.4)) * 0.16);
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD: {
        double cycle = phase / (2.0 * M_PI);
        double saw = 2.0 * (cycle - floor(cycle + 0.5));
        return (float)(saw * (0.08 + tone * 0.10) + sine * (0.06 - tone * 0.03));
    }
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS: {
        double sub_phase = phase * 0.5;
        double second = sin(phase * 2.0);
        (void)freq;
        return (float)(sin(sub_phase) * 0.22 + second * (0.02 + tone * 0.08));
    }
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB: {
        double detune_ratio = pow(2.0, (double)params.osc2_detune / 1200.0);
        double phase2 = phase * detune_ratio;
        double osc1 = sine * (0.14 + tone * 0.05);
        double cycle2 = phase2 / (2.0 * M_PI);
        double saw2 = 2.0 * (cycle2 - floor(cycle2 + 0.5));
        double osc_mix = params.osc_mix;
        if (osc_mix < 0.0) osc_mix = 0.0;
        if (osc_mix > 1.0) osc_mix = 1.0;
        double sub_mix = params.sub_mix;
        if (sub_mix < 0.0) sub_mix = 0.0;
        if (sub_mix > 1.0) sub_mix = 1.0;
        double sub = sin(phase * 0.5) * 0.18 * sub_mix;
        double blend = osc1 * (1.0 - osc_mix) + saw2 * (0.10 + tone * 0.08) * osc_mix + sub;
        double drive = 1.0 + params.drive * 5.0;
        return (float)(tanh(blend * drive) / (1.0 + params.drive * 0.8));
    }
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD: {
        double detune_ratio = pow(2.0, (double)params.osc2_detune / 1200.0);
        double phase2 = phase * detune_ratio;
        double soft = tanh(sine * (1.0 + tone * 1.8)) * 0.10;
        double second = sin(phase2) * 0.08 * params.osc_mix;
        double sub = sin(phase * 0.5) * 0.10 * params.sub_mix;
        return (float)(soft + second + sub);
    }
    case ENGINE_INSTRUMENT_PRESET_PLUCK: {
        double cycle = phase / (2.0 * M_PI);
        double triangle = 2.0 * fabs(2.0 * (cycle - floor(cycle + 0.5))) - 1.0;
        double bright = triangle * (0.08 + tone * 0.08);
        double body = sine * 0.09 * (1.0 - params.osc_mix * 0.5);
        double drive = 1.0 + params.drive * 3.0;
        return (float)(tanh((bright + body) * drive) / (1.0 + params.drive * 0.5));
    }
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD: {
        double cycle = phase / (2.0 * M_PI);
        double saw = 2.0 * (cycle - floor(cycle + 0.5));
        double pulse = tanh(sine * (2.0 + tone * 5.0)) * 0.07;
        double drive = 1.0 + params.drive * 4.5;
        return (float)(tanh((saw * (0.10 + tone * 0.08) + pulse) * drive) /
                       (1.0 + params.drive * 0.6));
    }
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS: {
        double harmonic = sin(phase * 2.0) * (0.03 + tone * 0.05);
        double rounded = tanh(sine * (1.0 + tone * 1.4)) * 0.13;
        double sub = sin(phase * 0.5) * 0.06 * params.sub_mix;
        return (float)(rounded + harmonic + sub);
    }
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE: {
        double sub = sin(phase * 0.5) * (0.18 + params.sub_mix * 0.08);
        double body = sine * (0.04 + tone * 0.04);
        double second = sin(phase * 0.25) * 0.05;
        double drive = 1.0 + params.drive * 3.0;
        (void)freq;
        return (float)(tanh((sub + body + second) * drive) / (1.0 + params.drive * 0.7));
    }
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        (void)freq;
        return (float)(sine * (0.16 + tone * 0.08));
    }
}

static EngineInstrumentParams instrument_params_at_frame(const EngineInstrumentSource* instrument,
                                                         uint64_t global_frame,
                                                         uint64_t clip_frame) {
    EngineInstrumentParams params = instrument->params;
    if (instrument->track_automation_lanes && instrument->track_automation_lane_count > 0) {
        for (int i = 0; i < instrument->track_automation_lane_count; ++i) {
            const EngineAutomationLane* lane = &instrument->track_automation_lanes[i];
            EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_COUNT;
            if (!engine_instrument_param_from_automation_target(lane->target, &param)) {
                continue;
            }
            float delta = engine_automation_lane_eval(lane, global_frame, UINT64_MAX);
            params = engine_instrument_params_apply_automation_delta(instrument->preset, params, param, delta);
        }
    }
    if (!instrument->automation_lanes || instrument->automation_lane_count <= 0) {
        return engine_instrument_params_sanitize(instrument->preset, params);
    }
    for (int i = 0; i < instrument->automation_lane_count; ++i) {
        const EngineAutomationLane* lane = &instrument->automation_lanes[i];
        EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_COUNT;
        if (!engine_instrument_param_from_automation_target(lane->target, &param)) {
            continue;
        }
        float delta = engine_automation_lane_eval(lane, clip_frame, instrument->clip_duration_frames);
        params = engine_instrument_params_apply_automation_delta(instrument->preset, params, param, delta);
    }
    return engine_instrument_params_sanitize(instrument->preset, params);
}

void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)userdata;
    if (!instrument || !interleaved || frames <= 0 || instrument->sample_rate <= 0 || instrument->channels <= 0) {
        return;
    }

    const int channels = instrument->channels;
    for (int i = 0; i < frames; ++i) {
        uint64_t global_frame = transport_frame + (uint64_t)i;
        if (global_frame < instrument->timeline_start_frame) {
            continue;
        }
        uint64_t clip_frame = global_frame - instrument->timeline_start_frame;
        if (clip_frame >= instrument->clip_duration_frames) {
            continue;
        }

        EngineInstrumentParams frame_params = instrument_params_at_frame(instrument, global_frame, clip_frame);
        float sample = 0.0f;
        for (int n = 0; n < instrument->note_count; ++n) {
            const EngineMidiNote* note = &instrument->notes[n];
            if (clip_frame < note->start_frame) {
                continue;
            }
            uint64_t rel = clip_frame - note->start_frame;
            if (rel >= note->duration_frames) {
                continue;
            }
            double freq = midi_note_frequency(note->note);
            if (instrument->preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB &&
                frame_params.vibrato_depth > 0.0f &&
                frame_params.vibrato_rate > 0.0f) {
                double seconds = (double)rel / (double)instrument->sample_rate;
                double cents = sin(2.0 * M_PI * (double)frame_params.vibrato_rate * seconds) *
                               (double)frame_params.vibrato_depth;
                freq *= pow(2.0, cents / 1200.0);
            }
            double phase = (2.0 * M_PI * freq * (double)rel) / (double)instrument->sample_rate;
            float env = note_envelope(rel, note->duration_frames, &frame_params, instrument->sample_rate);
            sample += preset_sample(instrument->preset, frame_params, phase, freq) *
                      frame_params.level *
                      note->velocity *
                      env;
        }

        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        for (int ch = 0; ch < channels; ++ch) {
            interleaved[i * channels + ch] = sample;
        }
    }
}

void engine_instrument_source_ops(EngineGraphSourceOps* ops) {
    if (!ops) {
        return;
    }
    ops->render = engine_instrument_source_render;
    ops->reset = engine_instrument_source_reset;
}
