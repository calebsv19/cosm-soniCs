
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
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB * 0.05f); }
static inline float lin_to_dB(float lin){ return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

typedef struct FxSCComp {
    float sr;
    unsigned max_channels;

    float thresh_dB, ratio, attack_ms, release_ms, makeup_dB, knee_dB;
    int   detector_mode; // 0 peak, 1 rms

    // single shared envelope for linking
    float env;
} FxSCComp;

static inline float comp_gain_db(float in_dB, float thr_dB, float ratio, float knee_dB)
{
    if (knee_dB <= 1e-6f) {
        if (in_dB <= thr_dB) return 0.0f;
        return (thr_dB + (in_dB - thr_dB)/ratio) - in_dB;
    } else {
        float halfK = knee_dB * 0.5f;
        if (in_dB < thr_dB - halfK) return 0.0f;
        if (in_dB > thr_dB + halfK) return (thr_dB + (in_dB - thr_dB)/ratio) - in_dB;
        float x = in_dB - (thr_dB - halfK);
        float y = x * x / (knee_dB * 4.0f);
        float target = in_dB + ((1.0f/ratio - 1.0f) * y);
        return target - in_dB;
    }
}

static void sccomp_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxSCComp* c = (FxSCComp*)h;
    switch (idx) {
        case 0: c->thresh_dB = clampf(value, -60.f, 0.f); break;
        case 1: c->ratio     = clampf(value, 1.f, 20.f); break;
        case 2: c->attack_ms = clampf(value, 0.1f, 100.f); break;
        case 3: c->release_ms= clampf(value, 5.f, 500.f); break;
        case 4: c->makeup_dB = clampf(value, -24.f, 24.f); break;
        case 5: c->knee_dB   = clampf(value, 0.f, 24.f); break;
        case 6: c->detector_mode = (value >= 0.5f) ? 1 : 0; break;
        default: break;
    }
}

static void sccomp_reset(FxHandle* h)
{
    FxSCComp* c = (FxSCComp*)h;
    c->env = 0.0f;
}

static void sccomp_destroy(FxHandle* h)
{
    free(h);
}

static inline float detect_level(FxSCComp* c, float sample, float atk_a, float rel_a)
{
    float level = fabsf(sample);
    if (c->detector_mode == 1) {
        // crude RMS via one-pole on squared
        float prev = c->env;
        float alpha = (level > prev) ? (1.0f - atk_a) : (1.0f - rel_a);
        float env_sq = prev*prev + alpha * (sample*sample - prev*prev);
        float env = sqrtf(fmaxf(env_sq, 0.0f));
        c->env = env;
        return env;
    } else {
        float prev = c->env;
        float coef = (level > prev) ? atk_a : rel_a;
        float env = coef*prev + (1.0f - coef)*level;
        c->env = env;
        return env;
    }
}

static void sccomp_apply(FxSCComp* c,
                         const float* key, int key_ch,
                         float* io, int frames, int channels)
{
    const float atk_a = expf(-1.0f / ((c->attack_ms  * 0.001f) * c->sr));
    const float rel_a = expf(-1.0f / ((c->release_ms * 0.001f) * c->sr));
    const float makeup = dB_to_lin(c->makeup_dB);

    for (int n = 0; n < frames; ++n) {
        // derive a mono key sample (average of channels)
        float klev = 0.0f;
        if (key && key_ch > 0) {
            float sum = 0.0f;
            for (int kc = 0; kc < key_ch; ++kc) sum += key[n*key_ch + kc];
            float km = sum / (float)key_ch;
            klev = detect_level(c, km, atk_a, rel_a);
        } else {
            // fallback: use input L channel
            float km = io[n*channels + 0];
            klev = detect_level(c, km, atk_a, rel_a);
        }

        float in_dB = lin_to_dB(fabsf(klev) + 1e-12f);
        float g_db = comp_gain_db(in_dB, c->thresh_dB, c->ratio, c->knee_dB);
        float g = dB_to_lin(g_db) * makeup;

        for (int ch = 0; ch < channels; ++ch) {
            io[n*channels + ch] *= g;
        }
    }
}

static void sccomp_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxSCComp* c = (FxSCComp*)h;
    if (channels < 1) return;
    sccomp_apply(c, NULL, 0, out, frames, channels);
}

static void sccomp_process_sc(FxHandle* h,
                              const float* in,
                              const float* sidechain,
                              float* out,
                              int frames,
                              int channels,
                              int sc_channels)
{
    (void)in;
    FxSCComp* c = (FxSCComp*)h;
    if (sc_channels < 1) { sccomp_apply(c, NULL, 0, out, frames, channels); return; }
    sccomp_apply(c, sidechain, sc_channels, out, frames, channels);
}

int sccomp_get_desc(FxDesc *out)
{
    if (!out) return 0;
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

int sccomp_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                  uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxSCComp* c = (FxSCComp*)calloc(1, sizeof(FxSCComp));
    if (!c) return 0;
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

    out_vt->process    = sccomp_process;
    out_vt->process_sc = sccomp_process_sc;
    out_vt->set_param  = sccomp_set_param;
    out_vt->reset      = sccomp_reset;
    out_vt->destroy    = sccomp_destroy;

    *out_handle = (FxHandle*)c;
    return 1;
}
