
// fx_chorus.c — 2-voice chorus per channel (interleaved, in-place)
// Params:
//   0: rate_hz    (0.05..5)
//   1: depth_ms   (0..15)
//   2: base_ms    (5..25)    — base delay
//   3: mix        (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float fracf(float x){ return x - floorf(x); }
static inline float lerp(float a, float b, float t){ return a + (b - a) * t; }

typedef struct FxChorus {
    float sr;
    unsigned max_channels;

    // params
    float rate_hz;
    float depth_ms;
    float base_ms;
    float mix;

    // state
    float phase;
    // delay buffer per channel (100 ms safety)
    unsigned max_delay;
    float*   buf;     // size = max_delay * max_channels
    unsigned write_pos;
} FxChorus;

static float* chan_ptr(FxChorus* c, unsigned ch){ return c->buf + (size_t)ch * c->max_delay; }

static void chorus_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxChorus* c = (FxChorus*)h;
    if (channels > (int)c->max_channels) channels = (int)c->max_channels;

    float phase = c->phase;
    const float inc = c->rate_hz / c->sr;
    const float mix = clampf(c->mix, 0.f, 1.f);
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        // two LFOs 90 degrees apart
        float p0 = phase;
        float p1 = phase + 0.25f; if (p1 >= 1.0f) p1 -= 1.0f;
        float l0 = sinf(2.0f * (float)M_PI * p0);
        float l1 = sinf(2.0f * (float)M_PI * p1);

        float depth_samp = clampf(c->depth_ms, 0.f, 15.f) * 0.001f * c->sr;
        float base_samp  = clampf(c->base_ms,  5.f, 25.f) * 0.001f * c->sr;

        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(c, (unsigned)ch);

            float x = out[base + ch];

            // two modulated taps
            float d0 = base_samp + depth_samp * l0 * 0.5f;
            float d1 = base_samp + depth_samp * l1 * 0.5f;

            // read fractional positions
            unsigned w = c->write_pos;
            float r0 = (float)((w + c->max_delay) % c->max_delay) - d0;
            float r1 = (float)((w + c->max_delay) % c->max_delay) - d1;
            while (r0 < 0) r0 += c->max_delay;
            while (r1 < 0) r1 += c->max_delay;

            unsigned i0 = (unsigned)r0;
            unsigned i1 = (unsigned)r1;
            float t0 = fracf(r0);
            float t1 = fracf(r1);

            unsigned i0n = (i0 + 1) % c->max_delay;
            unsigned i1n = (i1 + 1) % c->max_delay;

            float y0 = lerp(cb[i0], cb[i0n], t0);
            float y1 = lerp(cb[i1], cb[i1n], t1);

            float y = 0.5f*(y0 + y1);
            // write current input
            cb[w] = x;

            out[base + ch] = dry * x + mix * y;
        }

        c->write_pos = (c->write_pos + 1) % c->max_delay;
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
    }
    c->phase = phase;
}

static void chorus_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxChorus* c = (FxChorus*)h;
    switch (idx) {
        case 0: c->rate_hz  = clampf(value, 0.05f, 5.0f); break;
        case 1: c->depth_ms = clampf(value, 0.0f,  15.0f); break;
        case 2: c->base_ms  = clampf(value, 5.0f,  25.0f); break;
        case 3: c->mix      = clampf(value, 0.0f,  1.0f);  break;
        default: break;
    }
}

static void chorus_reset(FxHandle* h)
{
    FxChorus* c = (FxChorus*)h;
    if (c->buf) memset(c->buf, 0, (size_t)c->max_delay * (size_t)c->max_channels * sizeof(float));
    c->write_pos = 0;
    c->phase = 0.0f;
}

static void chorus_destroy(FxHandle* h)
{
    FxChorus* c = (FxChorus*)h;
    free(c->buf);
    free(c);
}

int chorus_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Chorus";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 4;
    out->param_names[0] = "rate_hz";
    out->param_names[1] = "depth_ms";
    out->param_names[2] = "base_ms";
    out->param_names[3] = "mix";
    out->param_defaults[0] = 0.8f;
    out->param_defaults[1] = 6.0f;
    out->param_defaults[2] = 15.0f;
    out->param_defaults[3] = 0.35f;
    out->latency_samples = 0;
    return 1;
}

int chorus_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                  uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxChorus* c = (FxChorus*)calloc(1, sizeof(FxChorus));
    if (!c) return 0;

    c->sr = (float)sample_rate;
    c->max_channels = max_channels ? max_channels : 2;
    c->rate_hz = 0.8f;
    c->depth_ms = 6.0f;
    c->base_ms = 15.0f;
    c->mix = 0.35f;
    c->phase = 0.0f;

    // 100 ms max delay buffer per channel
    c->max_delay = (unsigned)(0.100f * c->sr + 0.5f);
    size_t total = (size_t)c->max_delay * (size_t)c->max_channels;
    c->buf = (float*)calloc(total, sizeof(float));
    if (!c->buf) { chorus_destroy((FxHandle*)c); return 0; }

    chorus_reset((FxHandle*)c);

    out_vt->process = chorus_process;
    out_vt->set_param = chorus_set_param;
    out_vt->reset = chorus_reset;
    out_vt->destroy = chorus_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
