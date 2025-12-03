
// fx_vibrato.c — Delay-modulation vibrato (interleaved, in-place)
// Params:
//   0: rate_hz   (0.1..10)
//   1: depth_ms  (0.1..8)   — modulation depth
//   2: base_ms   (2..12)    — base delay
//   3: mix       (0..1)     — typically 1.0 for pure vibrato, <1 for "chorus-like"
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

typedef struct FxVibrato {
    float sr;
    unsigned max_channels;

    float rate_hz;
    float depth_ms;
    float base_ms;
    float mix;

    float phase;
    unsigned max_delay; // ~20 ms buffer per channel
    float*   buf;
    unsigned write_pos;
} FxVibrato;

static float* chan_ptr(FxVibrato* v, unsigned ch){ return v->buf + (size_t)ch * v->max_delay; }

static void vibrato_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxVibrato* v = (FxVibrato*)h;
    if (channels > (int)v->max_channels) channels = (int)v->max_channels;

    float phase = v->phase;
    const float inc = v->rate_hz / v->sr;
    const float mix = clampf(v->mix, 0.f, 1.f);
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        float l = sinf(2.0f * (float)M_PI * phase);
        float depth_samp = clampf(v->depth_ms, 0.1f, 8.0f) * 0.001f * v->sr;
        float base_samp  = clampf(v->base_ms,  2.0f, 12.0f) * 0.001f * v->sr;
        float d = base_samp + depth_samp * 0.5f * l;

        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(v, (unsigned)ch);
            unsigned w = v->write_pos;

            float r = (float)((w + v->max_delay) % v->max_delay) - d;
            while (r < 0) r += v->max_delay;
            unsigned i = (unsigned)r;
            unsigned inext = (i + 1) % v->max_delay;
            float t = r - (float)i;
            float delayed = lerp(cb[i], cb[inext], t);

            float x = out[base + ch];
            cb[w] = x; // write input

            // vibrato is 100% wet traditionally; we allow mix control
            out[base + ch] = dry * x + mix * delayed;
        }
        v->write_pos = (v->write_pos + 1) % v->max_delay;

        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
    }
    v->phase = phase;
}

static void vibrato_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxVibrato* v = (FxVibrato*)h;
    switch (idx) {
        case 0: v->rate_hz  = clampf(value, 0.1f, 10.0f); break;
        case 1: v->depth_ms = clampf(value, 0.1f, 8.0f); break;
        case 2: v->base_ms  = clampf(value, 2.0f, 12.0f); break;
        case 3: v->mix      = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void vibrato_reset(FxHandle* h)
{
    FxVibrato* v = (FxVibrato*)h;
    if (v->buf) memset(v->buf, 0, (size_t)v->max_delay * (size_t)v->max_channels * sizeof(float));
    v->write_pos = 0;
    v->phase = 0.0f;
}

static void vibrato_destroy(FxHandle* h)
{
    FxVibrato* v = (FxVibrato*)h;
    free(v->buf);
    free(v);
}

int vibrato_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Vibrato";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 4;
    out->param_names[0] = "rate_hz";
    out->param_names[1] = "depth_ms";
    out->param_names[2] = "base_ms";
    out->param_names[3] = "mix";
    out->param_defaults[0] = 5.0f;
    out->param_defaults[1] = 2.0f;
    out->param_defaults[2] = 6.0f;
    out->param_defaults[3] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int vibrato_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxVibrato* v = (FxVibrato*)calloc(1, sizeof(FxVibrato));
    if (!v) return 0;

    v->sr = (float)sample_rate;
    v->max_channels = max_channels ? max_channels : 2;
    v->rate_hz = 5.0f;
    v->depth_ms = 2.0f;
    v->base_ms = 6.0f;
    v->mix = 1.0f;
    v->phase = 0.0f;

    // 20 ms max delay per channel
    v->max_delay = (unsigned)(0.020f * v->sr + 2.0f);
    size_t total = (size_t)v->max_delay * (size_t)v->max_channels;
    v->buf = (float*)calloc(total, sizeof(float));
    if (!v->buf) { vibrato_destroy((FxHandle*)v); return 0; }

    vibrato_reset((FxHandle*)v);

    out_vt->process = vibrato_process;
    out_vt->set_param = vibrato_set_param;
    out_vt->reset = vibrato_reset;
    out_vt->destroy = vibrato_destroy;
    *out_handle = (FxHandle*)v;
    return 1;
}
