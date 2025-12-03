
// fx_tremolo_autopan.c — Tremolo / Auto-Pan (interleaved, in-place)
// Params:
//   0: rate_hz   (0.1..20)     — LFO rate
//   1: depth     (0..1)        — modulation depth
//   2: shape     (0..1)        — 0=sine, 1=square-like (soft)
//   3: autopan   (0/1)         — 0=tremolo (same L/R), 1=auto-pan (L/R opposite phase)
//   4: mix       (0..1)        — dry/wet (for tremolo this is an amplitude mix; wet scales the LFO gain)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxTremPan {
    float sr;
    unsigned max_channels;

    // params
    float rate_hz;
    float depth;
    float shape;    // 0..1
    float autopan;  // 0/1
    float mix;      // 0..1

    // state
    float phase;    // 0..1
} FxTremPan;

static inline float lfo_sample(float phase, float shape)
{
    // Sine base
    float s = sinf(2.0f * (float)M_PI * phase);
    // Shape blend: 0 -> sine, 1 -> soft-square via tanh
    float sq = tanhf(2.5f * s);
    return (1.0f - shape) * s + shape * sq;
}

static void trempan_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxTremPan* fx = (FxTremPan*)h;
    if (channels > (int)fx->max_channels) channels = (int)fx->max_channels;

    float phase = fx->phase;
    const float phase_inc = fx->rate_hz / fx->sr; // cycles per sample
    const float depth = clampf(fx->depth, 0.f, 1.f);
    const float shape = clampf(fx->shape, 0.f, 1.f);
    const int autopan = (fx->autopan >= 0.5f) ? 1 : 0;
    const float wet = clampf(fx->mix, 0.f, 1.f);
    const float dry = 1.0f - wet;

    for (int n = 0; n < frames; ++n) {
        // Base LFO value in [-1,1]
        float l = lfo_sample(phase, shape);
        float r = autopan ? -l : l;

        // Convert to gain in [1-depth, 1+depth] centered around 1.0
        float gL = 1.0f + depth * l;
        float gR = 1.0f + depth * r;

        int base = n * channels;
        if (channels >= 1) {
            float x = out[base + 0];
            float y = x * gL;
            out[base + 0] = dry * x + wet * y;
        }
        if (channels >= 2) {
            float x = out[base + 1];
            float y = x * gR;
            out[base + 1] = dry * x + wet * y;
        }
        // extra channels (3+): just use same as left
        for (int ch = 2; ch < channels; ++ch) {
            float x = out[base + ch];
            float y = x * gL;
            out[base + ch] = dry * x + wet * y;
        }

        phase += phase_inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
    }

    fx->phase = phase;
}

static void trempan_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxTremPan* fx = (FxTremPan*)h;
    switch (idx) {
        case 0: fx->rate_hz = clampf(value, 0.1f, 20.0f); break;
        case 1: fx->depth   = clampf(value, 0.0f, 1.0f);  break;
        case 2: fx->shape   = clampf(value, 0.0f, 1.0f);  break;
        case 3: fx->autopan = value;                      break;
        case 4: fx->mix     = clampf(value, 0.0f, 1.0f);  break;
        default: break;
    }
}

static void trempan_reset(FxHandle* h)
{
    FxTremPan* fx = (FxTremPan*)h;
    fx->phase = 0.0f;
}

static void trempan_destroy(FxHandle* h)
{
    free(h);
}

int trempan_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "TremoloPan";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 5;
    out->param_names[0] = "rate_hz";
    out->param_names[1] = "depth";
    out->param_names[2] = "shape";
    out->param_names[3] = "autopan";
    out->param_names[4] = "mix";
    out->param_defaults[0] = 3.0f;
    out->param_defaults[1] = 0.7f;
    out->param_defaults[2] = 0.0f;
    out->param_defaults[3] = 0.0f; // tremolo by default
    out->param_defaults[4] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int trempan_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxTremPan* fx = (FxTremPan*)calloc(1, sizeof(FxTremPan));
    if (!fx) return 0;
    fx->sr = (float)sample_rate;
    fx->max_channels = max_channels ? max_channels : 2;
    fx->rate_hz = 3.0f;
    fx->depth = 0.7f;
    fx->shape = 0.0f;
    fx->autopan = 0.0f;
    fx->mix = 1.0f;
    fx->phase = 0.0f;

    out_vt->process = trempan_process;
    out_vt->set_param = trempan_set_param;
    out_vt->reset = trempan_reset;
    out_vt->destroy = trempan_destroy;
    *out_handle = (FxHandle*)fx;
    return 1;
}
