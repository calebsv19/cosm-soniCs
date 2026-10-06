
// fx_sidechain_compressor.c — Compressor keyed by optional sidechain input
// If process_sc() is invoked with a sidechain buffer, detector uses it.
// Otherwise behaves like a regular compressor on the input.
// Params:
//   0: threshold_dB  (-60..0)
//   1: ratio         (1..20)
//   2: attack_ms     (0.1..100)
//   3: release_ms    (5..500)
//   4: makeup_dB     (-24..24)
//   5: knee_dB       (0..24)
//   6: detector      (0=peak,1=rms)
#include "effects/dynamics_math.h"
#include "effects/effects_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Bounds finite controls to their supported interval.
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
// Converts amplitude decibels to linear gain.
static inline float dB_to_lin(float dB) { return powf(10.0f, dB * 0.05f); }
// Converts a positive amplitude to decibels.
static inline float lin_to_dB(float lin) { return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

// Stores the existing linked key detector, controls, and applied reduction
// telemetry.
typedef struct FxSCComp {
    float sr;
    unsigned max_channels;

    float thresh_dB, ratio, attack_ms, release_ms, makeup_dB, knee_dB;
    int detector_mode; // 0 peak, 1 rms

    // single shared envelope for linking
    float env;
    float reduction_db;
} FxSCComp;

// Applies finite compressor controls without allocating.
static void sccomp_set_param(FxHandle* h, uint32_t idx, float value) {
    FxSCComp* c = (FxSCComp*)h;
    if (!isfinite(value))
        return;
    switch (idx) {
    case 0:
        c->thresh_dB = clampf(value, -60.f, 0.f);
        break;
    case 1:
        c->ratio = clampf(value, 1.f, 20.f);
        break;
    case 2:
        c->attack_ms = clampf(value, 0.1f, 100.f);
        break;
    case 3:
        c->release_ms = clampf(value, 5.f, 500.f);
        break;
    case 4:
        c->makeup_dB = clampf(value, -24.f, 24.f);
        break;
    case 5:
        c->knee_dB = clampf(value, 0.f, 24.f);
        break;
    case 6:
        c->detector_mode = (value >= 0.5f) ? 1 : 0;
        break;
    default:
        break;
    }
}

// Clears the linked detector and reduction history.
static void sccomp_reset(FxHandle* h) {
    FxSCComp* c = (FxSCComp*)h;
    c->env = 0.0f;
    c->reduction_db = 0;
}

// Releases the prepared instance.
static void sccomp_destroy(FxHandle* h) { free(h); }

// Advances the existing peak or RMS key envelope.
static inline float detect_level(FxSCComp* c, float sample, float atk_a, float rel_a) {
    if (!isfinite(sample))
        sample = 0;
    float level = fabsf(sample);
    if (c->detector_mode == 1) {
        // crude RMS via one-pole on squared
        float prev = c->env;
        float alpha = (level > prev) ? (1.0f - atk_a) : (1.0f - rel_a);
        double env_sq = (double)prev * prev + alpha * ((double)sample * sample - (double)prev * prev);
        float env = (float)sqrt(fmax(env_sq, 0.0));
        c->env = env;
        return env;
    } else {
        float prev = c->env;
        float coef = (level > prev) ? atk_a : rel_a;
        float env = coef * prev + (1.0f - coef) * level;
        c->env = env;
        return env;
    }
}

// Applies the existing key reduction equally to every output channel.
static void sccomp_apply(FxSCComp* c, const float* key, int key_ch, float* io, int frames, int channels) {
    c->reduction_db = 0;
    const float atk_a = expf(-1.0f / ((c->attack_ms * 0.001f) * c->sr));
    const float rel_a = expf(-1.0f / ((c->release_ms * 0.001f) * c->sr));
    const float makeup = dB_to_lin(c->makeup_dB);

    for (int n = 0; n < frames; ++n) {
        // derive a mono key sample (average of channels)
        float klev = 0.0f;
        if (key && key_ch > 0) {
            float sum = 0.0f;
            for (int kc = 0; kc < key_ch; ++kc)
                sum += key[n * key_ch + kc];
            float km = sum / (float)key_ch;
            klev = detect_level(c, km, atk_a, rel_a);
        } else {
            // fallback: use input L channel
            float km = io[n * channels + 0];
            klev = detect_level(c, km, atk_a, rel_a);
        }

        float in_dB = lin_to_dB(fabsf(klev) + 1e-12f);
        float g_db = fx_compressor_gain_db(in_dB, c->thresh_dB, c->ratio, c->knee_dB);
        c->reduction_db = fminf(c->reduction_db, g_db);
        float g = dB_to_lin(g_db) * makeup;

        for (int ch = 0; ch < channels; ++ch) {
            io[n * channels + ch] *= g;
        }
    }
}

// Processes the existing left-input-keyed fallback path.
static void sccomp_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    FxSCComp* c = (FxSCComp*)h;
    if (!in || !out || frames <= 0 || channels <= 0 || channels > (int)c->max_channels)
        return;
    if (in != out)
        memcpy(out, in, (size_t)frames * channels * sizeof(float));
    sccomp_apply(c, NULL, 0, out, frames, channels);
}

// Processes the optional explicitly supplied key buffer.
static void sccomp_process_sc(FxHandle* h, const float* in, const float* sidechain, float* out, int frames,
                              int channels, int sc_channels) {
    FxSCComp* c = (FxSCComp*)h;
    if (!in || !out || frames <= 0 || channels <= 0 || channels > (int)c->max_channels)
        return;
    if (in != out)
        memcpy(out, in, (size_t)frames * channels * sizeof(float));
    if (sc_channels < 1) {
        sccomp_apply(c, NULL, 0, out, frames, channels);
        return;
    }
    sccomp_apply(c, sidechain, sc_channels, out, frames, channels);
}

// Reports the most negative linked gain applied in the last block before
// makeup.
static float sccomp_reduction(FxHandle* h) { return ((FxSCComp*)h)->reduction_db; }

// Describes the existing sidechain-capable compressor controls.
int sccomp_get_desc(FxDesc* out) {
    if (!out)
        return 0;
    out->name = "SidechainCompressor";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 2; // sidechain-capable
    out->num_outputs = 1;
    out->num_params = 7;
    out->param_names[0] = "threshold_dB";
    out->param_names[1] = "ratio";
    out->param_names[2] = "attack_ms";
    out->param_names[3] = "release_ms";
    out->param_names[4] = "makeup_dB";
    out->param_names[5] = "knee_dB";
    out->param_names[6] = "detector";
    out->param_defaults[0] = -18.0f;
    out->param_defaults[1] = 4.0f;
    out->param_defaults[2] = 5.0f;
    out->param_defaults[3] = 80.0f;
    out->param_defaults[4] = 0.0f;
    out->param_defaults[5] = 6.0f;
    out->param_defaults[6] = 1.0f; // rms
    out->latency_samples = 0;
    return 1;
}

// Prepares one linked detector before rendering.
int sccomp_create(const FxDesc* desc, FxHandle** out_handle, FxVTable* out_vt, uint32_t sample_rate,
                  uint32_t max_block, uint32_t max_channels) {
    (void)desc;
    (void)max_block;
    if (!sample_rate || !out_handle || !out_vt)
        return 0;
    FxSCComp* c = (FxSCComp*)calloc(1, sizeof(FxSCComp));
    if (!c)
        return 0;
    c->sr = (float)sample_rate;
    c->max_channels = max_channels ? max_channels : 2;

    // defaults
    c->thresh_dB = -18.0f;
    c->ratio = 4.0f;
    c->attack_ms = 5.0f;
    c->release_ms = 80.0f;
    c->makeup_dB = 0.0f;
    c->knee_dB = 6.0f;
    c->detector_mode = 1;
    c->env = 0.0f;
    c->reduction_db = 0;

    *out_vt = (FxVTable){0};
    out_vt->gain_reduction_db = sccomp_reduction;
    out_vt->process = sccomp_process;
    out_vt->process_sc = sccomp_process_sc;
    out_vt->set_param = sccomp_set_param;
    out_vt->reset = sccomp_reset;
    out_vt->destroy = sccomp_destroy;

    *out_handle = (FxHandle*)c;
    return 1;
}
