
// fx_ringmod.c — Ring Modulator (interleaved, in-place)
// Params:
//   0: freq_hz  (1..5000)
//   1: mix      (0..1)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxRingMod {
    float sr;
    unsigned max_channels;
    float freq_hz;
    float mix;
    // state
    float phase; // 0..1
} FxRingMod;

static void ringmod_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxRingMod* fx = (FxRingMod*)h;
    if (channels > (int)fx->max_channels) channels = (int)fx->max_channels;

    float phase = fx->phase;
    const float inc = fx->freq_hz / fx->sr;
    const float wet = clampf(fx->mix, 0.f, 1.f);
    const float dry = 1.0f - wet;

    for (int n = 0; n < frames; ++n) {
        float mod = sinf(2.0f * (float)M_PI * phase);
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float y = x * mod;
            out[base + ch] = dry * x + wet * y;
        }
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
    }
    fx->phase = phase;
}

static void ringmod_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxRingMod* fx = (FxRingMod*)h;
    switch (idx) {
        case 0: fx->freq_hz = clampf(value, 1.0f, 5000.0f); break;
        case 1: fx->mix     = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
}

static void ringmod_reset(FxHandle* h)
{
    FxRingMod* fx = (FxRingMod*)h;
    fx->phase = 0.0f;
}

static void ringmod_destroy(FxHandle* h)
{
    free(h);
}

int ringmod_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "RingMod";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "freq_hz";
    out->param_names[1] = "mix";
    out->param_defaults[0] = 30.0f;
    out->param_defaults[1] = 0.5f;
    out->latency_samples = 0;
    return 1;
}

int ringmod_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxRingMod* fx = (FxRingMod*)calloc(1, sizeof(FxRingMod));
    if (!fx) return 0;
    fx->sr = (float)sample_rate;
    fx->max_channels = max_channels ? max_channels : 2;
    fx->freq_hz = 30.0f;
    fx->mix = 0.5f;
    fx->phase = 0.0f;

    out_vt->process = ringmod_process;
    out_vt->set_param = ringmod_set_param;
    out_vt->reset = ringmod_reset;
    out_vt->destroy = ringmod_destroy;
    *out_handle = (FxHandle*)fx;
    return 1;
}
