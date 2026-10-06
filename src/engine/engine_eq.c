#include "engine/engine_eq.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float q_from_width(float width_oct) {
    float width = clampf(width_oct, 0.1f, 4.0f);
    float sinh_arg = 0.5f * logf(2.0f) * width;
    float denom = 2.0f * sinhf(sinh_arg);
    if (denom <= 0.000001f) {
        return 0.707f;
    }
    return 1.0f / denom;
}

static void filter_reset(EngineEqFilter* f, int channels) {
    if (!f || channels <= 0 || !f->z1 || !f->z2) {
        return;
    }
    for (int i = 0; i < channels; ++i) {
        f->z1[i] = 0.0f;
        f->z2[i] = 0.0f;
    }
}

static void filter_process(EngineEqFilter* f, float* buffer, int frames, int channels) {
    if (!f || !f->enabled || !buffer || frames <= 0 || channels <= 0) {
        return;
    }
    if (!f->z1 || !f->z2) {
        return;
    }
    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = buffer[base + ch];
            float v = x - f->a1 * f->z1[ch] - f->a2 * f->z2[ch];
            float y = f->b0 * v + f->b1 * f->z1[ch] + f->b2 * f->z2[ch];
            f->z2[ch] = f->z1[ch];
            f->z1[ch] = v;
            if (fabsf(y) < 1e-30f) y = 0.0f;
            buffer[base + ch] = y;
        }
    }
}

static void biquad_design_peaking(EngineEqFilter* f,
                                  float sample_rate,
                                  float freq_hz,
                                  float q,
                                  float gain_db) {
    float fs = sample_rate;
    float w0 = 2.0f * (float)M_PI * clampf(freq_hz, 10.0f, fs * 0.45f) / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float Q = clampf(q, 0.1f, 24.0f);
    float alpha = sinw0 / (2.0f * Q);
    float A = powf(10.0f, gain_db * 0.05f);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static void biquad_design_lowpass(EngineEqFilter* f,
                                  float sample_rate,
                                  float freq_hz,
                                  float q) {
    float fs = sample_rate;
    float w0 = 2.0f * (float)M_PI * clampf(freq_hz, 10.0f, fs * 0.45f) / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float Q = clampf(q, 0.1f, 24.0f);
    float alpha = sinw0 / (2.0f * Q);

    float b0 = (1.0f - cosw0) * 0.5f;
    float b1 = 1.0f - cosw0;
    float b2 = (1.0f - cosw0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static void biquad_design_highpass(EngineEqFilter* f,
                                   float sample_rate,
                                   float freq_hz,
                                   float q) {
    float fs = sample_rate;
    float w0 = 2.0f * (float)M_PI * clampf(freq_hz, 10.0f, fs * 0.45f) / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float Q = clampf(q, 0.1f, 24.0f);
    float alpha = sinw0 / (2.0f * Q);

    float b0 = (1.0f + cosw0) * 0.5f;
    float b1 = -(1.0f + cosw0);
    float b2 = (1.0f + cosw0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static void filter_alloc(EngineEqFilter* f, int channels) {
    if (!f || channels <= 0) {
        return;
    }
    f->z1 = (float*)calloc((size_t)channels, sizeof(float));
    f->z2 = (float*)calloc((size_t)channels, sizeof(float));
}

static void filter_free(EngineEqFilter* f) {
    if (!f) {
        return;
    }
    free(f->z1);
    free(f->z2);
    f->z1 = NULL;
    f->z2 = NULL;
}

void engine_eq_init(EngineEqState* eq, float sample_rate, int max_channels) {
    if (!eq || sample_rate <= 0.0f || max_channels <= 0) {
        return;
    }
    if (eq->initialized) {
        engine_eq_free(eq);
    }
    memset(eq, 0, sizeof(*eq));
    eq->sample_rate = sample_rate;
    eq->max_channels = max_channels;
    filter_alloc(&eq->low_cut, max_channels);
    filter_alloc(&eq->high_cut, max_channels);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        filter_alloc(&eq->bands[i], max_channels);
    }
    eq->initialized = true;
    eq->active = false;
    eq->curve.low_cut.freq_hz = 80.0f;
    eq->curve.high_cut.freq_hz = 12000.0f;
    const float frequencies[ENGINE_EQ_BANDS] = {120.0f, 500.0f, 2000.0f, 8000.0f};
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        eq->curve.bands[i].freq_hz = frequencies[i];
        eq->curve.bands[i].q_width = 1.0f;
    }
}

void engine_eq_free(EngineEqState* eq) {
    if (!eq || !eq->initialized) {
        return;
    }
    filter_free(&eq->low_cut);
    filter_free(&eq->high_cut);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        filter_free(&eq->bands[i]);
    }
    memset(eq, 0, sizeof(*eq));
}

void engine_eq_reset(EngineEqState* eq) {
    if (!eq || !eq->initialized) {
        return;
    }
    int channels = eq->max_channels;
    filter_reset(&eq->low_cut, channels);
    filter_reset(&eq->high_cut, channels);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        filter_reset(&eq->bands[i], channels);
    }
}

// Copies filter coefficients while retaining separately allocated destination histories.
static bool filter_clone_configuration(EngineEqFilter* destination, const EngineEqFilter* source) {
    if (!destination->z1 || !destination->z2) return false;
    destination->enabled = source->enabled;
    destination->b0 = source->b0; destination->b1 = source->b1; destination->b2 = source->b2;
    destination->a1 = source->a1; destination->a2 = source->a2;
    return true;
}

// Prepares a complete independent filter configuration and rejects partial allocation.
bool engine_eq_clone_configuration(EngineEqState* destination, const EngineEqState* source) {
    if (!destination || !source || !source->initialized) return false;
    engine_eq_init(destination, source->sample_rate, source->max_channels);
    bool ok = filter_clone_configuration(&destination->low_cut, &source->low_cut) &&
              filter_clone_configuration(&destination->high_cut, &source->high_cut);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i)
        ok = filter_clone_configuration(&destination->bands[i], &source->bands[i]) && ok;
    if (!ok) { engine_eq_free(destination); return false; }
    destination->active = source->active;
    destination->curve = source->curve;
    return true;
}

// Preserves a filter's delay samples only when its coefficients and enable state still match.
static void filter_transfer_history(EngineEqFilter* destination, const EngineEqFilter* source, int channels) {
    if (destination->enabled != source->enabled || destination->b0 != source->b0 ||
        destination->b1 != source->b1 || destination->b2 != source->b2 ||
        destination->a1 != source->a1 || destination->a2 != source->a2 ||
        !destination->z1 || !destination->z2 || !source->z1 || !source->z2) return;
    memcpy(destination->z1, source->z1, (size_t)channels * sizeof(float));
    memcpy(destination->z2, source->z2, (size_t)channels * sizeof(float));
}

// Transfers compatible histories without allocating, resetting, or destroying DSP state.
void engine_eq_transfer_history(EngineEqState* destination, const EngineEqState* source) {
    if (!destination || !source || destination->sample_rate != source->sample_rate ||
        destination->max_channels != source->max_channels) return;
    filter_transfer_history(&destination->low_cut, &source->low_cut, destination->max_channels);
    filter_transfer_history(&destination->high_cut, &source->high_cut, destination->max_channels);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i)
        filter_transfer_history(&destination->bands[i], &source->bands[i], destination->max_channels);
}

void engine_eq_set_curve(EngineEqState* eq, const EngineEqCurve* curve) {
    if (!eq || !eq->initialized || !curve) {
        return;
    }
    eq->curve = *curve;
    eq->active = false;
    eq->low_cut.enabled = curve->low_cut.enabled;
    if (curve->low_cut.enabled) {
        biquad_design_highpass(&eq->low_cut, eq->sample_rate, curve->low_cut.freq_hz, 0.707f);
        eq->active = true;
    }
    eq->high_cut.enabled = curve->high_cut.enabled;
    if (curve->high_cut.enabled) {
        biquad_design_lowpass(&eq->high_cut, eq->sample_rate, curve->high_cut.freq_hz, 0.707f);
        eq->active = true;
    }
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        EngineEqFilter* f = &eq->bands[i];
        const EngineEqBand* band = &curve->bands[i];
        f->enabled = band->enabled;
        if (band->enabled) {
            float q = q_from_width(band->q_width);
            float gain_db = clampf(band->gain_db, -20.0f, 20.0f);
            biquad_design_peaking(f, eq->sample_rate, band->freq_hz, q, gain_db);
            eq->active = true;
        }
    }
}

void engine_eq_process(EngineEqState* eq, float* buffer, int frames, int channels) {
    if (!eq || !eq->initialized || !eq->active || !buffer) {
        return;
    }
    int use_channels = channels;
    if (use_channels > eq->max_channels) {
        use_channels = eq->max_channels;
    }
    if (use_channels <= 0) {
        return;
    }
    filter_process(&eq->low_cut, buffer, frames, use_channels);
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        filter_process(&eq->bands[i], buffer, frames, use_channels);
    }
    filter_process(&eq->high_cut, buffer, frames, use_channels);
}
