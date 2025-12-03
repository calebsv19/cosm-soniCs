
// fx_pingpong_delay.c — Stereo ping-pong delay (interleaved, in-place)
// Params:
//   0: time_ms    (1..2000)
//   1: feedback   (0..0.95)
//   2: mix        (0..1)
// Notes:
// - Requires stereo to "ping-pong". If mono, behaves like a regular delay.
// - Feedback is cross-fed L->R->L for ping-pong bounce.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxPingPong {
    float sr;
    unsigned max_channels;

    float time_ms, feedback, mix;

    unsigned max_delay_samples;
    float *bufL, *bufR;
    unsigned write_pos;
    float cur_delay_samp;
} FxPingPong;

static void pp_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxPingPong* d = (FxPingPong*)h;
    switch (idx) {
        case 0: d->time_ms = clampf(value, 1.0f, 2000.0f); break;
        case 1: d->feedback= clampf(value, 0.0f, 0.95f); break;
        case 2: d->mix     = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void pp_reset(FxHandle* h)
{
    FxPingPong* d = (FxPingPong*)h;
    memset(d->bufL, 0, d->max_delay_samples * sizeof(float));
    memset(d->bufR, 0, d->max_delay_samples * sizeof(float));
    d->write_pos = 0;
    d->cur_delay_samp = clampf(d->time_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 1));
}

static void pp_destroy(FxHandle* h)
{
    FxPingPong* d = (FxPingPong*)h;
    free(d->bufL);
    free(d->bufR);
    free(d);
}

static inline float read_frac(const float* cb, unsigned size, float idx)
{
    while (idx < 0) idx += size;
    while (idx >= size) idx -= size;
    unsigned i0 = (unsigned)idx;
    unsigned i1 = (i0 + 1) % size;
    float frac = idx - (float)i0;
    return cb[i0] * (1.0f - frac) + cb[i1] * frac;
}

static void pp_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxPingPong* d = (FxPingPong*)h;
    if (channels < 2) {
        // fall back to mono-ish behavior: write ping to bufL only and return to both
        const float fb = d->feedback;
        const float mix = d->mix, dry = 1.0f - mix;
        const float target = clampf(d->time_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 2));
        const float slew = 0.0015f;
        for (int n = 0; n < frames; ++n) {
            d->cur_delay_samp += (target - d->cur_delay_samp) * slew;
            float r_index = (float)d->write_pos - d->cur_delay_samp;
            float y_del = read_frac(d->bufL, d->max_delay_samples, r_index);
            float xin = out[n * channels + 0];
            float y = dry * xin + mix * y_del;
            out[n * channels + 0] = y;
            d->bufL[d->write_pos] = xin + fb * y_del;
            d->write_pos = (d->write_pos + 1) % d->max_delay_samples;
        }
        return;
    }

    const float fb = d->feedback;
    const float mix = d->mix, dry = 1.0f - mix;
    const float target = clampf(d->time_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 2));
    const float slew = 0.0015f;
    for (int n = 0; n < frames; ++n) {
        d->cur_delay_samp += (target - d->cur_delay_samp) * slew;
        float r_index = (float)d->write_pos - d->cur_delay_samp;

        float dL = read_frac(d->bufL, d->max_delay_samples, r_index);
        float dR = read_frac(d->bufR, d->max_delay_samples, r_index);

        float inL = out[n*channels + 0];
        float inR = out[n*channels + 1];

        float outL = dry*inL + mix*dL;
        float outR = dry*inR + mix*dR;
        out[n*channels + 0] = outL;
        out[n*channels + 1] = outR;

        // cross-feed feedback: L gets inL + fb*dR, R gets inR + fb*dL
        d->bufL[d->write_pos] = inL + fb * dR;
        d->bufR[d->write_pos] = inR + fb * dL;

        d->write_pos = (d->write_pos + 1) % d->max_delay_samples;
    }
}

int pingpong_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "PingPongDelay";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0] = "time_ms";
    out->param_names[1] = "feedback";
    out->param_names[2] = "mix";
    out->param_defaults[0] = 450.0f;
    out->param_defaults[1] = 0.45f;
    out->param_defaults[2] = 0.4f;
    out->latency_samples = 0;
    return 1;
}

int pingpong_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxPingPong* d = (FxPingPong*)calloc(1, sizeof(FxPingPong));
    if (!d) return 0;
    d->sr = (float)sample_rate;
    d->max_channels = max_channels ? max_channels : 2;

    d->max_delay_samples = (unsigned)(2.0f * d->sr + 1);
    d->bufL = (float*)calloc(d->max_delay_samples, sizeof(float));
    d->bufR = (float*)calloc(d->max_delay_samples, sizeof(float));
    if (!d->bufL || !d->bufR) { pp_destroy((FxHandle*)d); return 0; }

    d->time_ms = 450.0f;
    d->feedback = 0.45f;
    d->mix = 0.4f;

    pp_reset((FxHandle*)d);

    out_vt->process = pp_process;
    out_vt->set_param = pp_set_param;
    out_vt->reset = pp_reset;
    out_vt->destroy = pp_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
