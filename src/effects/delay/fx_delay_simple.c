
// fx_delay_simple.c — Stereo-capable simple delay with feedback (interleaved, in-place)
// Params:
//   0: time_ms   (1..2000)
//   1: feedback  (0..0.95)
//   2: mix       (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxDelaySimple {
    float sr;
    unsigned max_channels;

    // params
    float time_ms;
    float feedback;
    float mix;

    // buffer
    unsigned max_delay_samples;
    float   *buf;        // interleaved circular: [channels][max_delay_samples]
    unsigned write_pos;

    // smoothing to avoid zipper when time changes
    float cur_delay_samp;
} FxDelaySimple;

static float* chan_ptr(FxDelaySimple* d, unsigned ch){ return d->buf + (size_t)ch * (size_t)d->max_delay_samples; }

static void dly_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxDelaySimple* d = (FxDelaySimple*)h;
    switch (idx) {
        case 0: d->time_ms = clampf(value, 1.0f, 2000.0f); break;
        case 1: d->feedback= clampf(value, 0.0f, 0.95f); break;
        case 2: d->mix     = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void dly_reset(FxHandle* h)
{
    FxDelaySimple* d = (FxDelaySimple*)h;
    memset(d->buf, 0, (size_t)d->max_delay_samples * (size_t)d->max_channels * sizeof(float));
    d->write_pos = 0;
    d->cur_delay_samp = clampf(d->time_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 1));
}

static void dly_destroy(FxHandle* h)
{
    FxDelaySimple* d = (FxDelaySimple*)h;
    free(d->buf);
    free(d);
}

static inline float read_frac(const float* cb, unsigned size, float idx)
{
    // linear interpolation from circular buffer
    while (idx < 0) idx += size;
    while (idx >= size) idx -= size;
    unsigned i0 = (unsigned)idx;
    unsigned i1 = (i0 + 1) % size;
    float frac = idx - (float)i0;
    return cb[i0] * (1.0f - frac) + cb[i1] * frac;
}

static void dly_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxDelaySimple* d = (FxDelaySimple*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;

    const float fb = d->feedback;
    const float mix = d->mix;
    const float dry = 1.0f - mix;
    const float target_delay = clampf(d->time_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 2));
    // Slew the delay time a bit each frame to reduce zipper
    const float slew = 0.0015f; // fraction per sample
    for (int n = 0; n < frames; ++n) {
        d->cur_delay_samp += (target_delay - d->cur_delay_samp) * slew;
        float delay_samp = d->cur_delay_samp;
        unsigned w = d->write_pos;

        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(d, (unsigned)ch);
            // read from write_pos - delay_samp
            float r_index = (float)w - delay_samp;
            float y_del = read_frac(cb, d->max_delay_samples, r_index);
            float xin = out[n * channels + ch];
            float y = dry * xin + mix * y_del;
            out[n * channels + ch] = y;
            // write input + feedback*delayed into buffer
            cb[w] = xin + fb * y_del;
        }
        d->write_pos = (w + 1) % d->max_delay_samples;
    }
}

int delay_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "Delay";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0] = "time_ms";
    out->param_names[1] = "feedback";
    out->param_names[2] = "mix";
    out->param_defaults[0] = 400.0f;
    out->param_defaults[1] = 0.35f;
    out->param_defaults[2] = 0.35f;
    out->latency_samples = 0;
    return 1;
}

int delay_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                 uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxDelaySimple* d = (FxDelaySimple*)calloc(1, sizeof(FxDelaySimple));
    if (!d) return 0;
    d->sr = (float)sample_rate;
    d->max_channels = max_channels ? max_channels : 2;

    // allocate 2 seconds max delay by default
    d->max_delay_samples = (unsigned)(2.0f * d->sr + 1);
    size_t total = (size_t)d->max_delay_samples * (size_t)d->max_channels;
    d->buf = (float*)calloc(total, sizeof(float));
    if (!d->buf) { dly_destroy((FxHandle*)d); return 0; }

    d->time_ms = 400.0f;
    d->feedback = 0.35f;
    d->mix = 0.35f;

    dly_reset((FxHandle*)d);

    out_vt->process = dly_process;
    out_vt->set_param = dly_set_param;
    out_vt->reset = dly_reset;
    out_vt->destroy = dly_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
