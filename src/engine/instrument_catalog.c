#include "engine/instrument.h"
#include "engine/instrument_filter.h"
#include <math.h>
#include <string.h>

// Keeps stable preset identities, defaults and physical parameter metadata outside voice rendering.
EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset) {
    if (preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return preset;
}

const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SWEEP_BASS: return "sweep_bass";
    case ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK: return "filter_pluck";
    case ENGINE_INSTRUMENT_PRESET_MOVING_PAD: return "moving_pad";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
        return "rounded_keys";
    case ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS:
        return "filtered_keys";
    case ENGINE_INSTRUMENT_PRESET_FILTERED_PAD:
        return "filtered_pad";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
        return "rounded_pluck";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD:
        return "rounded_pad";
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
    case ENGINE_INSTRUMENT_PRESET_SWEEP_BASS: return "Sweep Bass";
    case ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK: return "Filter Pluck";
    case ENGINE_INSTRUMENT_PRESET_MOVING_PAD: return "Moving Pad";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
        return "Rounded Keys";
    case ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS:
        return "Filtered Keys";
    case ENGINE_INSTRUMENT_PRESET_FILTERED_PAD:
        return "Filtered Pad";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
        return "Rounded Pluck";
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD:
        return "Rounded Pad";
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
    case ENGINE_INSTRUMENT_PRESET_SWEEP_BASS: return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS;
    case ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK: return ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK;
    case ENGINE_INSTRUMENT_PRESET_MOVING_PAD: return ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS;
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS;
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD;
    case ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS;
    case ENGINE_INSTRUMENT_PRESET_FILTERED_PAD:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD:
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS;
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
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
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        *out_spec = (EngineInstrumentParamSpec){"tone", "Tone", "", 0.0f, 1.0f, 0.5f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        *out_spec = (EngineInstrumentParamSpec){"attack_ms", "Attack", "ms", 0.0f, 250.0f, 4.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME};
        return true;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        *out_spec = (EngineInstrumentParamSpec){"release_ms", "Release", "ms", 0.0f, 500.0f, 18.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 3, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        *out_spec = (EngineInstrumentParamSpec){"decay_ms", "Decay", "ms", 0.0f, 800.0f, 120.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 1, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        *out_spec = (EngineInstrumentParamSpec){"sustain", "Sustain", "", 0.0f, 1.0f, 0.78f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 2, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        *out_spec = (EngineInstrumentParamSpec){"osc_mix", "Osc Mix", "", 0.0f, 1.0f, 0.45f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        *out_spec = (EngineInstrumentParamSpec){"osc2_detune", "Detune", "ct", -24.0f, 24.0f, 7.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 1, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        *out_spec = (EngineInstrumentParamSpec){"sub_mix", "Sub", "", 0.0f, 1.0f, 0.22f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 2, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        *out_spec = (EngineInstrumentParamSpec){"drive", "Drive", "", 0.0f, 1.0f, 0.18f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 1, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_rate", "Rate", "Hz", 0.0f, 12.0f, 5.2f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_depth", "Depth", "ct", 0.0f, 60.0f, 0.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 1, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ:
        *out_spec = (EngineInstrumentParamSpec){"cutoff_hz", "Cutoff", "Hz", 80, 18000, 12000,
            ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 2, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_FREQUENCY};
        return true;
    case ENGINE_INSTRUMENT_PARAM_RESONANCE_Q:
        *out_spec = (EngineInstrumentParamSpec){"resonance_q", "Resonance", "Q", .5f, 2, .707f,
            ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 3, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_FILTER_ENV_OCTAVES:
        *out_spec = (EngineInstrumentParamSpec){"filter_env_octaves", "Filter Env", "oct", -4, 4, 0, ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 0, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
        return true;
    case ENGINE_INSTRUMENT_PARAM_FILTER_DECAY_MS:
        *out_spec = (EngineInstrumentParamSpec){"filter_decay_ms", "Filter Decay", "ms", 5, 2000, 250, ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 1, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME};
        return true;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_HZ:
        *out_spec = (EngineInstrumentParamSpec){"filter_lfo_hz", "Filter LFO", "Hz", 0.05, 12, 2, ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 2, ENGINE_INSTRUMENT_PARAM_SCALE_LOG_FREQUENCY};
        return true;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_OCTAVES:
        *out_spec = (EngineInstrumentParamSpec){"filter_lfo_octaves", "LFO Depth", "oct", 0, 3, 0, ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 3, ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR};
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
    if (preset == ENGINE_INSTRUMENT_PRESET_SWEEP_BASS || preset == ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK || preset == ENGINE_INSTRUMENT_PRESET_MOVING_PAD) {
        EngineInstrumentParams p = engine_instrument_default_params(preset == ENGINE_INSTRUMENT_PRESET_MOVING_PAD ? ENGINE_INSTRUMENT_PRESET_FILTERED_PAD : ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS);
        if (preset == ENGINE_INSTRUMENT_PRESET_SWEEP_BASS) {
            p.level=.8f; p.cutoff_hz=220; p.resonance_q=1.1f; p.sub_mix=.4f; p.attack_ms=3; p.decay_ms=220; p.sustain=.55f;
            p.filter_env_octaves=3; p.filter_decay_ms=180;
        } else if (preset == ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK) {
            p.level=.8f; p.cutoff_hz=700; p.resonance_q=.9f; p.attack_ms=2; p.decay_ms=240; p.sustain=.08f; p.release_ms=160;
            p.filter_env_octaves=3.2f; p.filter_decay_ms=100;
        } else {
            p.level=.6f; p.cutoff_hz=1200; p.resonance_q=.8f; p.filter_lfo_hz=.35f; p.filter_lfo_octaves=.9f;
        }
        return engine_instrument_params_sanitize(preset,p);
    }
    if (engine_instrument_filter_preset(preset)) {
        EngineInstrumentParams filtered = engine_instrument_default_params(preset == ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS
            ? ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS : ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD);
        filtered.tone = .8f;
        filtered.cutoff_hz = preset == ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS ? 1800 : 900;
        filtered.resonance_q = preset == ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS ? .8f : 1;
        return filtered;
    }
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
        .vibrato_depth = 0.0f, .cutoff_hz = 12000, .resonance_q = .707f
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
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
        params = (EngineInstrumentParams){.level = .85f, .tone = .32f, .attack_ms = 6,
            .decay_ms = 260, .sustain = .32f, .release_ms = 180, .osc_mix = .18f,
            .osc2_detune = 3, .sub_mix = .06f, .drive = .08f, .vibrato_rate = 5.2f, .cutoff_hz = 12000, .resonance_q = .707f};
        break;
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
        params = (EngineInstrumentParams){.level = .9f, .tone = .8f, .attack_ms = 2,
            .decay_ms = 150, .sustain = .08f, .release_ms = 90, .osc_mix = .08f,
            .osc2_detune = -2, .sub_mix = 0, .drive = .12f, .vibrato_rate = 5.2f, .cutoff_hz = 12000, .resonance_q = .707f};
        break;
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD:
        params = (EngineInstrumentParams){.level = .7f, .tone = .45f, .attack_ms = 180,
            .decay_ms = 420, .sustain = .8f, .release_ms = 450, .osc_mix = .45f,
            .osc2_detune = 6, .sub_mix = .12f, .drive = .04f, .vibrato_rate = 5.2f, .cutoff_hz = 12000, .resonance_q = .707f};
        break;
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        break;
    }
    params.filter_decay_ms = 250; params.filter_lfo_hz = 2;
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
    params.cutoff_hz = instrument_clamp_float(params.cutoff_hz, 80, 18000);
    params.resonance_q = instrument_clamp_float(params.resonance_q, .5f, 2);
    params.filter_env_octaves = instrument_clamp_float(params.filter_env_octaves, -4, 4);
    params.filter_decay_ms = instrument_clamp_float(params.filter_decay_ms, 5, 2000);
    params.filter_lfo_hz = instrument_clamp_float(params.filter_lfo_hz, 0.05, 12);
    params.filter_lfo_octaves = instrument_clamp_float(params.filter_lfo_octaves, 0, 3);

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
    case ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ:
        return params.cutoff_hz;
    case ENGINE_INSTRUMENT_PARAM_RESONANCE_Q:
        return params.resonance_q;
    case ENGINE_INSTRUMENT_PARAM_FILTER_ENV_OCTAVES:
        return params.filter_env_octaves;
    case ENGINE_INSTRUMENT_PARAM_FILTER_DECAY_MS:
        return params.filter_decay_ms;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_HZ:
        return params.filter_lfo_hz;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_OCTAVES:
        return params.filter_lfo_octaves;
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
    case ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ:
        params.cutoff_hz = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_RESONANCE_Q:
        params.resonance_q = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_FILTER_ENV_OCTAVES:
        params.filter_env_octaves = value; break;
    case ENGINE_INSTRUMENT_PARAM_FILTER_DECAY_MS:
        params.filter_decay_ms = value; break;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_HZ:
        params.filter_lfo_hz = value; break;
    case ENGINE_INSTRUMENT_PARAM_FILTER_LFO_OCTAVES:
        params.filter_lfo_octaves = value; break;
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
