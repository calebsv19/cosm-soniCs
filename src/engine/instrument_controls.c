#include "engine/instrument.h"
#include <math.h>

// Keeps envelope controls in attack/decay/sustain/release order while retaining stable parameter IDs.
static const EngineInstrumentParamId control_order[] = {
    ENGINE_INSTRUMENT_PARAM_LEVEL, ENGINE_INSTRUMENT_PARAM_TONE,
    ENGINE_INSTRUMENT_PARAM_ATTACK_MS, ENGINE_INSTRUMENT_PARAM_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_SUSTAIN, ENGINE_INSTRUMENT_PARAM_RELEASE_MS,
    ENGINE_INSTRUMENT_PARAM_OSC_MIX, ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE, ENGINE_INSTRUMENT_PARAM_SUB_MIX,
    ENGINE_INSTRUMENT_PARAM_DRIVE, ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE, ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH
};

// Identifies only controls consumed by the current preset's voice and common ADSR/output stages.
bool engine_instrument_preset_param_active(EngineInstrumentPresetId preset, EngineInstrumentParamId param) {
    if (preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT || param < 0 || param >= ENGINE_INSTRUMENT_PARAM_COUNT) return false;
    if (param <= ENGINE_INSTRUMENT_PARAM_SUSTAIN || preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB) return true;
    switch (preset) {
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD:
        return param == ENGINE_INSTRUMENT_PARAM_OSC_MIX || param == ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE ||
            param == ENGINE_INSTRUMENT_PARAM_SUB_MIX || param == ENGINE_INSTRUMENT_PARAM_DRIVE;
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return param == ENGINE_INSTRUMENT_PARAM_OSC_MIX || param == ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE || param == ENGINE_INSTRUMENT_PARAM_SUB_MIX;
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return param == ENGINE_INSTRUMENT_PARAM_OSC_MIX || param == ENGINE_INSTRUMENT_PARAM_DRIVE;
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return param == ENGINE_INSTRUMENT_PARAM_DRIVE;
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return param == ENGINE_INSTRUMENT_PARAM_SUB_MIX;
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return param == ENGINE_INSTRUMENT_PARAM_SUB_MIX || param == ENGINE_INSTRUMENT_PARAM_DRIVE;
    default:
        return false;
    }
}

// Counts active controls without changing the serialized parameter schema.
int engine_instrument_preset_param_count(EngineInstrumentPresetId preset) {
    int count = 0;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (engine_instrument_preset_param_active(preset, control_order[i])) ++count;
    return count;
}

// Enumerates active controls in presentation order using their existing stable IDs.
bool engine_instrument_preset_param_id_at(EngineInstrumentPresetId preset, int index, EngineInstrumentParamId* out_param) {
    if (!out_param || index < 0) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        if (!engine_instrument_preset_param_active(preset, control_order[i])) continue;
        if (index-- == 0) { *out_param = control_order[i]; return true; }
    }
    return false;
}

// Allows the UI to omit empty groups and safely fall back when a preset changes.
bool engine_instrument_preset_group_active(EngineInstrumentPresetId preset, EngineInstrumentParamGroupId group) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec;
        if (engine_instrument_preset_param_active(preset, (EngineInstrumentParamId)i) &&
            engine_instrument_param_spec((EngineInstrumentParamId)i, &spec) && spec.group == group) return true;
    }
    return false;
}

// Bounds a normalized control position and treats non-finite input as its minimum.
static float bounded_position(float value) {
    return !isfinite(value) || value < 0 ? 0 : value > 1 ? 1 : value;
}

// Uses log1p milliseconds for time controls so zero and short times remain reachable.
float engine_instrument_param_to_normalized(EngineInstrumentParamId param, float value) {
    EngineInstrumentParamSpec spec;
    if (!engine_instrument_param_spec(param, &spec) || !isfinite(value)) return 0;
    float position = bounded_position((value - spec.min_value) / (spec.max_value - spec.min_value));
    if (position == 0 || position == 1) return position;
    return spec.scale == ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME
        ? log1pf(position * (spec.max_value - spec.min_value)) / log1pf(spec.max_value - spec.min_value) : position;
}

// Reverses control mapping while leaving stored values, ranges and automation linearity unchanged.
float engine_instrument_param_from_normalized(EngineInstrumentParamId param, float position) {
    EngineInstrumentParamSpec spec;
    if (!engine_instrument_param_spec(param, &spec)) return 0;
    position = bounded_position(position);
    if (position == 0) return spec.min_value;
    if (position == 1) return spec.max_value;
    float range = spec.max_value - spec.min_value;
    return spec.min_value + (spec.scale == ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME
        ? expm1f(position * log1pf(range)) : position * range);
}
