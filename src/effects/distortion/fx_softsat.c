
// fx_softsat.c — Soft saturation (tanh waveshaper) with pre/post tone and mix
// Params:
//   0: drive_dB   (0..36)     — input gain into the shaper
//   1: output_dB  (-24..+12)  — makeup/output
//   2: tone_lp_hz (1000..20000) — simple 1-pole low-pass AFTER saturation
//   3: mix        (0..1)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static inline float dB_to_lin(float dB){return powf(10.0f, dB*0.05f);}

typedef struct OnePole {
    float a, b;    // y[n] = a*x[n] + b*y[n-1]
    float *z;      // per-channel
} OnePole;

typedef struct FxSoftSat{
    float sr; unsigned max_channels;
    float drive_dB, out_dB, lp_hz, mix;
    float pre_gain, post_gain;
    OnePole lp;
} FxSoftSat;

static void onepole_lp_update(OnePole* p, float sr, float hz){
    float fc = clampf(hz, 200.0f, sr*0.49f);
    float x = expf(-2.0f*(float)M_PI*fc/sr);
    p->a = 1.0f - x;
    p->b = x;
}

static void ss_update(FxSoftSat* s){
    s->pre_gain  = dB_to_lin(s->drive_dB);
    s->post_gain = dB_to_lin(s->out_dB);
    onepole_lp_update(&s->lp, s->sr, s->lp_hz);
}

static void ss_set_param(FxHandle* h, uint32_t idx, float value){
    FxSoftSat* s = (FxSoftSat*)h;
    switch(idx){
        case 0: s->drive_dB = clampf(value,0.f,36.f); break;
        case 1: s->out_dB   = clampf(value,-24.f,12.f); break;
        case 2: s->lp_hz    = clampf(value,1000.f,20000.f); break;
        case 3: s->mix      = clampf(value,0.f,1.f); break;
        default: break;
    }
    ss_update(s);
}

static void ss_reset(FxHandle* h){
    FxSoftSat* s = (FxSoftSat*)h;
    for(unsigned ch=0; ch<s->max_channels; ++ch) s->lp.z[ch] = 0.0f;
}
static void ss_destroy(FxHandle* h){
    FxSoftSat* s = (FxSoftSat*)h;
    free(s->lp.z);
    free(s);
}

// fast tanh approximation is ok, but regular tanhf is fine at audio rates.
static inline float saturate(float x){
    return tanhf(x);
}

static void ss_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxSoftSat* s = (FxSoftSat*)h;
    if (channels > (int)s->max_channels) channels = (int)s->max_channels;
    const float pre  = s->pre_gain;
    const float post = s->post_gain;
    const float dry = 1.0f - s->mix;
    const float wet = s->mix;
    for(int n=0;n<frames;++n){
        int base = n*channels;
        for(int ch=0; ch<channels; ++ch){
            float x = out[base+ch] * pre;
            float y = saturate(x);
            // simple tone tamer
            float z = s->lp.a * y + s->lp.b * s->lp.z[ch];
            s->lp.z[ch] = z;
            z *= post;
            out[base+ch] = dry*out[base+ch] + wet*z;
        }
    }
}

int softsat_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "SoftSaturation";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 4;
    out->param_names[0]="drive_dB";
    out->param_names[1]="output_dB";
    out->param_names[2]="tone_lp_hz";
    out->param_names[3]="mix";
    out->param_defaults[0]=12.0f;
    out->param_defaults[1]=0.0f;
    out->param_defaults[2]=8000.0f;
    out->param_defaults[3]=1.0f;
    out->latency_samples=0;
    return 1;
}
int softsat_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxSoftSat* s = (FxSoftSat*)calloc(1,sizeof(FxSoftSat));
    if(!s) return 0;
    s->sr = (float)sample_rate; s->max_channels = max_channels?max_channels:2;
    s->drive_dB=12.f; s->out_dB=0.f; s->lp_hz=8000.f; s->mix=1.f;
    s->lp.z = (float*)calloc(s->max_channels,sizeof(float));
    if(!s->lp.z){ ss_destroy((FxHandle*)s); return 0; }
    ss_update(s); ss_reset((FxHandle*)s);
    out_vt->process = ss_process;
    out_vt->set_param = ss_set_param;
    out_vt->reset = ss_reset;
    out_vt->destroy = ss_destroy;
    *out_handle = (FxHandle*)s;
    return 1;
}
