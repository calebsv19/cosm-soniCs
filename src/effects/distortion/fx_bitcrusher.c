
// fx_bitcrusher.c — Bit depth & sample-rate reducer (interleaved, in-place)
// Params:
//   0: bits     (4..16)       — quantization bits
//   1: srrate   (0.01..1.0)   — downsample ratio relative to sr (1.0 = no reduction)
//   2: mix      (0..1)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}

typedef struct FxBitCrush{
    float sr; unsigned max_channels;
    float bits, srrate, mix;
    // per-channel held sample and phase
    float *hold;
    float *phase;
} FxBitCrush;

static void bc_set_param(FxHandle* h, uint32_t idx, float value){
    FxBitCrush* c = (FxBitCrush*)h;
    switch(idx){
        case 0: c->bits   = clampf(value,4.f,16.f); break;
        case 1: c->srrate = clampf(value,0.01f,1.0f); break;
        case 2: c->mix    = clampf(value,0.f,1.f); break;
        default: break;
    }
}

static void bc_reset(FxHandle* h){
    FxBitCrush* c = (FxBitCrush*)h;
    for(unsigned ch=0; ch<c->max_channels; ++ch){ c->hold[ch]=0.f; c->phase[ch]=0.f; }
}
static void bc_destroy(FxHandle* h){
    FxBitCrush* c = (FxBitCrush*)h;
    free(c->hold); free(c->phase); free(c);
}

static void bc_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxBitCrush* c = (FxBitCrush*)h;
    if (channels > (int)c->max_channels) channels = (int)c->max_channels;
    const float dry = 1.0f - c->mix;
    const float wet = c->mix;

    // quantization step from bits: map [-1,1] to 2^bits levels
    const int qlevels = (int)(1u << (int)c->bits);
    const float step = 2.0f / (float)(qlevels - 1);

    // downsample: hold sample for hop = 1/srrate samples (min 1)
    const float hop = fmaxf(1.0f, 1.0f / c->srrate);

    for(int n=0;n<frames;++n){
        int base = n*channels;
        for(int ch=0; ch<channels; ++ch){
            float x = out[base+ch];
            // update hold based on phase
            c->phase[ch] += 1.0f;
            if (c->phase[ch] >= hop){
                c->phase[ch] -= hop;
                // quantize input at this moment
                float q = roundf((x + 1.0f) / step) * step - 1.0f;
                // clamp to [-1,1]
                if (q > 1.f) q = 1.f; else if (q < -1.f) q = -1.f;
                c->hold[ch] = q;
            }
            float y = c->hold[ch];
            out[base+ch] = dry*out[base+ch] + wet*y;
        }
    }
}

int bitcrusher_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "BitCrusher";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0]="bits";
    out->param_names[1]="srrate";
    out->param_names[2]="mix";
    out->param_defaults[0]=8.0f;
    out->param_defaults[1]=0.25f;
    out->param_defaults[2]=1.0f;
    out->latency_samples=0;
    return 1;
}
int bitcrusher_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                      uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxBitCrush* c = (FxBitCrush*)calloc(1,sizeof(FxBitCrush));
    if(!c) return 0;
    c->sr = (float)sample_rate; c->max_channels = max_channels?max_channels:2;
    c->bits=8.f; c->srrate=0.25f; c->mix=1.f;
    c->hold = (float*)calloc(c->max_channels,sizeof(float));
    c->phase= (float*)calloc(c->max_channels,sizeof(float));
    if(!c->hold || !c->phase){ bc_destroy((FxHandle*)c); return 0; }
    bc_reset((FxHandle*)c);
    out_vt->process = bc_process;
    out_vt->set_param = bc_set_param;
    out_vt->reset = bc_reset;
    out_vt->destroy = bc_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
