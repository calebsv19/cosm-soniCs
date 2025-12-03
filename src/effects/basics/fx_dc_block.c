
// fx_dc_block.c — First-order DC blocker / very-low HPF (interleaved, in-place)
// Params:
//   0: cutoff_hz (5..60)   — default 20 Hz
//   1: mix       (0..1)    — dry/wet (usually 1.0)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}

typedef struct FxDCBlock {
    float sr; unsigned max_channels;
    float cutoff_hz, mix;
    // simple one-pole HP realized as y = x - xz + R * y_prev, where R ~ 1-2*pi*fc/sr
    float R;
    float *xz;      // previous input per channel
    float *yz;      // previous output per channel
} FxDCBlock;

static void dc_refresh(FxDCBlock* d){
    float fc = clampf(d->cutoff_hz, 5.0f, 60.0f);
    // classic leaky integrator: R = 1 - 2*pi*fc/sr (clamped)
    float R = 1.0f - (2.0f * (float)M_PI * fc) / d->sr;
    if (R < 0.0f) R = 0.0f; if (R > 0.9999f) R = 0.9999f;
    d->R = R;
}

static void dc_set_param(FxHandle* h, uint32_t idx, float value){
    FxDCBlock* d = (FxDCBlock*)h;
    switch(idx){
        case 0: d->cutoff_hz = value; dc_refresh(d); break;
        case 1: d->mix       = clampf(value,0.f,1.f); break;
        default: break;
    }
}

static void dc_reset(FxHandle* h){
    FxDCBlock* d = (FxDCBlock*)h;
    for (unsigned ch=0; ch<d->max_channels; ++ch){ d->xz[ch]=0.f; d->yz[ch]=0.f; }
}

static void dc_destroy(FxHandle* h){
    FxDCBlock* d = (FxDCBlock*)h;
    free(d->xz); free(d->yz); free(d);
}

static void dc_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxDCBlock* d = (FxDCBlock*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;
    const float mix = d->mix, dry = 1.0f - mix;
    const float R = d->R;
    for (int n=0;n<frames;++n){
        int base = n*channels;
        for (int ch=0; ch<channels; ++ch){
            float x = out[base+ch];
            float y = x - d->xz[ch] + R * d->yz[ch];
            d->xz[ch] = x;
            d->yz[ch] = y;
            out[base+ch] = dry * x + mix * y;
        }
    }
}

int dcblock_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "DCBlock";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "cutoff_hz";
    out->param_names[1] = "mix";
    out->param_defaults[0] = 20.0f;
    out->param_defaults[1] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int dcblock_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxDCBlock* d = (FxDCBlock*)calloc(1, sizeof(FxDCBlock));
    if(!d) return 0;
    d->sr = (float)sample_rate;
    d->max_channels = max_channels?max_channels:2;
    d->cutoff_hz = 20.0f; d->mix = 1.0f;
    d->xz = (float*)calloc(d->max_channels, sizeof(float));
    d->yz = (float*)calloc(d->max_channels, sizeof(float));
    if(!d->xz || !d->yz){ dc_destroy((FxHandle*)d); return 0; }
    dc_refresh(d); dc_reset((FxHandle*)d);
    out_vt->process = dc_process;
    out_vt->set_param = dc_set_param;
    out_vt->reset = dc_reset;
    out_vt->destroy = dc_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
