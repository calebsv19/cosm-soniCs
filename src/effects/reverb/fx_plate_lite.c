
// fx_plate_lite.c — Bright plate‑style reverb (dense diffusion, fast build‑up)
//
// Design goal: distinct "plate" sound vs. room/Schroeder reverb:
//  - Faster initial diffusion (series allpasses), shorter delays.
//  - Brighter tail with gentle high‑cut control.
//  - No modulation (cheap).
//
// Params:
//   0: size         (0.6..1.4,  default 1.0)   — scales delays
//   1: decay_rt60   (0.2..8.0,  default 2.5)   — tail length
//   2: highcut_Hz   (2000..16000, default 12000)
//   3: mix          (0..1,      default 0.25)
//
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct DelayLine {
    float* buf; int len; int w;
} DelayLine;

static inline float dl_read(const DelayLine* d, int tap){ int r=d->w-tap; while(r<0) r+=d->len; return d->buf[r]; }
static inline void  dl_write(DelayLine* d, float x){ d->buf[d->w]=x; d->w++; if(d->w>=d->len)d->w=0; }
static inline void  dl_clear(DelayLine* d){ if(!d||!d->buf)return; for(int i=0;i<d->len;++i)d->buf[i]=0.f; d->w=0; }

typedef struct Allpass { DelayLine dl; float g; } Allpass;
typedef struct Comb    { DelayLine dl; float g; } Comb;

typedef struct FxPlate {
    uint32_t sr; unsigned max_ch;
    float size, decay_rt60, highcut_Hz, mix;
    // derived
    int taps_ap[3];
    int taps_cb[2];
    float cb_g[2];
    float lpf_a;
    // per side
    Allpass apL[3], apR[3];
    Comb    cbL[2], cbR[2];
    float   lpL, lpR;
} FxPlate;

// base delays @48k
static const int AP_BASE_48K[3] = { 142,  182,  256 }; // short, fast diffusion
static const int CB_BASE_48K[2] = { 911,  1171 };      // slightly longer combs for sustain
static const int SPREAD_48K = 19;

static int alloc_dl(DelayLine* d, int len){
    d->buf=(float*)calloc((size_t)len,sizeof(float));
    if(!d->buf) return 0;
    d->len=len; d->w=0; return 1;
}
static void free_dl(DelayLine* d){ free(d->buf); d->buf=NULL; d->len=0; d->w=0; }

static int scale_tap(int base48, float sr, float size, int spread, int right){
    int v = (int)(base48 * (sr/48000.f) * size + 0.5f);
    if (right) v += (int)(spread * (sr/48000.f) + 0.5f);
    if (v<1) v=1; return v;
}

static inline float rt60_to_g(float delay_s, float rt60){
    if (rt60 <= 1e-5f) rt60 = 1e-5f;
    float g = powf(10.f, (-3.f * delay_s) / rt60);
    if (g > 0.9998f) g = 0.9998f;
    if (g < 0.0f) g = 0.0f;
    return g;
}

static void plate_recalc(FxPlate* p){
    float sr = (float)p->sr;
    float size = clampf(p->size, 0.6f, 1.4f);
    // taps
    for(int i=0;i<3;++i){
        p->taps_ap[i] = scale_tap(AP_BASE_48K[i], sr, size, SPREAD_48K, 0);
    }
    for(int i=0;i<2;++i){
        p->taps_cb[i] = scale_tap(CB_BASE_48K[i], sr, size, SPREAD_48K, 0);
        p->cb_g[i] = rt60_to_g(p->taps_cb[i]/sr, p->decay_rt60);
    }
    // highcut one‑pole coeff
    float fc = clampf(p->highcut_Hz, 2000.f, 16000.f);
    float a = expf(-2.f * (float)M_PI * fc / sr);
    p->lpf_a = a;
}

static inline float ap_tick(Allpass* a, float x){
    float d = dl_read(&a->dl, a->dl.len - 1);
    float y = -a->g * x + d;
    float w = x + a->g * y;
    dl_write(&a->dl, w);
    return y;
}
static inline float cb_tick(Comb* c, float x){
    float y = dl_read(&c->dl, c->dl.len - 1);
    dl_write(&c->dl, x + c->g * y);
    return y;
}

static void plate_set_param(FxHandle* h, uint32_t idx, float v){
    FxPlate* p=(FxPlate*)h;
    switch(idx){
        case 0: p->size       = clampf(v, 0.6f, 1.4f); break;
        case 1: p->decay_rt60 = clampf(v, 0.2f, 8.0f); break;
        case 2: p->highcut_Hz = clampf(v, 2000.f, 16000.f); break;
        case 3: p->mix        = clampf(v, 0.f, 1.f); break;
        default: break;
    }
    plate_recalc(p);
}
static void plate_reset(FxHandle* h){
    FxPlate* p=(FxPlate*)h;
    for(int i=0;i<3;++i){ dl_clear(&p->apL[i].dl); dl_clear(&p->apR[i].dl); }
    for(int i=0;i<2;++i){ dl_clear(&p->cbL[i].dl); dl_clear(&p->cbR[i].dl); }
    p->lpL = p->lpR = 0.f;
}
static void plate_destroy(FxHandle* h){
    FxPlate* p=(FxPlate*)h;
    for(int i=0;i<3;++i){ free_dl(&p->apL[i].dl); free_dl(&p->apR[i].dl); }
    for(int i=0;i<2;++i){ free_dl(&p->cbL[i].dl); free_dl(&p->cbR[i].dl); }
    free(h);
}

static void plate_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxPlate* p=(FxPlate*)h;
    if (channels < 1) return;
    if (channels > 2) channels = 2;
    const float wet = p->mix, dry = 1.f - wet;
    for(int n=0;n<frames;++n){
        float xl = out[n*channels + 0];
        float xr = (channels>=2)? out[n*channels + 1] : xl;

        // fast diffusion via 3 series allpasses per side
        float yl = xl;
        float yr = xr;
        for(int i=0;i<3;++i){
            yl = ap_tick(&p->apL[i], yl);
            yr = ap_tick(&p->apR[i], yr);
        }
        // sustain via two short combs per side (bright plate sound)
        float sl = 0.f, sr = 0.f;
        for(int i=0;i<2;++i){
            sl += cb_tick(&p->cbL[i], yl);
            sr += cb_tick(&p->cbR[i], yr);
        }
        sl *= 0.5f; sr *= 0.5f;

        // gentle highcut one‑pole
        p->lpL = (1.f - p->lpf_a)*sl + p->lpf_a*p->lpL;
        p->lpR = (1.f - p->lpf_a)*sr + p->lpf_a*p->lpR;

        out[n*channels + 0] = dry*xl + wet*p->lpL;
        if (channels>=2) out[n*channels + 1] = dry*xr + wet*p->lpR;
    }
}

int plate_lite_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "PlateLite";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 4;
    out->param_names[0]    = "size";
    out->param_names[1]    = "decay_rt60";
    out->param_names[2]    = "highcut_Hz";
    out->param_names[3]    = "mix";
    out->param_defaults[0] = 1.0f;
    out->param_defaults[1] = 2.5f;
    out->param_defaults[2] = 12000.0f;
    out->param_defaults[3] = 0.25f;
    out->latency_samples = 0;
    return 1;
}

int plate_lite_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                      uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxPlate* p=(FxPlate*)calloc(1,sizeof(FxPlate));
    if(!p) return 0;
    p->sr=sample_rate; p->max_ch = max_channels?max_channels:2;
    p->size=1.f; p->decay_rt60=2.5f; p->highcut_Hz=12000.f; p->mix=0.25f;
    // allocate with initial sizes
    for(int i=0;i<3;++i){
        int len = (int)(AP_BASE_48K[i] * (p->sr/48000.f) + 8);
        if(len<8) len=8;
        if(!alloc_dl(&p->apL[i].dl,len) || !alloc_dl(&p->apR[i].dl,len)){ plate_destroy((FxHandle*)p); return 0; }
        p->apL[i].g = 0.6f + 0.05f*i;
        p->apR[i].g = 0.6f + 0.05f*i;
    }
    for(int i=0;i<2;++i){
        int len = (int)(CB_BASE_48K[i] * (p->sr/48000.f) + 8);
        if(len<8) len=8;
        if(!alloc_dl(&p->cbL[i].dl,len) || !alloc_dl(&p->cbR[i].dl,len)){ plate_destroy((FxHandle*)p); return 0; }
        p->cbL[i].g = p->cbR[i].g = 0.0f; // will set via recalc
    }
    plate_recalc(p);
    plate_reset((FxHandle*)p);
    out_vt->process = plate_process;
    out_vt->set_param = plate_set_param;
    out_vt->reset = plate_reset;
    out_vt->destroy = plate_destroy;
    *out_handle = (FxHandle*)p;
    return 1;
}
