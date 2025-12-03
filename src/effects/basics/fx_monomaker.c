
// fx_monomaker.c — Low-frequency mono-maker with crossover (interleaved, in-place)
// Params:
//   0: crossover_hz (60..500) — below this, sum to mono (mid-only), above, pass-through
//   1: slope        (1..4)    — number of 1st-order stages for steeper split
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}

typedef struct OnePole { float a,b; float zL, zR; } OnePole;

typedef struct FxMonoMaker{
    float sr; unsigned max_channels;
    float fc; int slope;
    OnePole lp[4]; // up to 4 cascaded stages
    OnePole hp[4];
} FxMonoMaker;

static void onepole_lp(OnePole* p, float sr, float hz){
    float fc = clampf(hz, 30.0f, sr*0.49f);
    float x = expf(-2.0f*(float)M_PI*fc/sr);
    p->a = 1.0f - x; p->b = x; p->zL = p->zR = 0.0f;
}
static void onepole_hp(OnePole* p, float sr, float hz){
    // derive from LP as y = x - lp(x)
    float fc = clampf(hz, 30.0f, sr*0.49f);
    float x = expf(-2.0f*(float)M_PI*fc/sr);
    p->a = 1.0f - x; p->b = x; p->zL = p->zR = 0.0f;
}

static float lp_tickL(OnePole* p, float x){ float y = p->a*x + p->b*p->zL; p->zL = y; return y; }
static float lp_tickR(OnePole* p, float x){ float y = p->a*x + p->b*p->zR; p->zR = y; return y; }
static float hp_tickL(OnePole* p, float x){ float lp = p->a*x + p->b*p->zL; p->zL = lp; return x - lp; }
static float hp_tickR(OnePole* p, float x){ float lp = p->a*x + p->b*p->zR; p->zR = lp; return x - lp; }

static void mm_set_param(FxHandle* h, uint32_t idx, float value){
    FxMonoMaker* m = (FxMonoMaker*)h;
    switch(idx){
        case 0: m->fc = clampf(value, 60.f, 500.f); break;
        case 1: m->slope = (int)clampf(value, 1.f, 4.f); break;
        default: break;
    }
    for (int i=0;i<4;++i){ onepole_lp(&m->lp[i], m->sr, m->fc); onepole_hp(&m->hp[i], m->sr, m->fc); }
}
static void mm_reset(FxHandle* h){
    FxMonoMaker* m = (FxMonoMaker*)h;
    for (int i=0;i<4;++i){ m->lp[i].zL=m->lp[i].zR=0.f; m->hp[i].zL=m->hp[i].zR=0.f; }
}
static void mm_destroy(FxHandle* h){ free(h); }

static void mm_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxMonoMaker* m = (FxMonoMaker*)h;
    if (channels < 2) return; // only meaningful for stereo

    int N = m->slope;
    for (int n=0;n<frames;++n){
        int base = n*channels;
        float L = out[base+0];
        float R = out[base+1];

        // split
        float Llp=L, Rlp=R, Lhp=L, Rhp=R;
        for (int i=0;i<N;++i){
            Llp = lp_tickL(&m->lp[i], Llp);
            Rlp = lp_tickR(&m->lp[i], Rlp);
            Lhp = hp_tickL(&m->hp[i], Lhp);
            Rhp = hp_tickR(&m->hp[i], Rhp);
        }

        // mono the lows (mid only)
        float MidLow = 0.5f * (Llp + Rlp);
        // reconstruct
        out[base+0] = MidLow + Lhp;
        out[base+1] = MidLow + Rhp;
    }
}

int monomaker_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "MonoMakerLow";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "crossover_hz";
    out->param_names[1] = "slope";
    out->param_defaults[0] = 150.0f;
    out->param_defaults[1] = 2.0f;
    out->latency_samples = 0;
    return 1;
}

int monomaker_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxMonoMaker* m = (FxMonoMaker*)calloc(1,sizeof(FxMonoMaker));
    if(!m) return 0;
    m->sr = (float)sample_rate; m->max_channels = max_channels?max_channels:2;
    m->fc = 150.0f; m->slope = 2;
    for (int i=0;i<4;++i){ onepole_lp(&m->lp[i], m->sr, m->fc); onepole_hp(&m->hp[i], m->sr, m->fc); }
    mm_reset((FxHandle*)m);
    out_vt->process = mm_process;
    out_vt->set_param = mm_set_param;
    out_vt->reset = mm_reset;
    out_vt->destroy = mm_destroy;
    *out_handle = (FxHandle*)m;
    return 1;
}
