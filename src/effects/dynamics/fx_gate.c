
// fx_gate.c — Noise gate / downward expander (interleaved, in-place)
// Params:
//   0: threshold_dB (-90..0)
//   1: ratio        (1..8)     // 1=gate (infinite), >1 downward expansion
//   2: attack_ms    (0.1..50)
//   3: release_ms   (5..500)
//   4: hold_ms      (0..200)
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB * 0.05f); }
static inline float lin_to_dB(float lin){ return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

typedef struct FxGate {
    float sr;
    unsigned max_channels;

    float thresh_dB;
    float ratio;
    float attack_ms;
    float release_ms;
    float hold_ms;

    // state
    float *env;       // per-channel detector
    float *gain;      // per-channel applied gain
    int   *hold_cnt;  // samples remaining in hold
} FxGate;

static void gate_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxGate* g = (FxGate*)h;
    switch (idx) {
        case 0: g->thresh_dB = clampf(value, -90.f, 0.f); break;
        case 1: g->ratio     = clampf(value, 1.f, 8.f);   break;
        case 2: g->attack_ms = clampf(value, 0.1f, 50.f); break;
        case 3: g->release_ms= clampf(value, 5.f, 500.f); break;
        case 4: g->hold_ms   = clampf(value, 0.f, 200.f); break;
        default: break;
    }
}

static void gate_reset(FxHandle* h)
{
    FxGate* g = (FxGate*)h;
    for (unsigned ch = 0; ch < g->max_channels; ++ch) {
        g->env[ch] = 0.0f;
        g->gain[ch] = 1.0f;
        g->hold_cnt[ch] = 0;
    }
}

static void gate_destroy(FxHandle* h)
{
    FxGate* g = (FxGate*)h;
    free(g->env);
    free(g->gain);
    free(g->hold_cnt);
    free(g);
}

static void gate_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxGate* g = (FxGate*)h;
    if (channels > (int)g->max_channels) channels = (int)g->max_channels;

    const float atk_a = expf(-1.0f / ((g->attack_ms  * 0.001f) * g->sr));
    const float rel_a = expf(-1.0f / ((g->release_ms * 0.001f) * g->sr));
    const int hold_samp = (int)(g->hold_ms * 0.001f * g->sr + 0.5f);
    const float thr = g->thresh_dB;

    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float level = fabsf(x);
            // simple peak follower
            float prev = g->env[ch];
            float coef = (level > prev) ? atk_a : rel_a;
            float env = coef*prev + (1.0f - coef)*level;
            g->env[ch] = env;

            float in_dB = lin_to_dB(env + 1e-12f);

            float target_gain = 1.0f;
            if (in_dB < thr) {
                // downward expansion: reduce gain; ratio controls slope
                float diff = thr - in_dB;
                float atten_dB = diff * (g->ratio - 1.0f); // more diff => more attenuation
                target_gain = dB_to_lin(-atten_dB);
            } else {
                // above threshold: open
                target_gain = 1.0f;
                g->hold_cnt[ch] = hold_samp; // reset hold when signal above threshold
            }

            // hold: if in hold, keep at last gain or recover towards 1 slowly
            if (g->hold_cnt[ch] > 0) {
                g->hold_cnt[ch]--;
                if (in_dB < thr) {
                    // still under threshold but holding — do nothing (keep current gain)
                    target_gain = fmaxf(target_gain, g->gain[ch]);
                }
            }

            // smooth gain changes (attack towards lower gain fast, release towards 1 slow)
            float gprev = g->gain[ch];
            float alpha = (target_gain < gprev) ? (1.0f - atk_a) : (1.0f - rel_a);
            float gnew = gprev + alpha * (target_gain - gprev);
            g->gain[ch] = gnew;

            out[base + ch] = x * gnew;
        }
    }
}

int gate_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Gate";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 5;
    out->param_names[0] = "threshold_dB";
    out->param_names[1] = "ratio";
    out->param_names[2] = "attack_ms";
    out->param_names[3] = "release_ms";
    out->param_names[4] = "hold_ms";
    out->param_defaults[0] = -50.0f;
    out->param_defaults[1] = 4.0f;
    out->param_defaults[2] = 3.0f;
    out->param_defaults[3] = 120.0f;
    out->param_defaults[4] = 20.0f;
    out->latency_samples = 0;
    return 1;
}

int gate_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxGate* g = (FxGate*)calloc(1, sizeof(FxGate));
    if (!g) return 0;

    g->sr = (float)sample_rate;
    g->max_channels = max_channels ? max_channels : 2;

    g->env = (float*)calloc(g->max_channels, sizeof(float));
    g->gain = (float*)calloc(g->max_channels, sizeof(float));
    g->hold_cnt = (int*)calloc(g->max_channels, sizeof(int));
    if (!g->env || !g->gain || !g->hold_cnt) {
        gate_destroy((FxHandle*)g);
        return 0;
    }

    // defaults
    g->thresh_dB = -50.0f;
    g->ratio = 4.0f;
    g->attack_ms = 3.0f;
    g->release_ms = 120.0f;
    g->hold_ms = 20.0f;

    gate_reset((FxHandle*)g);

    out_vt->process = gate_process;
    out_vt->set_param = gate_set_param;
    out_vt->reset = gate_reset;
    out_vt->destroy = gate_destroy;
    *out_handle = (FxHandle*)g;
    return 1;
}
