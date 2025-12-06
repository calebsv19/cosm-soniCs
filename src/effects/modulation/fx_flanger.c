
// fx_flanger.c — Flanger (interleaved, in-place)
// Params:
//   0: rate_hz   (0.05..5)
//   1: depth_ms  (0.1..5)     — modulation depth
//   2: base_ms   (0.1..5)     — base delay
//   3: feedback  (-0.95..0.95)
//   4: mix       (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float lerp(float a, float b, float t){ return a + (b - a) * t; }

typedef struct FxFlanger {
    float sr;
    unsigned max_channels;

    // params
    float rate_hz;
    float depth_ms;
    float base_ms;
    float feedback;
    float mix;

    // state
    float phase;
    unsigned max_delay; // 10 ms buffer per channel
    float*   buf;
    unsigned write_pos;
} FxFlanger;

static float* chan_ptr(FxFlanger* f, unsigned ch){ return f->buf + (size_t)ch * f->max_delay; }

static void flanger_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxFlanger* f = (FxFlanger*)h;
    if (channels > (int)f->max_channels) channels = (int)f->max_channels;

    float phase = f->phase;
    const float inc = f->rate_hz / f->sr;

    const float fb  = clampf(f->feedback, -0.95f, 0.95f);
    const float mix = clampf(f->mix, 0.f, 1.f);
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        float l = sinf(2.0f * (float)M_PI * phase);
        float depth_samp = clampf(f->depth_ms, 0.1f, 5.0f) * 0.001f * f->sr;
        float base_samp  = clampf(f->base_ms,  0.1f, 5.0f) * 0.001f * f->sr;
        float d = base_samp + depth_samp * 0.5f * l;

        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(f, (unsigned)ch);
            unsigned w = f->write_pos;

            // read fractional
            float r = (float)((w + f->max_delay) % f->max_delay) - d;
            while (r < 0) r += f->max_delay;
            unsigned i = (unsigned)r;
            unsigned inext = (i + 1) % f->max_delay;
            float t = r - (float)i;
            float delayed = lerp(cb[i], cb[inext], t);

            float x = out[base + ch];
            float y = x + delayed * fb; // feedback into buffer
            cb[w] = y;                  // write

            out[base + ch] = dry * x + mix * (0.7f * (x + delayed)); // light comb
        }

        f->write_pos = (f->write_pos + 1) % f->max_delay;
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
    }
    f->phase = phase;
}

static void flanger_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxFlanger* f = (FxFlanger*)h;
    switch (idx) {
        case 0: f->rate_hz  = clampf(value, 0.05f, 5.0f); break;
        case 1: f->depth_ms = clampf(value, 0.1f, 5.0f); break;
        case 2: f->base_ms  = clampf(value, 0.1f, 5.0f); break;
        case 3: f->feedback = clampf(value, -0.95f, 0.95f); break;
        case 4: f->mix      = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void flanger_reset(FxHandle* h)
{
    FxFlanger* f = (FxFlanger*)h;
    if (f->buf) memset(f->buf, 0, (size_t)f->max_delay * (size_t)f->max_channels * sizeof(float));
    f->write_pos = 0;
    f->phase = 0.0f;
}

static void flanger_destroy(FxHandle* h)
{
    FxFlanger* f = (FxFlanger*)h;
    free(f->buf);
    free(f);
}

int flanger_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Flanger";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 5;
    out->param_names[0] = "rate_hz";
    out->param_names[1] = "depth_ms";
    out->param_names[2] = "base_ms";
    out->param_names[3] = "feedback";
    out->param_names[4] = "mix";
    out->param_defaults[0] = 0.25f;
    out->param_defaults[1] = 2.0f;
    out->param_defaults[2] = 1.0f;
    out->param_defaults[3] = 0.25f;
    out->param_defaults[4] = 0.5f;
    out->latency_samples = 0;
    return 1;
}

int flanger_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxFlanger* f = (FxFlanger*)calloc(1, sizeof(FxFlanger));
    if (!f) return 0;

    f->sr = (float)sample_rate;
    f->max_channels = max_channels ? max_channels : 2;
    f->rate_hz = 0.25f;
    f->depth_ms = 2.0f;
    f->base_ms = 1.0f;
    f->feedback = 0.25f;
    f->mix = 0.5f;
    f->phase = 0.0f;

    // 10 ms buffer per channel
    f->max_delay = (unsigned)(0.010f * f->sr + 2.0f);
    size_t total = (size_t)f->max_delay * (size_t)f->max_channels;
    f->buf = (float*)calloc(total, sizeof(float));
    if (!f->buf) { flanger_destroy((FxHandle*)f); return 0; }

    flanger_reset((FxHandle*)f);

    out_vt->process = flanger_process;
    out_vt->set_param = flanger_set_param;
    out_vt->reset = flanger_reset;
    out_vt->destroy = flanger_destroy;
    *out_handle = (FxHandle*)f;
    return 1;
}
