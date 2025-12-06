
// fx_limiter.c — Brickwall-style limiter with short lookahead (interleaved, in-place)
// Params:
//   0: ceiling_dB   (-24..0)
//   1: lookahead_ms (0..3)        // 0 works but may clip on sudden peaks
//   2: release_ms   (5..200)
// Notes:
// - Uses per-channel envelope with shared gain reduction (linking) by taking max across channels per frame.
// - Lookahead implemented with circular buffer per channel (allocated at create).
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB * 0.05f); }

typedef struct FxLimiter {
    float sr;
    unsigned max_channels;

    float ceiling_dB;
    float look_ms;
    float release_ms;

    unsigned look_samples;
    float*   buf;       // size = look_samples * channels
    unsigned write_pos;

    float gain; // current shared gain (linear)
} FxLimiter;

static float* chan_ptr(struct FxLimiter* L, unsigned ch){ return L->buf + (size_t)ch * (size_t)L->look_samples; }

static void limiter_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxLimiter* L = (FxLimiter*)h;
    switch (idx) {
        case 0: L->ceiling_dB = clampf(value, -24.f, 0.f); break;
        case 1: L->look_ms    = clampf(value, 0.0f, 3.0f);
                L->look_samples = (unsigned)(L->look_ms * 0.001f * L->sr + 0.5f);
                if (L->look_samples < 1) L->look_samples = 1; // minimum 1-sample pipeline
                break;
        case 2: L->release_ms = clampf(value, 5.f, 200.f); break;
        default: break;
    }
}

static void limiter_reset(FxHandle* h)
{
    FxLimiter* L = (FxLimiter*)h;
    if (L->buf) memset(L->buf, 0, (size_t)L->look_samples * (size_t)L->max_channels * sizeof(float));
    L->write_pos = 0;
    L->gain = 1.0f;
}

static void limiter_destroy(FxHandle* h)
{
    FxLimiter* L = (FxLimiter*)h;
    free(L->buf);
    free(L);
}

static void limiter_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxLimiter* L = (FxLimiter*)h;
    if (channels > (int)L->max_channels) channels = (int)L->max_channels;

    const float ceil_lin = dB_to_lin(L->ceiling_dB);
    const float rel_a = expf(-1.0f / ( (L->release_ms * 0.001f) * L->sr ));

    for (int n = 0; n < frames; ++n) {
        unsigned w = L->write_pos;
        unsigned r = (w + L->look_samples - 1) % L->look_samples; // read oldest (lookahead)

        // write current sample to delay buffers
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(L, (unsigned)ch);
            cb[w] = out[base + ch];
        }

        // peek future (actually delayed) level and compute needed gain
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(L, (unsigned)ch);
            float s = fabsf(cb[r]);
            if (s > peak) peak = s;
        }
        float needed = (peak > 1e-12f) ? (ceil_lin / peak) : 1.0f;
        if (needed > 1.0f) needed = 1.0f;

        // gain follows the minimum (attack instant), releases exponentially
        if (needed < L->gain) {
            L->gain = needed;
        } else {
            L->gain = L->gain * rel_a + (1.0f - rel_a) * needed;
        }

        // output the delayed sample with shared gain at read index
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(L, (unsigned)ch);
            float y = cb[r] * L->gain;
            out[base + ch] = y;
        }

        L->write_pos = (L->write_pos + 1) % L->look_samples;
    }
}

int limiter_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Limiter";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0] = "ceiling_dB";
    out->param_names[1] = "lookahead_ms";
    out->param_names[2] = "release_ms";
    out->param_defaults[0] = -0.3f;
    out->param_defaults[1] = 1.0f;
    out->param_defaults[2] = 50.0f;
    out->latency_samples = 0; // lookahead is internal; throughput latency equals lookahead, but engine can ignore for now
    return 1;
}

int limiter_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxLimiter* L = (FxLimiter*)calloc(1, sizeof(FxLimiter));
    if (!L) return 0;

    L->sr = (float)sample_rate;
    L->max_channels = max_channels ? max_channels : 2;

    // defaults
    L->ceiling_dB = -0.3f;
    L->look_ms = 1.0f;
    L->release_ms = 50.0f;
    L->look_samples = (unsigned)(L->look_ms * 0.001f * L->sr + 0.5f);
    if (L->look_samples < 1) L->look_samples = 1;

    size_t total = (size_t)L->look_samples * (size_t)L->max_channels;
    L->buf = (float*)calloc(total, sizeof(float));
    if (!L->buf) { limiter_destroy((FxHandle*)L); return 0; }

    limiter_reset((FxHandle*)L);

    out_vt->process = limiter_process;
    out_vt->set_param = limiter_set_param;
    out_vt->reset = limiter_reset;
    out_vt->destroy = limiter_destroy;
    *out_handle = (FxHandle*)L;
    return 1;
}
