#include "effects/param_utils.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool unit_is_time(FxParamUnit unit) {
    return unit == FX_PARAM_UNIT_MS || unit == FX_PARAM_UNIT_SECONDS;
}

static bool unit_is_rate(FxParamUnit unit) {
    return unit == FX_PARAM_UNIT_HZ;
}

static void lower_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

FxParamKind fx_param_kind_from_name(const char* name) {
    char lower[64];
    lower_copy(lower, sizeof(lower), name);
    if (strstr(lower, "time_ms") || strstr(lower, "_ms") || strstr(lower, "predelay_ms") ||
        strstr(lower, "attack_ms") || strstr(lower, "release_ms") || strstr(lower, "delay_ms") ||
        strstr(lower, "hold_ms") || strstr(lower, "wobble_depth_ms")) {
        return FX_PARAM_KIND_TIME_MS;
    }
    if (strstr(lower, "time") || strstr(lower, "decay") || strstr(lower, "rt60") ||
        strstr(lower, "hold") || strstr(lower, "release")) {
        return FX_PARAM_KIND_TIME_SECONDS;
    }
    if (strstr(lower, "rate") || strstr(lower, "wobble") || strstr(lower, "lfo")) {
        return FX_PARAM_KIND_RATE_HZ;
    }
    return FX_PARAM_KIND_GENERIC;
}

bool fx_param_kind_is_syncable(FxParamKind kind) {
    return kind == FX_PARAM_KIND_TIME_MS || kind == FX_PARAM_KIND_TIME_SECONDS || kind == FX_PARAM_KIND_RATE_HZ;
}

bool fx_param_spec_is_syncable(const EffectParamSpec* spec) {
    if (!spec) {
        return false;
    }
    if ((spec->flags & FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE) == 0) {
        return false;
    }
    return unit_is_time(spec->unit) || unit_is_rate(spec->unit);
}

float fx_param_native_to_beats(FxParamKind kind, float native_value, const TempoState* tempo) {
    if (!tempo || tempo->bpm <= 0.0) {
        return native_value;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    switch (kind) {
        case FX_PARAM_KIND_TIME_MS:
            return (float)((native_value * 0.001) / sec_per_beat);
        case FX_PARAM_KIND_TIME_SECONDS:
            return (float)((double)native_value / sec_per_beat);
        case FX_PARAM_KIND_RATE_HZ: {
            if (native_value <= 0.0f) {
                return 0.0f;
            }
            double period = 1.0 / (double)native_value;
            return (float)(period / sec_per_beat);
        }
        default:
            return native_value;
    }
}

float fx_param_beats_to_native(FxParamKind kind, float beat_value, const TempoState* tempo) {
    if (!tempo || tempo->bpm <= 0.0) {
        return beat_value;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    switch (kind) {
        case FX_PARAM_KIND_TIME_MS:
            return (float)(beat_value * sec_per_beat * 1000.0);
        case FX_PARAM_KIND_TIME_SECONDS:
            return (float)(beat_value * sec_per_beat);
        case FX_PARAM_KIND_RATE_HZ: {
            double sec = beat_value * sec_per_beat;
            if (sec <= 1e-6) {
                sec = 1e-6;
            }
            return (float)(1.0 / sec);
        }
        default:
            return beat_value;
    }
}

float fx_param_spec_native_to_beats(const EffectParamSpec* spec, float native_value, const TempoState* tempo) {
    if (!spec || !tempo || tempo->bpm <= 0.0) {
        return native_value;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    if (spec->unit == FX_PARAM_UNIT_MS) {
        return (float)((native_value * 0.001) / sec_per_beat);
    }
    if (spec->unit == FX_PARAM_UNIT_SECONDS) {
        return (float)((double)native_value / sec_per_beat);
    }
    if (spec->unit == FX_PARAM_UNIT_HZ) {
        if (native_value <= 0.0f) {
            return 0.0f;
        }
        double period = 1.0 / (double)native_value;
        return (float)(period / sec_per_beat);
    }
    return native_value;
}

float fx_param_spec_beats_to_native(const EffectParamSpec* spec, float beat_value, const TempoState* tempo) {
    if (!spec || !tempo || tempo->bpm <= 0.0) {
        return beat_value;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    if (spec->unit == FX_PARAM_UNIT_MS) {
        return (float)(beat_value * sec_per_beat * 1000.0);
    }
    if (spec->unit == FX_PARAM_UNIT_SECONDS) {
        return (float)(beat_value * sec_per_beat);
    }
    if (spec->unit == FX_PARAM_UNIT_HZ) {
        double sec = beat_value * sec_per_beat;
        if (sec <= 1e-6) {
            sec = 1e-6;
        }
        return (float)(1.0 / sec);
    }
    return beat_value;
}

// Derives beat-space bounds by converting the spec's native min/max for the active tempo.
bool fx_param_spec_get_beat_bounds(const EffectParamSpec* spec,
                                   const TempoState* tempo,
                                   float* out_min_beats,
                                   float* out_max_beats) {
    if (!spec || !out_min_beats || !out_max_beats || !fx_param_spec_is_syncable(spec)) {
        return false;
    }
    TempoState fallback_tempo = {0};
    fallback_tempo.bpm = 120.0;
    const TempoState* active_tempo = (tempo && tempo->bpm > 0.0) ? tempo : &fallback_tempo;
    float beat_a = fx_param_spec_native_to_beats(spec, spec->min_value, active_tempo);
    float beat_b = fx_param_spec_native_to_beats(spec, spec->max_value, active_tempo);
    if (!isfinite(beat_a) || !isfinite(beat_b)) {
        return false;
    }
    float min_beats = fminf(beat_a, beat_b);
    float max_beats = fmaxf(beat_a, beat_b);
    if (min_beats < 0.0f) {
        min_beats = 0.0f;
    }
    if (max_beats < min_beats + 1e-6f) {
        max_beats = min_beats + 1e-6f;
    }
    *out_min_beats = min_beats;
    *out_max_beats = max_beats;
    return true;
}

float fx_param_quantize_beats(float beat_value) {
    const float base_step = 1.0f / 16.0f;
    static const float kNoteBeats[] = {
        4.0f, 2.0f, 1.0f, 3.0f / 4.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f,
        3.0f / 16.0f, 1.0f / 6.0f, 1.0f / 8.0f, 3.0f / 32.0f, 1.0f / 12.0f,
        1.0f / 16.0f, 3.0f / 64.0f, 1.0f / 24.0f, 1.0f / 32.0f
    };
    if (!isfinite(beat_value)) {
        return 0.0f;
    }
    float snapped = roundf(beat_value / base_step) * base_step;
    float best = snapped;
    float best_diff = fabsf(beat_value - snapped);
    for (size_t i = 0; i < sizeof(kNoteBeats) / sizeof(kNoteBeats[0]); ++i) {
        float diff = fabsf(beat_value - kNoteBeats[i]);
        if (diff < best_diff) {
            best_diff = diff;
            best = kNoteBeats[i];
        }
    }
    return best;
}

void fx_param_format_value(const EffectParamSpec* spec, float value, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!spec) {
        snprintf(out, out_size, "%.2f", value);
        return;
    }
    if (spec->type == FX_PARAM_TYPE_BOOL) {
        snprintf(out, out_size, "%s", value >= 0.5f ? "On" : "Off");
        return;
    }
    if (spec->type == FX_PARAM_TYPE_ENUM && spec->enum_count > 0) {
        int idx = (int)lroundf(value);
        if (idx < 0) idx = 0;
        if ((uint32_t)idx >= spec->enum_count) {
            idx = (int)spec->enum_count - 1;
        }
        const char* label = spec->enum_labels[idx];
        if (label && label[0] != '\0') {
            snprintf(out, out_size, "%s", label);
        } else {
            snprintf(out, out_size, "%d", idx);
        }
        return;
    }
    switch (spec->unit) {
        case FX_PARAM_UNIT_DB:
            snprintf(out, out_size, "%.1f dB", value);
            break;
        case FX_PARAM_UNIT_HZ:
            snprintf(out, out_size, "%.1f Hz", value);
            break;
        case FX_PARAM_UNIT_MS:
            snprintf(out, out_size, "%.1f ms", value);
            break;
        case FX_PARAM_UNIT_SECONDS:
            snprintf(out, out_size, "%.2f s", value);
            break;
        case FX_PARAM_UNIT_PERCENT:
            snprintf(out, out_size, "%.1f%%", value * 100.0f);
            break;
        case FX_PARAM_UNIT_RATIO:
            snprintf(out, out_size, "%.2f:1", value);
            break;
        default:
            if (fabsf(value) >= 100.0f) {
                snprintf(out, out_size, "%.0f", value);
            } else {
                snprintf(out, out_size, "%.2f", value);
            }
            break;
    }
}

float fx_param_map_ui_to_value(const EffectParamSpec* spec, float t) {
    if (!spec) {
        return t;
    }
    float min_v = spec->min_value;
    float max_v = spec->max_value;
    if (!isfinite(min_v) || !isfinite(max_v) || fabsf(max_v - min_v) < 1e-6f) {
        return min_v;
    }
    t = clampf(t, 0.0f, 1.0f);
    if (spec->curve == FX_PARAM_CURVE_LOG && min_v > 0.0f && max_v > min_v) {
        float ratio = max_v / min_v;
        return min_v * powf(ratio, t);
    }
    return min_v + t * (max_v - min_v);
}

float fx_param_map_value_to_ui(const EffectParamSpec* spec, float value) {
    if (!spec) {
        return value;
    }
    float min_v = spec->min_value;
    float max_v = spec->max_value;
    if (!isfinite(min_v) || !isfinite(max_v) || fabsf(max_v - min_v) < 1e-6f) {
        return 0.0f;
    }
    value = clampf(value, min_v, max_v);
    if (spec->curve == FX_PARAM_CURVE_LOG && min_v > 0.0f && max_v > min_v) {
        float ratio = max_v / min_v;
        return logf(value / min_v) / logf(ratio);
    }
    return (value - min_v) / (max_v - min_v);
}

int fx_param_spec_find_index(const EffectParamSpec* specs, uint32_t count, const char* id) {
    if (!specs || !id || id[0] == '\0') {
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const char* spec_id = specs[i].id;
        if (spec_id && strcmp(spec_id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}
