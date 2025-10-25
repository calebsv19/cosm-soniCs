// fx_delay.c — simple feedback delay (interleaved, in-place)
// Params: time_ms (0..2000), feedback (0..0.95), mix (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxDelay {
    // config
    uint32_t sr;
    uint32_t max_channels;

    // params
    float time_ms;     // desired delay time
    float feedback;    // 0..0.95
    float mix;         // 0..1

    // derived
    uint32_t max_delay_samples; // fixed (2 seconds)
    uint32_t delay_samples;     // from time_ms
    // per-channel circular buffers
    float*   buf;      // size = max_delay_samples * max_channels
    uint32_t write_pos;
} FxDelay;

static void delay_recompute(FxDelay* d) {
    float t = clampf(d->time_ms, 0.0f, 2000.0f);
    uint32_t samples = (uint32_t)((t * 0.001f) * (float)d->sr + 0.5f);
    if (samples > d->max_delay_samples) samples = d->max_delay_samples;
    d->delay_samples = samples;
}

static float* chan_ptr(FxDelay* d, uint32_t ch) {
    return d->buf + ch * d->max_delay_samples;
}

static void delay_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    (void)in; // in-place
    FxDelay* d = (FxDelay*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;

    const float fb  = clampf(d->feedback, 0.0f, 0.95f);
    const float mix = clampf(d->mix, 0.0f, 1.0f);
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        uint32_t w = d->write_pos;
        uint32_t r = (w + d->max_delay_samples - d->delay_samples) % d->max_delay_samples;

        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(d, (uint32_t)ch);

            float x = out[base + ch];     // incoming sample
            float y = cb[r];              // delayed sample

            // write current + feedback
            cb[w] = x + y * fb;

            // output mix
            out[base + ch] = dry * x + mix * y;
        }

        // advance pointers
        d->write_pos++;
        if (d->write_pos >= d->max_delay_samples) d->write_pos = 0;
    }
}

static void delay_set_param(FxHandle* h, uint32_t idx, float value) {
    FxDelay* d = (FxDelay*)h;
    switch (idx) {
        case 0: d->time_ms = clampf(value, 0.0f, 2000.0f); delay_recompute(d); break;
        case 1: d->feedback = clampf(value, 0.0f, 0.95f); break;
        case 2: d->mix = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void delay_reset(FxHandle* h) {
    FxDelay* d = (FxDelay*)h;
    if (d->buf) {
        memset(d->buf, 0, (size_t)d->max_delay_samples * (size_t)d->max_channels * sizeof(float));
    }
    d->write_pos = 0;
}

static void delay_destroy(FxHandle* h) {
    FxDelay* d = (FxDelay*)h;
    free(d->buf);
    free(d);
}

int delay_get_desc(FxDesc *out) {
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
    out->latency_samples = 0; // simple feed-back echo; nominal "latency" is creative delay, not throughput 
latency
    return 1;
}

int delay_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                 uint32_t sample_rate, uint32_t max_block, uint32_t max_channels) {
    (void)desc; (void)max_block;

    FxDelay* d = (FxDelay*)calloc(1, sizeof(FxDelay));
    if (!d) return 0;

    d->sr = sample_rate;
    d->max_channels = (max_channels > 0) ? max_channels : 2;

    // 2 seconds maximum delay buffer
    d->max_delay_samples = (uint32_t)(2.0f * (float)sample_rate + 0.5f);
    size_t total = (size_t)d->max_delay_samples * (size_t)d->max_channels;
    d->buf = (float*)calloc(total, sizeof(float));
    if (!d->buf) {
        delay_destroy((FxHandle*)d);
        return 0;
    }

    // defaults
    d->time_ms = 400.0f;
    d->feedback = 0.35f;
    d->mix = 0.35f;
    delay_recompute(d);
    delay_reset((FxHandle*)d);

    out_vt->process = delay_process;
    out_vt->set_param = delay_set_param;
    out_vt->reset = delay_reset;
    out_vt->destroy = delay_destroy;

    *out_handle = (FxHandle*)d;
    return 1;
}

