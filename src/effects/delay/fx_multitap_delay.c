
// fx_multitap_delay.c — Up to 4 taps with independent multipliers and gains
// Params:
//   0: base_time_ms  (1..1500) — base delay
//   1: feedback      (0..0.9)
//   2: mix           (0..1)
//   3: tap2_mul      (0..4)    — delay = base * mul
//   4: tap3_mul      (0..4)
//   5: tap4_mul      (0..4)
//   6: tap1_gain     (0..1)    — relative wet gains
//   7: tap2_gain     (0..1)
//   8: tap3_gain     (0..1)
//   9: tap4_gain     (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxMultiTap {
    float sr;
    unsigned max_channels;

    float base_ms, fb, mix;
    float mul[4];
    float gain[4];

    unsigned max_delay_samples;
    float *buf;  // [channels][max_delay_samples]
    unsigned write_pos;

    float cur_base_samp;
} FxMultiTap;

static float* chan_ptr(FxMultiTap* d, unsigned ch){ return d->buf + (size_t)ch * (size_t)d->max_delay_samples; }

static void mt_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxMultiTap* d = (FxMultiTap*)h;
    switch (idx) {
        case 0: d->base_ms = clampf(value, 1.0f, 1500.0f); break;
        case 1: d->fb      = clampf(value, 0.0f, 0.90f); break;
        case 2: d->mix     = clampf(value, 0.0f, 1.0f); break;
        case 3: d->mul[1]  = clampf(value, 0.0f, 4.0f); break;
        case 4: d->mul[2]  = clampf(value, 0.0f, 4.0f); break;
        case 5: d->mul[3]  = clampf(value, 0.0f, 4.0f); break;
        case 6: d->gain[0] = clampf(value, 0.0f, 1.0f); break;
        case 7: d->gain[1] = clampf(value, 0.0f, 1.0f); break;
        case 8: d->gain[2] = clampf(value, 0.0f, 1.0f); break;
        case 9: d->gain[3] = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void mt_reset(FxHandle* h)
{
    FxMultiTap* d = (FxMultiTap*)h;
    memset(d->buf, 0, (size_t)d->max_delay_samples * (size_t)d->max_channels * sizeof(float));
    d->write_pos = 0;
    d->cur_base_samp = clampf(d->base_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 1));
}

static void mt_destroy(FxHandle* h)
{
    FxMultiTap* d = (FxMultiTap*)h;
    free(d->buf);
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

static void mt_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxMultiTap* d = (FxMultiTap*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;

    const float fb = d->fb;
    const float mix = d->mix, dry = 1.0f - mix;
    const float target_base = clampf(d->base_ms * 0.001f * d->sr, 1.0f, (float)(d->max_delay_samples - 2));
    const float slew = 0.0015f;

    for (int n = 0; n < frames; ++n) {
        d->cur_base_samp += (target_base - d->cur_base_samp) * slew;
        unsigned w = d->write_pos;

        for (int ch = 0; ch < channels; ++ch) {
            float* cb = chan_ptr(d, (unsigned)ch);
            float xin = out[n*channels + ch];

            float wet = 0.0f;
            for (int t = 0; t < 4; ++t) {
                float mul = (t==0) ? 1.0f : d->mul[t];
                float delay_samp = d->cur_base_samp * mul;
                float r_index = (float)w - delay_samp;
                float y_del = read_frac(cb, d->max_delay_samples, r_index);
                wet += d->gain[t] * y_del;
            }

            float y = dry * xin + mix * wet;
            out[n*channels + ch] = y;

            // simple feedback from first tap only to keep stability predictable
            float fb_del = read_frac(cb, d->max_delay_samples, (float)w - d->cur_base_samp);
            cb[w] = xin + fb * fb_del;
        }
        d->write_pos = (w + 1) % d->max_delay_samples;
    }
}

int multitap_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "MultiTapDelay";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 10;
    out->param_names[0] = "base_time_ms";
    out->param_names[1] = "feedback";
    out->param_names[2] = "mix";
    out->param_names[3] = "tap2_mul";
    out->param_names[4] = "tap3_mul";
    out->param_names[5] = "tap4_mul";
    out->param_names[6] = "tap1_gain";
    out->param_names[7] = "tap2_gain";
    out->param_names[8] = "tap3_gain";
    out->param_names[9] = "tap4_gain";
    out->param_defaults[0] = 300.0f;
    out->param_defaults[1] = 0.30f;
    out->param_defaults[2] = 0.35f;
    out->param_defaults[3] = 2.0f;
    out->param_defaults[4] = 3.0f;
    out->param_defaults[5] = 4.0f;
    out->param_defaults[6] = 1.0f;
    out->param_defaults[7] = 0.8f;
    out->param_defaults[8] = 0.6f;
    out->param_defaults[9] = 0.5f;
    out->latency_samples = 0;
    return 1;
}

int multitap_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxMultiTap* d = (FxMultiTap*)calloc(1, sizeof(FxMultiTap));
    if (!d) return 0;
    d->sr = (float)sample_rate;
    d->max_channels = max_channels ? max_channels : 2;

    d->max_delay_samples = (unsigned)(2.0f * d->sr + 1);
    size_t total = (size_t)d->max_delay_samples * (size_t)d->max_channels;
    d->buf = (float*)calloc(total, sizeof(float));
    if (!d->buf) { mt_destroy((FxHandle*)d); return 0; }

    d->base_ms = 300.0f;
    d->fb = 0.30f;
    d->mix = 0.35f;
    d->mul[0] = 1.0f; d->mul[1] = 2.0f; d->mul[2] = 3.0f; d->mul[3] = 4.0f;
    d->gain[0] = 1.0f; d->gain[1] = 0.8f; d->gain[2] = 0.6f; d->gain[3] = 0.5f;

    mt_reset((FxHandle*)d);

    out_vt->process = mt_process;
    out_vt->set_param = mt_set_param;
    out_vt->reset = mt_reset;
    out_vt->destroy = mt_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
