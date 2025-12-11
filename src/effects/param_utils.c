#include "effects/param_utils.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

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

float fx_param_quantize_beats(float beat_value) {
    const float base_step = 1.0f / 16.0f;
    static const float kNoteBeats[] = {
        4.0f, 2.0f, 1.0f, 0.75f, 0.5f, 0.333f, 0.25f, 0.1875f, 0.167f, 0.125f,
        0.09375f, 0.083f, 0.0625f, 0.0468f, 0.0416f, 0.03125f
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
