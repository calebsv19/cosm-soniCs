
// fx_compressor.c — Feed-forward compressor (interleaved, in-place)
// Params:
//   0: threshold_dB  (-60..0)
//   1: ratio         (1..20)
//   2: attack_ms     (0.1..100)
//   3: release_ms    (5..500)
//   4: makeup_dB     (-24..24)
//   5: knee_dB       (0..24)  // 0 = hard knee
//   6: detector      (0=peak,1=rms)
// Implementation notes:
// - One detector/envelope per channel.
// - Detector is RT-safe; no allocations in process().
// - Gain computer uses standard soft-knee curve.
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB * 0.05f); }    // 10^(dB/20)
static inline float lin_to_dB(float lin){ return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

typedef struct FxCompressor {
    // config
    float sr;
    unsigned max_channels;
    // params
    float thresh_dB;
    float ratio;
    float attack_ms;
    float release_ms;
    float makeup_dB;
    float knee_dB;
    int   detector_mode; // 0 peak, 1 rms
    // state
    float *env;   // detector envelope per channel
} FxCompressor;

static void comp_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxCompressor* c = (FxCompressor*)h;
    switch (idx) {
        case 0: c->thresh_dB = clampf(value, -60.f, 0.f); break;
        case 1: c->ratio     = clampf(value, 1.f, 20.f);  break;
        case 2: c->attack_ms = clampf(value, 0.1f, 100.f); break;
        case 3: c->release_ms= clampf(value, 5.f, 500.f); break;
        case 4: c->makeup_dB = clampf(value, -24.f, 24.f); break;
        case 5: c->knee_dB   = clampf(value, 0.f, 24.f);  break;
        case 6: c->detector_mode = (value >= 0.5f) ? 1 : 0; break;
        default: break;
    }
}

static void comp_reset(FxHandle* h)
{
    FxCompressor* c = (FxCompressor*)h;
    for (unsigned ch = 0; ch < c->max_channels; ++ch) c->env[ch] = 0.0f;
}

static void comp_destroy(FxHandle* h)
{
    FxCompressor* c = (FxCompressor*)h;
    free(c->env);
    free(c);
}

// Soft-knee gain computer: returns gain linear
static inline float comp_gain_db(float in_dB, float thr_dB, float ratio, float knee_dB)
{
    if (knee_dB <= 1e-6f) {
        // hard knee
        if (in_dB <= thr_dB) return 0.0f; // 0 dB gain change
        return (thr_dB + (in_dB - thr_dB)/ratio) - in_dB; // negative dB
    } else {
        float halfK = knee_dB * 0.5f;
        if (in_dB < thr_dB - halfK) {
            return 0.0f;
        } else if (in_dB > thr_dB + halfK) {
            return (thr_dB + (in_dB - thr_dB)/ratio) - in_dB;
        } else {
            // within knee region: quadratic interpolation per classic design
            float x = in_dB - (thr_dB - halfK); // 0..knee
            float y = x * x / (knee_dB * 4.0f); // 0..knee/4
            float target = in_dB + ( (1.0f/ratio - 1.0f) * y );
            return target - in_dB;
        }
    }
}

static void comp_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxCompressor* c = (FxCompressor*)h;
    if (channels > (int)c->max_channels) channels = (int)c->max_channels;

    // attack/release envelopes
    const float atk_a = expf(-1.0f / ( (c->attack_ms  * 0.001f) * c->sr ));
    const float rel_a = expf(-1.0f / ( (c->release_ms * 0.001f) * c->sr ));
    const float makeup = dB_to_lin(c->makeup_dB);

    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float level = fabsf(x);
            if (c->detector_mode == 1) {
                // RMS: cheap one-pole on squared, sqrt after
                float prev = c->env[ch];
                float sq = x * x;
                float alpha = (level > prev) ? (1.0f - atk_a) : (1.0f - rel_a);
                float env_sq = prev*prev + alpha*(sq - prev*prev);
                float env = sqrtf(fmaxf(env_sq, 0.0f));
                c->env[ch] = env;
                level = env;
            } else {
                // Peak: one-pole rectifier
                float prev = c->env[ch];
                float coef = (level > prev) ? atk_a : rel_a;
                float env = coef*prev + (1.0f - coef)*level;
                c->env[ch] = env;
                level = env;
            }

            float in_dB = lin_to_dB(level + 1e-12f);
            float g_db = comp_gain_db(in_dB, c->thresh_dB, c->ratio, c->knee_dB);
            float gain = dB_to_lin(g_db) * makeup;
            out[base + ch] = x * gain;
        }
    }
}

int compressor_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Compressor";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
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

int compressor_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                      uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxCompressor* c = (FxCompressor*)calloc(1, sizeof(FxCompressor));
    if (!c) return 0;
    c->sr = (float)sample_rate;
    c->max_channels = max_channels ? max_channels : 2;

    c->env = (float*)calloc(c->max_channels, sizeof(float));
    if (!c->env) { free(c); return 0; }

    // defaults
    c->thresh_dB = -18.0f;
    c->ratio = 4.0f;
    c->attack_ms = 5.0f;
    c->release_ms = 80.0f;
    c->makeup_dB = 0.0f;
    c->knee_dB = 6.0f;
    c->detector_mode = 1;

    comp_reset((FxHandle*)c);

    out_vt->process = comp_process;
    out_vt->set_param = comp_set_param;
    out_vt->reset = comp_reset;
    out_vt->destroy = comp_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
