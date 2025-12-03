
// fx_hardclip.c — Hard clipper (interleaved, in-place)
// Params:
//   0: threshold_dB  (-24..0)  — level where clipping begins (relative, applied as input gain)
//   1: output_dB     (-24..+12) — makeup/output gain after clipping
//   2: mix           (0..1)    — dry/wet
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static inline float dB_to_lin(float dB){return powf(10.0f, dB*0.05f);}

typedef struct FxHardClip{
    float sr; unsigned max_channels;
    float thr_dB, out_dB, mix;
    float in_gain, out_gain;
} FxHardClip;

static void hc_update(FxHardClip* c){
    // Interpret threshold_dB as a pre-gain that maps that dB to ~1.0 full-scale
    // If thr_dB = -6 dB, in_gain ≈ +6 dB so typical material clips at ±1
    c->in_gain  = dB_to_lin(-c->thr_dB);
    c->out_gain = dB_to_lin(c->out_dB);
}

static void hc_set_param(FxHandle* h, uint32_t idx, float value){
    FxHardClip* c = (FxHardClip*)h;
    switch(idx){
        case 0: c->thr_dB = clampf(value,-24.f,0.f); break;
        case 1: c->out_dB = clampf(value,-24.f,12.f); break;
        case 2: c->mix    = clampf(value,0.f,1.f); break;
        default: break;
    }
    hc_update(c);
}
static void hc_reset(FxHandle* h){ (void)h; }
static void hc_destroy(FxHandle* h){ free(h); }

static void hc_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxHardClip* c = (FxHardClip*)h;
    const float dry = 1.0f - c->mix;
    const float wet = c->mix;
    const float pre = c->in_gain;
    const float post= c->out_gain;
    for(int n=0;n<frames;++n){
        int base = n*channels;
        for(int ch=0; ch<channels; ++ch){
            float x = out[base+ch]*pre;
            float y = x;
            if (y > 1.f) y = 1.f;
            else if (y < -1.f) y = -1.f;
            y *= post;
            out[base+ch] = dry*out[base+ch] + wet*y;
        }
    }
}

int hardclip_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "HardClip";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0]="threshold_dB";
    out->param_names[1]="output_dB";
    out->param_names[2]="mix";
    out->param_defaults[0] = -6.0f;
    out->param_defaults[1] = 0.0f;
    out->param_defaults[2] = 1.0f;
    out->latency_samples = 0;
    return 1;
}
int hardclip_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxHardClip* c = (FxHardClip*)calloc(1,sizeof(FxHardClip));
    if(!c) return 0;
    c->sr = (float)sample_rate; c->max_channels = max_channels?max_channels:2;
    c->thr_dB=-6.f; c->out_dB=0.f; c->mix=1.f;
    hc_update(c);
    out_vt->process = hc_process;
    out_vt->set_param = hc_set_param;
    out_vt->reset = hc_reset;
    out_vt->destroy = hc_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
