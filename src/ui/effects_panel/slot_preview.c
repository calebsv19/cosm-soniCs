#include "ui/effects_panel_preview.h"
#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/kit_viz_fx_preview_adapter.h"
#include "ui/render_utils.h"

#include <math.h>
#include <stdio.h>

// effects_slot_preview_has_gr returns true if the FX type emits gain reduction scope samples.
bool effects_slot_preview_has_gr(FxTypeId type_id) {
    return type_id == 20u || type_id == 21u || type_id == 22u || type_id == 23u
        || type_id == 24u || type_id == 25u || type_id == 26u || type_id == 27u;
}

// effects_slot_preview_mode returns the preview rendering mode for an effect.
EffectsPreviewMode effects_slot_preview_mode(FxTypeId type_id) {
    switch (type_id) {
        case 7u:
            return FX_PREVIEW_HISTORY_TRIM;
        case 20u:
        case 21u:
        case 22u:
        case 23u:
        case 24u:
        case 25u:
        case 26u:
        case 27u:
            return FX_PREVIEW_HISTORY_GR;
        case 30u:
        case 31u:
        case 32u:
        case 33u:
        case 40u:
        case 41u:
        case 43u:
            return FX_PREVIEW_EQ;
        case 70u:
        case 71u:
        case 72u:
        case 73u:
        case 74u:
        case 75u:
        case 76u:
            return FX_PREVIEW_LFO;
        case 90u:
        case 91u:
        case 92u:
        case 93u:
            return FX_PREVIEW_REVERB;
        case 50u:
        case 51u:
        case 52u:
        case 53u:
        case 54u:
            return FX_PREVIEW_DELAY;
        case 60u:
        case 61u:
        case 62u:
        case 63u:
        case 64u:
        case 65u:
            return FX_PREVIEW_CURVE;
        default:
            return FX_PREVIEW_NONE;
    }
}

// EffectsPreviewStyle holds display configuration for the preview history plot.
typedef struct EffectsPreviewStyle {
    bool bipolar;
    float max_db;
    const char* value_label;
} EffectsPreviewStyle;

// effects_slot_preview_style fills history plot display parameters based on effect type.
static void effects_slot_preview_style(FxTypeId type_id, EffectsPreviewStyle* out) {
    if (!out) {
        return;
    }
    out->bipolar = false;
    out->max_db = 24.0f;
    out->value_label = "GR";
    if (type_id == 7u) {
        out->bipolar = true;
        out->value_label = "Trim";
    }
}

// db_to_lin converts a decibel value to linear gain.
static float db_to_lin(float db) {
    return powf(10.0f, db * 0.05f);
}

// clampf clamps a float to a range.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// PreviewBiquadCoeffs stores normalized biquad filter coefficients for preview math.
typedef struct PreviewBiquadCoeffs {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} PreviewBiquadCoeffs;

// preview_db_to_lin converts a decibel value to linear gain for preview math.
static float preview_db_to_lin(float db) {
    return powf(10.0f, db * 0.05f);
}

// preview_biquad_mag evaluates the magnitude response for a biquad at angular frequency.
static float preview_biquad_mag(const PreviewBiquadCoeffs* bq, float w) {
    float cw = cosf(w);
    float sw = sinf(w);
    float c2 = cosf(2.0f * w);
    float s2 = sinf(2.0f * w);
    float num_re = bq->b0 + bq->b1 * cw + bq->b2 * c2;
    float num_im = -bq->b1 * sw - bq->b2 * s2;
    float den_re = 1.0f + bq->a1 * cw + bq->a2 * c2;
    float den_im = -bq->a1 * sw - bq->a2 * s2;
    float num = num_re * num_re + num_im * num_im;
    float den = den_re * den_re + den_im * den_im;
    if (den <= 0.0f) {
        return 0.0f;
    }
    return sqrtf(num / den);
}

// preview_make_peaking designs RBJ peaking EQ coefficients for preview.
static PreviewBiquadCoeffs preview_make_peaking(float sr, float freq, float q, float gain_db) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha / A;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_low_shelf designs RBJ low shelf coefficients for preview.
static PreviewBiquadCoeffs preview_make_low_shelf(float sr, float freq, float gain_db, float slope) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = 2.0f * A * ((Am - Ap * cosw0));
    const float b2 = A * ((Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = -2.0f * ((Am + Ap * cosw0));
    const float a2 = (Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_low_shelf_half_db designs RBJ low shelf coefficients using dB/40 scaling.
static PreviewBiquadCoeffs preview_make_low_shelf_half_db(float sr, float freq, float gain_db, float slope) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = 2.0f * A * ((Am - Ap * cosw0));
    const float b2 = A * ((Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = -2.0f * ((Am + Ap * cosw0));
    const float a2 = (Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_high_shelf designs RBJ high shelf coefficients for preview.
static PreviewBiquadCoeffs preview_make_high_shelf(float sr, float freq, float gain_db, float slope) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = -2.0f * A * ((Am + Ap * cosw0));
    const float b2 = A * ((Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = 2.0f * ((Am - Ap * cosw0));
    const float a2 = (Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_high_shelf_half_db designs RBJ high shelf coefficients using dB/40 scaling.
static PreviewBiquadCoeffs preview_make_high_shelf_half_db(float sr, float freq, float gain_db, float slope) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = -2.0f * A * ((Am + Ap * cosw0));
    const float b2 = A * ((Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = 2.0f * ((Am - Ap * cosw0));
    const float a2 = (Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_notch designs RBJ notch coefficients for preview.
static PreviewBiquadCoeffs preview_make_notch(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = 1.0f;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_lowpass designs RBJ low-pass coefficients for preview.
static PreviewBiquadCoeffs preview_make_lowpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = (1.0f - cosw0) / 2.0f;
    const float b1 = 1.0f - cosw0;
    const float b2 = (1.0f - cosw0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_highpass designs RBJ high-pass coefficients for preview.
static PreviewBiquadCoeffs preview_make_highpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = (1.0f + cosw0) / 2.0f;
    const float b1 = -(1.0f + cosw0);
    const float b2 = (1.0f + cosw0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_make_bandpass designs RBJ band-pass coefficients for preview.
static PreviewBiquadCoeffs preview_make_bandpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

// preview_lfo_sample computes a shape-blended LFO value for preview rendering.
static float preview_lfo_sample(float phase, float shape) {
    float s = sinf(phase * 2.0f * 3.14159265358979323846f);
    float tri = 2.0f * fabsf(2.0f * (phase - floorf(phase + 0.5f))) - 1.0f;
    float sq = phase < 0.5f ? 1.0f : -1.0f;
    float blend = clampf(shape, 0.0f, 1.0f);
    float mix1 = (1.0f - blend) * s + blend * tri;
    return (1.0f - blend) * mix1 + blend * sq;
}

// preview_rate_cycles maps a rate to a reasonable number of cycles for the preview plot.
static float preview_rate_cycles(float rate_hz) {
    float cycles = rate_hz * 0.8f;
    if (cycles < 1.0f) cycles = 1.0f;
    if (cycles > 8.0f) cycles = 8.0f;
    return cycles;
}

// preview_envelope_from_rt60 converts time to a normalized decay envelope for an RT60.
static float preview_envelope_from_rt60(float t_sec, float rt60_sec) {
    if (rt60_sec <= 0.0001f) {
        return 0.0f;
    }
    float db = -60.0f * (t_sec / rt60_sec);
    return clampf(powf(10.0f, db / 20.0f), 0.0f, 1.0f);
}

// preview_delay_amp returns an echo amplitude after n repeats.
static float preview_delay_amp(float feedback, int repeat) {
    float amp = 1.0f;
    for (int i = 0; i < repeat; ++i) {
        amp *= feedback;
    }
    return amp;
}

// preview_mod_ms_value converts delay modulation params into a normalized value for plotting.
static float preview_mod_ms_value(float base_ms, float depth_ms, float phase, float max_range) {
    float mod = preview_lfo_sample(phase, 0.4f);
    float value = base_ms + mod * depth_ms;
    return clampf(value / max_range, 0.0f, 1.0f);
}

// effects_slot_preview_eq_mag returns the preview magnitude response for EQ-style effects.
static float effects_slot_preview_eq_mag(FxTypeId type_id,
                                         const FxSlotUIState* slot,
                                         float freq_hz,
                                         float sample_rate,
                                         float sweep_min_hint) {
    if (!slot || sample_rate <= 0.0f) {
        return 1.0f;
    }
    float mag = 1.0f;
    float q = 0.707f;
    float freq = freq_hz;
    float w = 2.0f * 3.14159265358979323846f * freq_hz / sample_rate;
    switch (type_id) {
        case 30u: { // EQ 3
            float low_gain = clampf(slot->param_values[0], -15.0f, 15.0f);
            float mid_gain = clampf(slot->param_values[1], -15.0f, 15.0f);
            float high_gain = clampf(slot->param_values[2], -15.0f, 15.0f);
            q = clampf(slot->param_values[3], 0.3f, 4.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf(sample_rate, 100.0f, low_gain, 1.0f);
            PreviewBiquadCoeffs mid = preview_make_peaking(sample_rate, 1000.0f, q, mid_gain);
            PreviewBiquadCoeffs high = preview_make_high_shelf(sample_rate, 8000.0f, high_gain, 1.0f);
            mag = preview_biquad_mag(&low, w) * preview_biquad_mag(&mid, w) * preview_biquad_mag(&high, w);
            break;
        }
        case 31u: { // Notch
            freq = clampf(slot->param_values[0], 40.0f, 18000.0f);
            q = clampf(slot->param_values[1], 1.0f, 30.0f);
            float depth_db = clampf(slot->param_values[2], -30.0f, 0.0f);
            PreviewBiquadCoeffs notch = preview_make_notch(sample_rate, freq, q);
            PreviewBiquadCoeffs cut = preview_make_peaking(sample_rate, freq, q, depth_db);
            mag = preview_biquad_mag(&notch, w) * preview_biquad_mag(&cut, w);
            break;
        }
        case 32u: { // Tilt
            float tilt_db = clampf(slot->param_values[0], -12.0f, 12.0f);
            float pivot = clampf(slot->param_values[1], 200.0f, 2000.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf(sample_rate, pivot, -tilt_db * 0.5f, 1.0f);
            PreviewBiquadCoeffs high = preview_make_high_shelf(sample_rate, pivot, tilt_db * 0.5f, 1.0f);
            mag = preview_biquad_mag(&low, w) * preview_biquad_mag(&high, w);
            break;
        }
        case 33u: { // Filter
            int ftype = (int)slot->param_values[0];
            float cutoff = clampf(slot->param_values[1], 20.0f, sample_rate * 0.45f);
            q = clampf(slot->param_values[2], 0.3f, 12.0f);
            float gain_db = clampf(slot->param_values[3], -24.0f, 24.0f);
            PreviewBiquadCoeffs bq;
            switch (ftype) {
                case 1:
                    bq = preview_make_highpass(sample_rate, cutoff, q);
                    break;
                case 2:
                    bq = preview_make_bandpass(sample_rate, cutoff, q);
                    break;
                case 3:
                    bq = preview_make_notch(sample_rate, cutoff, q);
                    break;
                default:
                    bq = preview_make_lowpass(sample_rate, cutoff, q);
                    break;
            }
            mag = preview_biquad_mag(&bq, w) * preview_db_to_lin(gain_db);
            break;
        }
        case 40u: { // Auto Wah
            float freq = clampf(slot->param_values[1], 200.0f, 4000.0f);
            q = clampf(slot->param_values[2], 0.5f, 8.0f);
            float mix = clampf(slot->param_values[5], 0.0f, 1.0f);
            PreviewBiquadCoeffs bp = preview_make_bandpass(sample_rate, freq, q);
            float wet = preview_biquad_mag(&bp, w);
            mag = (1.0f - mix) + wet * mix;
            break;
        }
        case 41u: { // Tilt EQ
            float pivot = clampf(slot->param_values[0], 100.0f, 5000.0f);
            float tilt_db = clampf(slot->param_values[1], -12.0f, 12.0f);
            float mix = clampf(slot->param_values[2], 0.0f, 1.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf_half_db(sample_rate, pivot, -tilt_db * 0.5f, 0.5f);
            PreviewBiquadCoeffs high = preview_make_high_shelf_half_db(sample_rate, pivot, tilt_db * 0.5f, 0.5f);
            float wet = preview_biquad_mag(&low, w) * preview_biquad_mag(&high, w);
            mag = (1.0f - mix) + wet * mix;
            break;
        }
        case 43u: { // Sweep EQ
            float sweep_min = sweep_min_hint > 0.0f ? sweep_min_hint : clampf(slot->param_values[0], 200.0f, 2000.0f);
            float sweep_max = clampf(slot->param_values[1], sweep_min + 10.0f, 6000.0f);
            float depth_db = clampf(slot->param_values[2], -12.0f, 12.0f);
            float rate = clampf(slot->param_values[3], 0.1f, 5.0f);
            float phase = (rate > 0.0f) ? sweep_min + (sweep_max - sweep_min) * 0.5f : sweep_min;
            float freq = phase;
            PreviewBiquadCoeffs bq = preview_make_peaking(sample_rate, freq, 0.7f, depth_db);
            mag = preview_biquad_mag(&bq, w);
            break;
        }
        default:
            break;
    }
    return mag;
}

// effects_slot_preview_curve_eval evaluates an effect transfer curve for a sample.
static float effects_slot_preview_curve_eval(FxTypeId type_id, const FxSlotUIState* slot, float x) {
    if (!slot) {
        return x;
    }
    switch (type_id) {
        case 60u: { // Hard Clip
            float thr_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[2];
            float pre = db_to_lin(-thr_db);
            float post = db_to_lin(out_db);
            float y = clampf(x * pre, -1.0f, 1.0f);
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 61u: { // Soft Saturation
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[2];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float y = tanhf(x * pre);
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 62u: { // Bit Crusher
            float bits = slot->param_values[0];
            float mix = slot->param_values[3];
            int qbits = (int)lroundf(clampf(bits, 4.0f, 16.0f));
            float q = (qbits >= 16) ? x : roundf(x * (float)(1 << (qbits - 1))) / (float)(1 << (qbits - 1));
            q = clampf(q, -1.0f, 1.0f);
            return (1.0f - mix) * x + mix * q;
        }
        case 63u: { // Overdrive
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[4];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float y = tanhf(x * pre) * 0.8f + tanhf(x * pre * 0.3f) * 0.2f;
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 64u: { // Waveshaper
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[4];
            float shape = slot->param_values[3];
            float bias = slot->param_values[2];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float d = x * pre + bias;
            float y = d;
            switch ((int)shape) {
                case 0:
                    y = tanhf(d);
                    break;
                case 1:
                    y = atanf(d) * 0.63662f;
                    break;
                case 2:
                    y = d / (1.0f + fabsf(d));
                    break;
                case 3:
                    if (d > 1.0f) y = 2.0f - d;
                    else if (d < -1.0f) y = -2.0f - d;
                    break;
                default:
                    y = d;
                    break;
            }
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 65u: { // Decimator
            float bit_depth = slot->param_values[1];
            float mix = slot->param_values[4];
            int bits = (int)lroundf(clampf(bit_depth, 4.0f, 24.0f));
            float q = (bits >= 24) ? x : roundf(x * (float)(1 << (bits - 1))) / (float)(1 << (bits - 1));
            q = clampf(q, -1.0f, 1.0f);
            return (1.0f - mix) * x + mix * q;
        }
        default:
            break;
    }
    return x;
}

// effects_slot_preview_push appends a sample to the preview history buffer.
static void effects_slot_preview_push(EffectsPanelPreviewSlotState* preview, float gr_db) {
    if (!preview) {
        return;
    }
    preview->history[preview->history_write] = gr_db;
    preview->history_write = (preview->history_write + 1) % FX_PANEL_PREVIEW_HISTORY;
    if (preview->history_write == 0) {
        preview->history_filled = true;
    }
}

// effects_slot_preview_quant_steps returns the step count for quantizer previews.
static int effects_slot_preview_quant_steps(FxTypeId type_id, const FxSlotUIState* slot) {
    if (!slot) {
        return 0;
    }
    if (type_id == 62u) {
        float bits = slot->param_values[0];
        int qbits = (int)lroundf(clampf(bits, 4.0f, 16.0f));
        int steps = 1 << qbits;
        if (steps > 64) steps = 64;
        return steps;
    }
    if (type_id == 65u) {
        float bits = slot->param_values[1];
        int qbits = (int)lroundf(clampf(bits, 4.0f, 24.0f));
        int steps = 1 << qbits;
        if (steps > 64) steps = 64;
        return steps;
    }
    return 0;
}

// effects_slot_preview_render_history draws the history preview panel for a slot.
static void effects_slot_preview_render_history(SDL_Renderer* renderer,
                                                const EffectsPanelPreviewSlotState* preview,
                                                const SDL_Rect* rect,
                                                SDL_Color label_color,
                                                SDL_Color text_dim,
                                                const char* title,
                                                const EffectsPreviewStyle* style) {
    if (!renderer || !preview || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {90, 150, 210, 220};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    int count = FX_PANEL_PREVIEW_HISTORY;
    if (count <= 1 || plot_rect.w <= 0 || plot_rect.h <= 0) {
        return;
    }
    int start = preview->history_write;
    int draw_h = plot_rect.h;
    int max_y = plot_rect.y + plot_rect.h;
    int min_y = plot_rect.y;

    float latest_gr = preview->history[(start + count - 1) % FX_PANEL_PREVIEW_HISTORY];
    if (title) {
        char buf[32];
        const char* label = (style && style->value_label) ? style->value_label : "GR";
        float display_value = latest_gr;
        if (style && style->bipolar) {
            snprintf(buf, sizeof(buf), "%s %+0.1f dB", label, display_value);
        } else {
            float reduction = -display_value;
            if (reduction < 0.0f) reduction = 0.0f;
            snprintf(buf, sizeof(buf), "%s %0.1f dB", label, reduction);
        }
        int value_w = ui_measure_text_width(buf, 0.9f);
        int text_x = rect->x + rect->w - value_w - pad;
        int text_y = rect->y + 4;
        ui_draw_text(renderer, text_x, text_y, buf, text_dim, 0.9f);
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    float max_db = (style && style->max_db > 0.0f) ? style->max_db : 24.0f;
    if (max_db < 6.0f) max_db = 6.0f;

    float y_samples[FX_PANEL_PREVIEW_HISTORY];
    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % FX_PANEL_PREVIEW_HISTORY;
        float gr_db = preview->history[idx];
        if (style && style->bipolar) {
            y_samples[i] = gr_db;
        } else {
            float reduction = -gr_db;
            if (reduction < 0.0f) reduction = 0.0f;
            y_samples[i] = max_db - reduction;
        }
    }

    DawKitVizPlotRange range = style && style->bipolar
                               ? (DawKitVizPlotRange){-max_db, max_db}
                               : (DawKitVizPlotRange){0.0f, max_db};
    KitVizVecSegment segments[FX_PANEL_PREVIEW_HISTORY];
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)count,
                                                                   &plot_rect,
                                                                   range,
                                                                   segments,
                                                                   FX_PANEL_PREVIEW_HISTORY,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        daw_kit_viz_render_segments(renderer, segments, segment_count, line);
        return;
    }

    // Fallback keeps previous direct draw behavior if adapter conversion fails.
    int prev_x = plot_rect.x;
    int prev_y = max_y;
    for (int i = 0; i < count; ++i) {
        float norm = clampf((y_samples[i] - range.min_value) / (range.max_value - range.min_value), 0.0f, 1.0f);
        int x = plot_rect.x + (int)lroundf(((float)i / (float)(count - 1)) * (float)plot_rect.w);
        int y = max_y - (int)lroundf(norm * (float)draw_h);
        if (y < min_y) y = min_y;
        if (y > max_y) y = max_y;
        if (i > 0) {
            SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
            SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        }
        prev_x = x;
        prev_y = y;
    }
}

// effects_slot_preview_render_curve draws a static transfer curve preview.
static void effects_slot_preview_render_curve(SDL_Renderer* renderer,
                                              const FxSlotUIState* slot,
                                              const SDL_Rect* rect,
                                              SDL_Color label_color,
                                              const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {110, 190, 240, 220};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_Color unity = {90, 100, 120, 200};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    int mid_x = plot_rect.x + plot_rect.w / 2;
    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);
    SDL_RenderDrawLine(renderer, mid_x, plot_rect.y, mid_x, plot_rect.y + plot_rect.h);

    SDL_SetRenderDrawColor(renderer, unity.r, unity.g, unity.b, unity.a);
    SDL_RenderDrawLine(renderer, plot_rect.x, plot_rect.y + plot_rect.h, plot_rect.x + plot_rect.w, plot_rect.y);

    int steps = effects_slot_preview_quant_steps(slot->type_id, slot);
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float x = ((float)i / (float)(plot_rect.w - 1)) * 2.0f - 1.0f;
        float y = effects_slot_preview_curve_eval(slot->type_id, slot, x);
        if (steps > 0) {
            y = roundf(y * (float)(steps - 1)) / (float)(steps - 1);
        }
        y_samples[i] = y;
    }

    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    if (!segments) {
        free(y_samples);
        return;
    }
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)plot_rect.w,
                                                                   &plot_rect,
                                                                   (DawKitVizPlotRange){-1.0f, 1.0f},
                                                                   segments,
                                                                   (size_t)plot_rect.w,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        daw_kit_viz_render_segments(renderer, segments, segment_count, line);
        free(segments);
        free(y_samples);
        return;
    }

    // Fallback keeps previous direct draw behavior if adapter conversion fails.
    int prev_x = plot_rect.x;
    int prev_y = plot_rect.y + plot_rect.h;
    for (int i = 0; i < plot_rect.w; ++i) {
        float y = y_samples[i];
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h - (int)lroundf((y + 1.0f) * 0.5f * (float)plot_rect.h);
        if (i > 0) {
            SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
            SDL_RenderDrawLine(renderer, prev_x, prev_y, px, py);
        }
        prev_x = px;
        prev_y = py;
    }
    free(segments);
    free(y_samples);
}

// effects_slot_preview_render_eq draws a frequency response preview for EQ-style effects.
static void effects_slot_preview_render_eq(SDL_Renderer* renderer,
                                           const FxSlotUIState* slot,
                                           const SDL_Rect* rect,
                                           SDL_Color label_color,
                                           float sample_rate,
                                           const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {110, 190, 240, 220};
    SDL_Color line_alt = {130, 120, 220, 200};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_Color zero = {90, 100, 120, 200};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_SetRenderDrawColor(renderer, zero.r, zero.g, zero.b, zero.a);
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);

    float sweep_min = 0.0f;
    float sweep_max = 0.0f;
    bool dual_curve = false;
    if (slot->type_id == 40u) {
        sweep_min = clampf(slot->param_values[0], 200.0f, 2000.0f);
        sweep_max = clampf(slot->param_values[1], sweep_min + 10.0f, 6000.0f);
        dual_curve = true;
    }

    float* db_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    float* db_samples_alt = dual_curve ? (float*)malloc((size_t)plot_rect.w * sizeof(float)) : NULL;
    if (!db_samples || (dual_curve && !db_samples_alt)) {
        free(db_samples);
        free(db_samples_alt);
        return;
    }

    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float log_min = log10f(20.0f);
        float log_max = log10f(sample_rate * 0.45f);
        float log_f = log_min + t * (log_max - log_min);
        float freq = powf(10.0f, log_f);
        float mag = effects_slot_preview_eq_mag(slot->type_id, slot, freq, sample_rate, dual_curve ? sweep_max : 0.0f);
        float db = 20.0f * log10f(fmaxf(mag, 1e-4f));
        db_samples[i] = db;

        if (dual_curve) {
            float mag_alt = effects_slot_preview_eq_mag(slot->type_id, slot, freq, sample_rate, sweep_min);
            float db_alt = 20.0f * log10f(fmaxf(mag_alt, 1e-4f));
            db_samples_alt[i] = db_alt;
        }
    }

    DawKitVizPlotRange range = {-24.0f, 24.0f};
    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    KitVizVecSegment* segments_alt = dual_curve ? (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment)) : NULL;
    if (!segments || (dual_curve && !segments_alt)) {
        free(db_samples);
        free(db_samples_alt);
        free(segments);
        free(segments_alt);
        return;
    }

    size_t segment_count = 0;
    CoreResult r_main = daw_kit_viz_plot_line_from_y_samples(db_samples,
                                                             (uint32_t)plot_rect.w,
                                                             &plot_rect,
                                                             range,
                                                             segments,
                                                             (size_t)plot_rect.w,
                                                             &segment_count);
    CoreResult r_alt = core_result_ok();
    size_t segment_count_alt = 0;
    if (dual_curve) {
        r_alt = daw_kit_viz_plot_line_from_y_samples(db_samples_alt,
                                                     (uint32_t)plot_rect.w,
                                                     &plot_rect,
                                                     range,
                                                     segments_alt,
                                                     (size_t)plot_rect.w,
                                                     &segment_count_alt);
    }
    if (r_main.code == CORE_OK && (!dual_curve || r_alt.code == CORE_OK)) {
        daw_kit_viz_render_segments(renderer, segments, segment_count, line);
        if (dual_curve) {
            daw_kit_viz_render_segments(renderer, segments_alt, segment_count_alt, line_alt);
        }
        free(db_samples);
        free(db_samples_alt);
        free(segments);
        free(segments_alt);
        return;
    }

    // Fallback keeps previous direct draw behavior if adapter conversion fails.
    int prev_x = plot_rect.x;
    int prev_y = mid_y;
    int prev_y_alt = mid_y;
    for (int i = 0; i < plot_rect.w; ++i) {
        float y_norm = clampf((db_samples[i] + 24.0f) / 48.0f, 0.0f, 1.0f);
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h - (int)lroundf(y_norm * (float)plot_rect.h);
        int old_prev_x = prev_x;
        if (i > 0) {
            SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
            SDL_RenderDrawLine(renderer, prev_x, prev_y, px, py);
        }
        if (dual_curve) {
            float y_alt = clampf((db_samples_alt[i] + 24.0f) / 48.0f, 0.0f, 1.0f);
            int py_alt = plot_rect.y + plot_rect.h - (int)lroundf(y_alt * (float)plot_rect.h);
            if (i > 0) {
                SDL_SetRenderDrawColor(renderer, line_alt.r, line_alt.g, line_alt.b, line_alt.a);
                SDL_RenderDrawLine(renderer, old_prev_x, prev_y_alt, px, py_alt);
            }
            prev_y_alt = py_alt;
        }
        prev_x = px;
        prev_y = py;
    }
    free(db_samples);
    free(db_samples_alt);
    free(segments);
    free(segments_alt);
}

// effects_slot_preview_render_lfo draws an LFO-style preview for modulation effects.
static void effects_slot_preview_render_lfo(SDL_Renderer* renderer,
                                            const FxSlotUIState* slot,
                                            const SDL_Rect* rect,
                                            SDL_Color label_color,
                                            const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {110, 190, 240, 220};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);

    float rate_hz = 1.0f;
    float depth = 1.0f;
    float shape = 0.5f;
    float base_ms = 0.0f;
    float depth_ms = 0.0f;
    float max_range = 1.0f;
    float phase_offset = 0.0f;

    switch (slot->type_id) {
        case 70u: // Tremolo
            rate_hz = clampf(slot->param_values[0], 0.1f, 20.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = clampf(slot->param_values[2], 0.0f, 1.0f);
            break;
        case 71u: // Chorus
            rate_hz = clampf(slot->param_values[0], 0.05f, 5.0f);
            depth_ms = clampf(slot->param_values[1], 0.0f, 15.0f);
            base_ms = clampf(slot->param_values[2], 5.0f, 25.0f);
            max_range = 40.0f;
            break;
        case 72u: // Flanger
            rate_hz = clampf(slot->param_values[0], 0.05f, 5.0f);
            depth_ms = clampf(slot->param_values[1], 0.1f, 5.0f);
            base_ms = clampf(slot->param_values[2], 0.1f, 5.0f);
            max_range = 10.0f;
            break;
        case 73u: // Phaser
            rate_hz = clampf(slot->param_values[0], 0.1f, 10.0f);
            depth_ms = clampf(slot->param_values[1], 0.1f, 8.0f);
            base_ms = clampf(slot->param_values[2], 2.0f, 12.0f);
            max_range = 20.0f;
            break;
        case 74u: // Ring Mod
            rate_hz = clampf(slot->param_values[0], 1.0f, 5000.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = 0.1f;
            break;
        case 75u: // Auto Pan
            rate_hz = clampf(slot->param_values[0], 0.01f, 10.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            phase_offset = clampf(slot->param_values[2], 0.0f, 360.0f) / 360.0f;
            break;
        case 76u: // Vibrato
            rate_hz = clampf(slot->param_values[0], 0.05f, 3.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = 0.2f;
            break;
        default:
            break;
    }

    float cycles = preview_rate_cycles(rate_hz);
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float phase = t * cycles + phase_offset;
        float value = 0.0f;
        if (slot->type_id == 71u || slot->type_id == 72u || slot->type_id == 73u) {
            value = preview_mod_ms_value(base_ms, depth_ms, phase, max_range);
        } else {
            value = preview_lfo_sample(phase, shape) * depth;
        }
        if (slot->type_id == 71u || slot->type_id == 72u || slot->type_id == 73u) {
            value = (value - 0.5f) * 2.0f;
        }
        y_samples[i] = value;
    }

    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    if (!segments) {
        free(y_samples);
        return;
    }
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)plot_rect.w,
                                                                   &plot_rect,
                                                                   (DawKitVizPlotRange){-1.0f, 1.0f},
                                                                   segments,
                                                                   (size_t)plot_rect.w,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        daw_kit_viz_render_segments(renderer, segments, segment_count, line);
        free(segments);
        free(y_samples);
        return;
    }

    // Fallback keeps previous direct draw behavior if adapter conversion fails.
    int prev_x = plot_rect.x;
    int prev_y = mid_y;
    for (int i = 0; i < plot_rect.w; ++i) {
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h / 2 - (int)lroundf(y_samples[i] * 0.5f * (float)plot_rect.h);
        if (i > 0) {
            SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
            SDL_RenderDrawLine(renderer, prev_x, prev_y, px, py);
        }
        prev_x = px;
        prev_y = py;
    }
    free(segments);
    free(y_samples);
}

// effects_slot_preview_render_reverb draws a time response preview for reverb-style effects.
static void effects_slot_preview_render_reverb(SDL_Renderer* renderer,
                                               const FxSlotUIState* slot,
                                               const SDL_Rect* rect,
                                               SDL_Color label_color,
                                               const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {110, 190, 240, 220};
    SDL_Color line_dim = {120, 120, 200, 180};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    float rt60 = 1.5f;
    float damping = 0.4f;
    float predelay_ms = 0.0f;
    float gate_thresh = -24.0f;
    float gate_hold_ms = 0.0f;
    float gate_release_ms = 120.0f;
    bool gated = false;
    bool early = false;
    bool plate = false;

    switch (slot->type_id) {
        case 90u: // Reverb
            rt60 = clampf(slot->param_values[1], 0.1f, 10.0f);
            damping = clampf(slot->param_values[2], 0.0f, 1.0f);
            predelay_ms = clampf(slot->param_values[3], 0.0f, 100.0f);
            break;
        case 91u: // Early reflections
            predelay_ms = clampf(slot->param_values[0], 0.0f, 100.0f);
            early = true;
            break;
        case 92u: { // Plate
            rt60 = clampf(slot->param_values[1], 0.2f, 8.0f);
            float highcut = clampf(slot->param_values[2], 2000.0f, 16000.0f);
            damping = clampf(1.0f - highcut / 16000.0f, 0.0f, 1.0f);
            predelay_ms = clampf(slot->param_values[3], 0.0f, 120.0f);
            plate = true;
            break;
        }
        case 93u: // Gated
            rt60 = clampf(slot->param_values[1], 0.2f, 4.0f);
            gate_thresh = clampf(slot->param_values[2], -60.0f, 0.0f);
            gate_hold_ms = clampf(slot->param_values[3], 0.0f, 500.0f);
            gate_release_ms = clampf(slot->param_values[4], 5.0f, 800.0f);
            gated = true;
            break;
        default:
            break;
    }

    float predelay_sec = predelay_ms * 0.001f;
    float total_time = rt60 * 1.2f + predelay_sec;
    if (gated) {
        total_time = predelay_sec + (gate_hold_ms + gate_release_ms) * 0.001f + 0.3f;
    }
    if (early) {
        total_time = predelay_sec + 0.4f;
    }
    if (total_time < 0.5f) total_time = 0.5f;
    if (total_time > 6.0f) total_time = 6.0f;

    int base_y = plot_rect.y + plot_rect.h;
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float time = t * total_time;
        float env = 0.0f;
        if (time < predelay_sec) {
            env = 0.0f;
        } else {
            float tail_t = time - predelay_sec;
            env = preview_envelope_from_rt60(tail_t, rt60);
            float damp_rt60 = fmaxf(0.2f, rt60 * (1.0f - damping * 0.7f));
            float env_dim = preview_envelope_from_rt60(tail_t, damp_rt60);
            if (plate) {
                env = (env + env_dim * 0.8f) * 0.5f;
            } else {
                env = env * 0.6f + env_dim * 0.4f;
            }
            if (early) {
                env *= expf(-tail_t * 6.0f);
            }
            if (gated) {
                float hold = gate_hold_ms * 0.001f;
                float release = gate_release_ms * 0.001f;
                float gate_time = tail_t;
                float gate_env = 1.0f;
                if (gate_time > hold) {
                    float rel_t = gate_time - hold;
                    gate_env = expf(-rel_t / fmaxf(release, 0.02f));
                }
                float gate_scale = (gate_thresh + 60.0f) / 60.0f;
                env = env * fmaxf(gate_env, gate_scale * 0.2f);
            }
        }
        y_samples[i] = env;
    }

    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    if (!segments) {
        free(y_samples);
        return;
    }
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)plot_rect.w,
                                                                   &plot_rect,
                                                                   (DawKitVizPlotRange){0.0f, 1.0f},
                                                                   segments,
                                                                   (size_t)plot_rect.w,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        daw_kit_viz_render_segments(renderer, segments, segment_count, line);
    } else {
        // Fallback keeps previous direct draw behavior if adapter conversion fails.
        int prev_x = plot_rect.x;
        int prev_y = base_y;
        for (int i = 0; i < plot_rect.w; ++i) {
            int px = plot_rect.x + i;
            int py = base_y - (int)lroundf(y_samples[i] * (float)plot_rect.h);
            if (i > 0) {
                SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
                SDL_RenderDrawLine(renderer, prev_x, prev_y, px, py);
            }
            prev_x = px;
            prev_y = py;
        }
    }
    free(segments);
    free(y_samples);

    if (early) {
        int tap_count = 6;
        for (int i = 0; i < tap_count; ++i) {
            float tap_time = predelay_sec + 0.03f * (float)i;
            float amp = 1.0f - 0.12f * (float)i;
            if (tap_time > total_time) {
                break;
            }
            int px = plot_rect.x + (int)lroundf((tap_time / total_time) * (float)plot_rect.w);
            int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
            SDL_SetRenderDrawColor(renderer, line_dim.r, line_dim.g, line_dim.b, line_dim.a);
            SDL_RenderDrawLine(renderer, px, base_y, px, py);
        }
    }
}

// effects_slot_preview_render_delay draws a tap/echo preview for delay effects.
static void effects_slot_preview_render_delay(SDL_Renderer* renderer,
                                              const FxSlotUIState* slot,
                                              const SDL_Rect* rect,
                                              SDL_Color label_color,
                                              const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {22, 24, 30, 255};
    SDL_Color border = {70, 76, 92, 255};
    SDL_Color line = {110, 190, 240, 220};
    SDL_Color line_dim = {120, 120, 200, 180};
    SDL_Color grid = {50, 54, 66, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    int base_y = plot_rect.y + plot_rect.h;
    SDL_RenderDrawLine(renderer, plot_rect.x, base_y, plot_rect.x + plot_rect.w, base_y);

    float time_ms = 400.0f;
    float feedback = 0.4f;
    float wobble_ms = 0.0f;
    float diffusion = 0.0f;
    bool pingpong = false;
    bool multitap = false;
    bool tape = false;

    switch (slot->type_id) {
        case 50u: // Delay
            time_ms = clampf(slot->param_values[0], 1.0f, 2000.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            break;
        case 51u: // PingPong
            time_ms = clampf(slot->param_values[0], 1.0f, 2000.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            pingpong = true;
            break;
        case 52u: // Echo
            time_ms = clampf(slot->param_values[0], 1.0f, 1500.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.9f);
            break;
        case 53u: // Tape
            time_ms = clampf(slot->param_values[0], 20.0f, 1500.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.97f);
            wobble_ms = clampf(slot->param_values[4], 0.0f, 12.0f);
            tape = true;
            break;
        case 54u: // Multitap
            time_ms = clampf(slot->param_values[0], 20.0f, 800.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            diffusion = clampf(slot->param_values[2], 0.0f, 0.9f);
            multitap = true;
            break;
        default:
            break;
    }

    float total_time = time_ms * 0.001f * 8.0f;
    if (total_time < 0.6f) total_time = 0.6f;
    if (total_time > 4.0f) total_time = 4.0f;

    float base_time = time_ms * 0.001f;
    {
        float* mins = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        float* max_main = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        float* max_dim = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        KitVizVecSegment* seg_main = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
        KitVizVecSegment* seg_dim = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
        if (mins && max_main && max_dim && seg_main && seg_dim) {
            int max_repeats_accum = 12;
            for (int i = 0; i < max_repeats_accum; ++i) {
                float echo_time = base_time * (float)(i + 1);
                if (echo_time > total_time) {
                    break;
                }
                float amp = preview_delay_amp(feedback, i);
                if (amp < 0.05f) {
                    break;
                }
                int ix = (int)lroundf((echo_time / total_time) * (float)(plot_rect.w - 1));
                if (ix >= 0 && ix < plot_rect.w && amp > max_main[ix]) {
                    max_main[ix] = amp;
                }
                if (pingpong) {
                    float amp_dim = amp * 0.8f;
                    if (ix >= 0 && ix < plot_rect.w && amp_dim > max_dim[ix]) {
                        max_dim[ix] = amp_dim;
                    }
                }
                if (tape && wobble_ms > 0.1f) {
                    float offset = wobble_ms * 0.001f;
                    float x2 = (echo_time + offset) / total_time;
                    if (x2 <= 1.0f) {
                        int ix2 = (int)lroundf(x2 * (float)(plot_rect.w - 1));
                        if (ix2 >= 0 && ix2 < plot_rect.w && amp > max_dim[ix2]) {
                            max_dim[ix2] = amp;
                        }
                    }
                }
                if (diffusion > 0.01f) {
                    int smear = (int)lroundf(amp * diffusion * 4.0f);
                    for (int s = 1; s <= smear; ++s) {
                        int ixs = ix + s;
                        if (ixs >= plot_rect.w) {
                            break;
                        }
                        float amp_dim = amp * 0.5f;
                        if (amp_dim > max_dim[ixs]) {
                            max_dim[ixs] = amp_dim;
                        }
                    }
                }
            }

            if (multitap) {
                float tap2 = clampf(slot->param_values[3], 0.0f, 4.0f);
                float tap3 = clampf(slot->param_values[4], 0.0f, 4.0f);
                float tap4 = clampf(slot->param_values[5], 0.0f, 4.0f);
                float tap1_gain = clampf(slot->param_values[6], 0.0f, 1.0f);
                float tap2_gain = clampf(slot->param_values[7], 0.0f, 1.0f);
                float tap3_gain = clampf(slot->param_values[8], 0.0f, 1.0f);
                float tap4_gain = clampf(slot->param_values[9], 0.0f, 1.0f);
                float taps[4] = {1.0f, tap2, tap3, tap4};
                float gains[4] = {tap1_gain, tap2_gain, tap3_gain, tap4_gain};
                for (int i = 0; i < 4; ++i) {
                    float tap_time = base_time * taps[i];
                    if (tap_time <= 0.0f || tap_time > total_time) {
                        continue;
                    }
                    int ix = (int)lroundf((tap_time / total_time) * (float)(plot_rect.w - 1));
                    if (ix >= 0 && ix < plot_rect.w && gains[i] > max_dim[ix]) {
                        max_dim[ix] = gains[i];
                    }
                }
            }

            size_t seg_count_main = 0;
            size_t seg_count_dim = 0;
            CoreResult main_r = daw_kit_viz_plot_envelope_from_min_max(mins,
                                                                        max_main,
                                                                        (uint32_t)plot_rect.w,
                                                                        &plot_rect,
                                                                        (DawKitVizPlotRange){0.0f, 1.0f},
                                                                        seg_main,
                                                                        (size_t)plot_rect.w,
                                                                        &seg_count_main);
            CoreResult dim_r = daw_kit_viz_plot_envelope_from_min_max(mins,
                                                                       max_dim,
                                                                       (uint32_t)plot_rect.w,
                                                                       &plot_rect,
                                                                       (DawKitVizPlotRange){0.0f, 1.0f},
                                                                       seg_dim,
                                                                       (size_t)plot_rect.w,
                                                                       &seg_count_dim);
            if (main_r.code == CORE_OK && dim_r.code == CORE_OK) {
                daw_kit_viz_render_segments(renderer, seg_main, seg_count_main, line);
                daw_kit_viz_render_segments(renderer, seg_dim, seg_count_dim, line_dim);
                free(seg_dim);
                free(seg_main);
                free(max_dim);
                free(max_main);
                free(mins);
                return;
            }
        }
        free(seg_dim);
        free(seg_main);
        free(max_dim);
        free(max_main);
        free(mins);
    }

    int max_repeats = 12;
    for (int i = 0; i < max_repeats; ++i) {
        float echo_time = base_time * (float)(i + 1);
        if (echo_time > total_time) {
            break;
        }
        float amp = preview_delay_amp(feedback, i);
        if (amp < 0.05f) {
            break;
        }
        float x_norm = echo_time / total_time;
        int px = plot_rect.x + (int)lroundf(x_norm * (float)plot_rect.w);
        int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
        SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
        SDL_RenderDrawLine(renderer, px, base_y, px, py);

        if (pingpong) {
            int py_alt = base_y - (int)lroundf(amp * 0.8f * (float)plot_rect.h);
            SDL_SetRenderDrawColor(renderer, line_dim.r, line_dim.g, line_dim.b, line_dim.a);
            SDL_RenderDrawLine(renderer, px, base_y, px, py_alt);
        }
        if (tape && wobble_ms > 0.1f) {
            float offset = wobble_ms * 0.001f;
            float x2 = (echo_time + offset) / total_time;
            if (x2 <= 1.0f) {
                int px2 = plot_rect.x + (int)lroundf(x2 * (float)plot_rect.w);
                SDL_SetRenderDrawColor(renderer, line_dim.r, line_dim.g, line_dim.b, line_dim.a);
                SDL_RenderDrawLine(renderer, px2, base_y, px2, py);
            }
        }
        if (diffusion > 0.01f) {
            int smear = (int)lroundf(amp * diffusion * 4.0f);
            for (int s = 1; s <= smear; ++s) {
                int pxs = px + s;
                if (pxs >= plot_rect.x + plot_rect.w) {
                    break;
                }
                SDL_SetRenderDrawColor(renderer, line_dim.r, line_dim.g, line_dim.b, line_dim.a);
                SDL_RenderDrawLine(renderer, pxs, base_y, pxs, base_y - (py - base_y) / 2);
            }
        }
    }

    if (multitap) {
        float tap2 = clampf(slot->param_values[3], 0.0f, 4.0f);
        float tap3 = clampf(slot->param_values[4], 0.0f, 4.0f);
        float tap4 = clampf(slot->param_values[5], 0.0f, 4.0f);
        float tap1_gain = clampf(slot->param_values[6], 0.0f, 1.0f);
        float tap2_gain = clampf(slot->param_values[7], 0.0f, 1.0f);
        float tap3_gain = clampf(slot->param_values[8], 0.0f, 1.0f);
        float tap4_gain = clampf(slot->param_values[9], 0.0f, 1.0f);
        float taps[4] = {1.0f, tap2, tap3, tap4};
        float gains[4] = {tap1_gain, tap2_gain, tap3_gain, tap4_gain};
        for (int i = 0; i < 4; ++i) {
            float tap_time = base_time * taps[i];
            if (tap_time <= 0.0f || tap_time > total_time) {
                continue;
            }
            float amp = gains[i];
            int px = plot_rect.x + (int)lroundf((tap_time / total_time) * (float)plot_rect.w);
            int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
            SDL_SetRenderDrawColor(renderer, line_dim.r, line_dim.g, line_dim.b, line_dim.a);
            SDL_RenderDrawLine(renderer, px, base_y, px, py);
        }
    }
}

// effects_slot_preview_draw_toggle draws the preview toggle footer button.
static void effects_slot_preview_draw_toggle(SDL_Renderer* renderer,
                                             const SDL_Rect* rect,
                                             bool open,
                                             SDL_Color label_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {40, 44, 54, 255};
    SDL_Color border = {80, 85, 100, 200};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    const char* label = open ? "Hide Preview" : "Show Preview";
    int text_y = rect->y + (rect->h - ui_font_line_height(1.0f)) / 2;
    ui_draw_text(renderer, rect->x + 6, text_y, label, label_color, 1.0f);
}

// effects_slot_preview_render draws the preview panel and toggle for a slot.
void effects_slot_preview_render(SDL_Renderer* renderer,
                                 const FxSlotUIState* slot,
                                 EffectsPanelPreviewSlotState* preview,
                                 const SDL_Rect* preview_rect,
                                 const SDL_Rect* toggle_rect,
                                 SDL_Color label_color,
                                 SDL_Color text_dim,
                                 float sample_rate,
                                 bool have_gr,
                                 float gr_db) {
    if (!renderer || !slot || !preview || !preview_rect || !toggle_rect) {
        return;
    }
    if (preview_rect->w <= 0 || preview_rect->h <= 0) {
        return;
    }

    EffectsPreviewMode preview_mode = effects_slot_preview_mode(slot->type_id);
    if (preview_mode == FX_PREVIEW_HISTORY_GR || preview_mode == FX_PREVIEW_HISTORY_TRIM) {
        effects_slot_preview_push(preview, have_gr ? gr_db : 0.0f);
    }

    if (preview->open) {
        SDL_Rect prev_clip;
        SDL_bool had_clip = ui_clip_is_enabled(renderer);
        ui_get_clip_rect(renderer, &prev_clip);
        ui_set_clip_rect(renderer, preview_rect);
        if (preview_mode == FX_PREVIEW_CURVE) {
            effects_slot_preview_render_curve(renderer,
                                              slot,
                                              preview_rect,
                                              label_color,
                                              "Preview");
        } else if (preview_mode == FX_PREVIEW_EQ) {
            effects_slot_preview_render_eq(renderer,
                                           slot,
                                           preview_rect,
                                           label_color,
                                           sample_rate,
                                           "Preview");
        } else if (preview_mode == FX_PREVIEW_LFO) {
            effects_slot_preview_render_lfo(renderer,
                                            slot,
                                            preview_rect,
                                            label_color,
                                            "Preview");
        } else if (preview_mode == FX_PREVIEW_REVERB) {
            effects_slot_preview_render_reverb(renderer,
                                               slot,
                                               preview_rect,
                                               label_color,
                                               "Preview");
        } else if (preview_mode == FX_PREVIEW_DELAY) {
            effects_slot_preview_render_delay(renderer,
                                              slot,
                                              preview_rect,
                                              label_color,
                                              "Preview");
        } else {
            EffectsPreviewStyle preview_style;
            effects_slot_preview_style(slot->type_id, &preview_style);
            effects_slot_preview_render_history(renderer,
                                                preview,
                                                preview_rect,
                                                label_color,
                                                text_dim,
                                                "Preview",
                                                &preview_style);
        }
        ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
    } else {
        SDL_Color border = {80, 85, 100, 200};
        SDL_SetRenderDrawColor(renderer, 30, 32, 40, 255);
        SDL_RenderFillRect(renderer, preview_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, preview_rect);
        ui_draw_text(renderer,
                     preview_rect->x + 6,
                     preview_rect->y + 2,
                     "Preview (collapsed)",
                     text_dim,
                     0.9f);
    }

    effects_slot_preview_draw_toggle(renderer, toggle_rect, preview->open, label_color);
}
