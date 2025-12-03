
// fx_overdrive.c — Asymmetric overdrive with pre/post tone (HP pre, LP post), mix
// Params:
//   0: drive_dB    (0..36)
//   1: asymmetry   (0..1)    — 0 = symmetric, 1 = strongly asymmetric (more even harmonics)
//   2: pre_hp_hz   (20..500) — high-pass pre-emphasis (tighten low end)
//   3: post_lp_hz  (1000..20000) — low-pass de-harsh after drive
//   4: output_dB   (-24..+12)
//   5: mix         (0..1)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static inline float dB_to_lin(float dB){return powf(10.0f, dB*0.05f);}

typedef struct OnePole { float a, b; float *z; } OnePole;

typedef struct FxOverdrive{
    float sr; unsigned max_channels;
    float drive_dB, asym, pre_hp, post_lp, out_dB, mix;
    float pre_gain, out_gain;
    OnePole hp, lp;
} FxOverdrive;

static void hp_update(OnePole* p, float sr, float hz){
    float fc = clampf(hz, 10.0f, sr*0.49f);
    float x = expf(-2.0f*(float)M_PI*fc/sr);
    // simple HP from LP via y[n]=x[n]-lp(x[n])
    // we'll store LP coeffs and compute hp inline
    p->a = 1.0f - x; p->b = x;
}
static void lp_update(OnePole* p, float sr, float hz){
    float fc = clampf(hz, 200.0f, sr*0.49f);
    float x = expf(-2.0f*(float)M_PI*fc/sr);
    p->a = 1.0f - x; p->b = x;
}

static void od_update(FxOverdrive* d){
    d->pre_gain = dB_to_lin(d->drive_dB);
    d->out_gain = dB_to_lin(d->out_dB);
    hp_update(&d->hp, d->sr, d->pre_hp);
    lp_update(&d->lp, d->sr, d->post_lp);
}

static void od_set_param(FxHandle* h, uint32_t idx, float value){
    FxOverdrive* d = (FxOverdrive*)h;
    switch(idx){
        case 0: d->drive_dB = clampf(value,0.f,36.f); break;
        case 1: d->asym     = clampf(value,0.f,1.f); break;
        case 2: d->pre_hp   = clampf(value,20.f,500.f); break;
        case 3: d->post_lp  = clampf(value,1000.f,20000.f); break;
        case 4: d->out_dB   = clampf(value,-24.f,12.f); break;
        case 5: d->mix      = clampf(value,0.f,1.f); break;
        default: break;
    }
    od_update(d);
}

static void od_reset(FxHandle* h){
    FxOverdrive* d = (FxOverdrive*)h;
    for(unsigned ch=0; ch<d->max_channels; ++ch){ d->hp.z[ch]=0.f; d->lp.z[ch]=0.f; }
}
static void od_destroy(FxHandle* h){
    FxOverdrive* d = (FxOverdrive*)h;
    free(d->hp.z); free(d->lp.z); free(d);
}

static inline float hipass_tick(OnePole* p, int ch, float x){
    float lp = p->a * x + p->b * p->z[ch];
    p->z[ch] = lp;
    return x - lp;
}

static inline float lopass_tick(OnePole* p, int ch, float x){
    float y = p->a * x + p->b * p->z[ch];
    p->z[ch] = y;
    return y;
}

// asymmetric soft clipper: blend tanh(x) with tanh(x + bias) - tanh(bias)
static inline float asym_sat(float x, float amt){
    float bias = amt * 0.6f; // bias range
    float y1 = tanhf(x);
    float y2 = tanhf(x + bias) - tanhf(bias);
    return 0.5f*(y1 + y2);
}

static void od_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxOverdrive* d = (FxOverdrive*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;
    const float pre  = d->pre_gain;
    const float post = d->out_gain;
    const float wet = d->mix, dry = 1.0f - wet;
    const float asym = d->asym;
    for(int n=0;n<frames;++n){
        int base = n*channels;
        for(int ch=0; ch<channels; ++ch){
            float x = out[base+ch];
            // pre high-pass
            x = hipass_tick(&d->hp, ch, x);
            // drive
            x *= pre;
            // asymmetric saturation
            float y = asym_sat(x, asym);
            // post low-pass to tame fizz
            y = lopass_tick(&d->lp, ch, y);
            y *= post;
            out[base+ch] = dry*out[base+ch] + wet*y;
        }
    }
}

int overdrive_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "Overdrive";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 6;
    out->param_names[0]="drive_dB";
    out->param_names[1]="asymmetry";
    out->param_names[2]="pre_hp_hz";
    out->param_names[3]="post_lp_hz";
    out->param_names[4]="output_dB";
    out->param_names[5]="mix";
    out->param_defaults[0]=18.0f;
    out->param_defaults[1]=0.4f;
    out->param_defaults[2]=80.0f;
    out->param_defaults[3]=7000.0f;
    out->param_defaults[4]=0.0f;
    out->param_defaults[5]=1.0f;
    out->latency_samples=0;
    return 1;
}
int overdrive_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxOverdrive* d = (FxOverdrive*)calloc(1,sizeof(FxOverdrive));
    if(!d) return 0;
    d->sr = (float)sample_rate; d->max_channels = max_channels?max_channels:2;
    d->drive_dB=18.f; d->asym=0.4f; d->pre_hp=80.f; d->post_lp=7000.f; d->out_dB=0.f; d->mix=1.f;
    d->hp.z = (float*)calloc(d->max_channels,sizeof(float));
    d->lp.z = (float*)calloc(d->max_channels,sizeof(float));
    if(!d->hp.z || !d->lp.z){ od_destroy((FxHandle*)d); return 0; }
    od_update(d); od_reset((FxHandle*)d);
    out_vt->process = od_process;
    out_vt->set_param = od_set_param;
    out_vt->reset = od_reset;
    out_vt->destroy = od_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
