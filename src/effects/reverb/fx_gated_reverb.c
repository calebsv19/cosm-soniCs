
// fx_gated_reverb.c — Nonlinear "gated" reverb (reverb tail chopped by gate envelope)
//
// Design:
//  - Short dense tail followed by envelope gate (threshold + hold + release).
//  - Distinct 80s snare vibe; different enough to warrant separate module.
//  - Internally a tiny Schroeder-ish tank -> envelope follower -> gain VCA.
//
// Params:
//   0: size         (0.6..1.4, default 0.9)
//   1: decay_rt60   (0.2..4.0, default 1.2) — pre-gate tank length
//   2: thresh_dB    (-60..0,   default -24)
//   3: hold_ms      (0..500,   default 120)
//   4: release_ms   (5..800,   default 220)
//   5: mix          (0..1,     default 0.35)
//
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float db2lin(float dB){ return powf(10.f, dB/20.f); }
static inline float fabsf_safe(float x){ return x<0.f?-x:x; }

typedef struct DelayLine { float* buf; int len; int w; } DelayLine;
static inline float dl_read(const DelayLine* d, int tap){ int r=d->w-tap; while(r<0) r+=d->len; return d->buf[r]; }
static inline void  dl_write(DelayLine* d, float x){ d->buf[d->w]=x; d->w++; if(d->w>=d->len) d->w=0; }
static inline void  dl_clear(DelayLine* d){ if(!d||!d->buf) return; for(int i=0;i<d->len;++i)d->buf[i]=0.f; d->w=0; }
static int  alloc_dl(DelayLine* d, int len){ d->buf=(float*)calloc((size_t)len,sizeof(float)); if(!d->buf) return 0; d->len=len; d->w=0; return 1; }

typedef struct AP { DelayLine dl; float g; } AP;
typedef struct CB { DelayLine dl; float g; } CB;

typedef struct FxGatedVerb {
    uint32_t sr; unsigned max_ch;
    float size, decay_rt60, thresh_dB, hold_ms, release_ms, mix;
    // tank (very small): 2 combs -> 2 allpasses
    CB cbL[2], cbR[2];
    AP apL[2], apR[2];
    int tap_cb[2];
    int tap_ap[2];
    // gate env state per side
    float envL, envR;
    int hold_ctr_L, hold_ctr_R;
    float rel_coeff;
} FxGatedVerb;

static const int CB_BASE_48K[2] = { 731,  887 };
static const int AP_BASE_48K[2] = { 171,  229 };
static const int SPREAD_48K = 13;

static int scale_tap(int base48, float sr, float size, int spread, int right){
    int v = (int)(base48 * (sr/48000.f) * size + 0.5f);
    if (right) v += (int)(spread * (sr/48000.f) + 0.5f);
    if (v<1) v=1; return v;
}

static inline float rt60_to_g(float delay_s, float rt60){
    if (rt60 <= 1e-5f) rt60 = 1e-5f;
    float g = powf(10.f, (-3.f * delay_s) / rt60);
    if (g > 0.9995f) g = 0.9995f;
    if (g < 0.0f) g = 0.0f;
    return g;
}

static void gv_recalc(FxGatedVerb* v){
    float sr = (float)v->sr;
    float size = clampf(v->size, 0.6f, 1.4f);
    for(int i=0;i<2;++i){
        v->tap_cb[i] = scale_tap(CB_BASE_48K[i], sr, size, SPREAD_48K, 0);
        v->tap_ap[i] = scale_tap(AP_BASE_48K[i], sr, size, SPREAD_48K, 0);
        v->cbL[i].g = v->cbR[i].g = rt60_to_g(v->tap_cb[i]/sr, v->decay_rt60);
        v->apL[i].g = v->apR[i].g = 0.6f;
    }
    float rel_t = clampf(v->release_ms, 5.f, 800.f) * 0.001f;
    v->rel_coeff = expf(-1.f / (rel_t * sr));
}

static inline float cb_tick(CB* c, float x){ float y=dl_read(&c->dl,c->dl.len-1); dl_write(&c->dl, x + c->g*y); return y; }
static inline float ap_tick(AP* a, float x){ float d=dl_read(&a->dl,a->dl.len-1); float y=-a->g*x + d; dl_write(&a->dl, x + a->g*y); return y; }

static void gv_set_param(FxHandle* h, uint32_t idx, float v){
    FxGatedVerb* g=(FxGatedVerb*)h;
    switch(idx){
        case 0: g->size       = clampf(v, 0.6f, 1.4f); break;
        case 1: g->decay_rt60 = clampf(v, 0.2f, 4.0f); break;
        case 2: g->thresh_dB  = clampf(v, -60.f, 0.f); break;
        case 3: g->hold_ms    = clampf(v, 0.f, 500.f); break;
        case 4: g->release_ms = clampf(v, 5.f, 800.f); break;
        case 5: g->mix        = clampf(v, 0.f, 1.f);   break;
        default: break;
    }
    gv_recalc(g);
}
static void gv_reset(FxHandle* h){
    FxGatedVerb* g=(FxGatedVerb*)h;
    for(int i=0;i<2;++i){ dl_clear(&g->cbL[i].dl); dl_clear(&g->cbR[i].dl); dl_clear(&g->apL[i].dl); dl_clear(&g->apR[i].dl); }
    g->envL=g->envR=0.f; g->hold_ctr_L=g->hold_ctr_R=0;
}
static void gv_destroy(FxHandle* h){
    FxGatedVerb* g=(FxGatedVerb*)h;
    for(int i=0;i<2;++i){ free(g->cbL[i].dl.buf); free(g->cbR[i].dl.buf); free(g->apL[i].dl.buf); free(g->apR[i].dl.buf); }
    free(h);
}

static void gv_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxGatedVerb* g=(FxGatedVerb*)h;
    if (channels<1) return;
    if (channels>2) channels=2;
    const float wet=g->mix, dry=1.f-wet;
    const float thr = db2lin(g->thresh_dB);
    const int hold_samp = (int)(g->hold_ms * 0.001f * (float)g->sr + 0.5f);

    for(int n=0;n<frames;++n){
        float xl = out[n*channels + 0];
        float xr = (channels>=2)? out[n*channels + 1] : xl;

        // tiny tank
        float yl = xl, yr = xr;
        for(int i=0;i<2;++i){ yl = cb_tick(&g->cbL[i], yl); yr = cb_tick(&g->cbR[i], yr); }
        for(int i=0;i<2;++i){ yl = ap_tick(&g->apL[i], yl); yr = ap_tick(&g->apR[i], yr); }

        // envelope follower (abs then smooth release)
        float el = fabsf_safe(yl);
        float er = fabsf_safe(yr);
        // attack instant for snappiness; release via coeff
        g->envL = (el > g->envL) ? el : (g->envL * g->rel_coeff);
        g->envR = (er > g->envR) ? er : (g->envR * g->rel_coeff);

        // gate: open if above threshold or holding; otherwise attenuate to 0
        if (g->envL >= thr) g->hold_ctr_L = hold_samp;
        if (g->envR >= thr) g->hold_ctr_R = hold_samp;

        float gateL = (g->hold_ctr_L > 0) ? 1.f : 0.f;
        float gateR = (g->hold_ctr_R > 0) ? 1.f : 0.f;
        if (g->hold_ctr_L > 0) g->hold_ctr_L--;
        if (g->hold_ctr_R > 0) g->hold_ctr_R--;

        yl *= gateL;
        yr *= gateR;

        out[n*channels + 0] = dry*xl + wet*yl;
        if (channels>=2) out[n*channels + 1] = dry*xr + wet*yr;
    }
}

int gated_reverb_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "GatedReverb";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 6;
    out->param_names[0]    = "size";
    out->param_names[1]    = "decay_rt60";
    out->param_names[2]    = "thresh_dB";
    out->param_names[3]    = "hold_ms";
    out->param_names[4]    = "release_ms";
    out->param_names[5]    = "mix";
    out->param_defaults[0] = 0.9f;
    out->param_defaults[1] = 1.2f;
    out->param_defaults[2] = -24.0f;
    out->param_defaults[3] = 120.0f;
    out->param_defaults[4] = 220.0f;
    out->param_defaults[5] = 0.35f;
    out->latency_samples = 0;
    return 1;
}

int gated_reverb_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                        uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxGatedVerb* g=(FxGatedVerb*)calloc(1,sizeof(FxGatedVerb));
    if(!g) return 0;
    g->sr=sample_rate; g->max_ch=max_channels?max_channels:2;
    g->size=0.9f; g->decay_rt60=1.2f; g->thresh_dB=-24.f; g->hold_ms=120.f; g->release_ms=220.f; g->mix=0.35f;
    // allocate
    for(int i=0;i<2;++i){
        int len_cb = (int)(CB_BASE_48K[i]*(g->sr/48000.f) + 8);
        int len_ap = (int)(AP_BASE_48K[i]*(g->sr/48000.f) + 8);
        if (len_cb<8) len_cb=8; if(len_ap<8) len_ap=8;
        if(!alloc_dl(&g->cbL[i].dl,len_cb) || !alloc_dl(&g->cbR[i].dl,len_cb) ||
           !alloc_dl(&g->apL[i].dl,len_ap) || !alloc_dl(&g->apR[i].dl,len_ap)){
            gv_destroy((FxHandle*)g); return 0;
        }
    }
    gv_recalc(g); gv_reset((FxHandle*)g);
    out_vt->process = gv_process;
    out_vt->set_param = gv_set_param;
    out_vt->reset = gv_reset;
    out_vt->destroy = gv_destroy;
    *out_handle = (FxHandle*)g;
    return 1;
}
