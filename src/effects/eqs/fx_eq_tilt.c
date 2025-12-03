// fx_eq_tilt.c — Tilt EQ: complementary low/high shelves around a pivot
// Positive tilt boosts highs and cuts lows; negative does the reverse.
//
// Params:
//   0: tilt_dB      (-12..+12, default 0)  — total tilt spread (± applied symmetrically)
//   1: pivot_Hz     (200..2000, default 650)
//
// Implemented as two shelves with ±tilt/2 gain. Coeffs recomputed in set_param.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }

typedef struct Biquad { float b0,b1,b2,a1,a2,z1,z2; } Biquad;

static void biq_reset(Biquad* b){ b->z1 = b->z2 = 0.f; }
static float biq_run(Biquad* b, float x){
    float y = b->b0*x + b->z1;
    b->z1 = b->b1*x - b->a1*y + b->z2;
    b->z2 = b->b2*x - b->a2*y;
    return y;
}

static void make_lowshelf(Biquad* b, float sr, float freq, float gain_dB){
    const float A  = db_to_lin(gain_dB);
    const float w0 = 2.f*(float)M_PI * freq / sr;
    const float cosw0 = cosf(w0), sinw0 = sinf(w0);
    const float S = 1.0f;
    const float alpha = sinw0/2.f * sqrtf( (A + 1/A)*(1/S - 1) + 2 );
    const float Ap = A + 1.f, Am = A - 1.f;
    const float b0 =    A*( (Ap - Am*cosw0) + 2.f*sqrtf(A)*alpha );
    const float b1 =  2*A*( (Am - Ap*cosw0) );
    const float b2 =    A*( (Ap - Am*cosw0) - 2.f*sqrtf(A)*alpha );
    const float a0 =        (Ap + Am*cosw0) + 2.f*sqrtf(A)*alpha;
    const float a1 =   -2.f*( (Am + Ap*cosw0) );
    const float a2 =        (Ap + Am*cosw0) - 2.f*sqrtf(A)*alpha;

    b->b0 = b0/a0; b->b1 = b1/a0; b->b2 = b2/a0;
    b->a1 = a1/a0; b->a2 = a2/a0;
}
static void make_highshelf(Biquad* b, float sr, float freq, float gain_dB){
    const float A  = db_to_lin(gain_dB);
    const float w0 = 2.f*(float)M_PI * freq / sr;
    const float cosw0 = cosf(w0), sinw0 = sinf(w0);
    const float S = 1.0f;
    const float alpha = sinw0/2.f * sqrtf( (A + 1/A)*(1/S - 1) + 2 );
    const float Ap = A + 1.f, Am = A - 1.f;
    const float b0 =    A*( (Ap + Am*cosw0) + 2.f*sqrtf(A)*alpha );
    const float b1 = -2*A*( (Am + Ap*cosw0) );
    const float b2 =    A*( (Ap + Am*cosw0) - 2.f*sqrtf(A)*alpha );
    const float a0 =        (Ap - Am*cosw0) + 2.f*sqrtf(A)*alpha;
    const float a1 =    2.f*( (Am - Ap*cosw0) );
    const float a2 =        (Ap - Am*cosw0) - 2.f*sqrtf(A)*alpha;

    b->b0 = b0/a0; b->b1 = b1/a0; b->b2 = b2/a0;
    b->a1 = a1/a0; b->a2 = a2/a0;
}

typedef struct FxTilt {
    float sr; unsigned max_ch;
    float tilt_dB;
    float pivot_Hz;
    Biquad *ls, *hs;
} FxTilt;

static void tilt_recalc(FxTilt* t){
    float half = 0.5f * t->tilt_dB;
    for(unsigned c=0;c<t->max_ch;++c){
        make_lowshelf(&t->ls[c], t->sr, t->pivot_Hz, -half);
        make_highshelf(&t->hs[c], t->sr, t->pivot_Hz, +half);
        biq_reset(&t->ls[c]); biq_reset(&t->hs[c]);
    }
}
static void tilt_set_param(FxHandle* h, uint32_t idx, float v){
    FxTilt* t = (FxTilt*)h;
    switch(idx){
        case 0: t->tilt_dB = clampf(v, -12.f, 12.f); break;
        case 1: t->pivot_Hz= clampf(v, 200.f, 2000.f); break;
        default: break;
    }
    tilt_recalc(t);
}
static void tilt_reset(FxHandle* h){
    FxTilt* t=(FxTilt*)h;
    for(unsigned c=0;c<t->max_ch;++c){ biq_reset(&t->ls[c]); biq_reset(&t->hs[c]); }
}
static void tilt_destroy(FxHandle* h){
    FxTilt* t=(FxTilt*)h;
    free(t->ls); free(t->hs); free(t);
}

static void tilt_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxTilt* t=(FxTilt*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > t->max_ch) ch = (int)t->max_ch;

    for(int n=0;n<frames;++n){
        int i = n*ch;
        for(int c=0;c<ch;++c){
            float x = out[i+c];
            x = biq_run(&t->ls[c], x);
            x = biq_run(&t->hs[c], x);
            out[i+c] = x;
        }
    }
}

int eq_tilt_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "EQ_Tilt";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 2;
    out->param_names[0]    = "tilt_dB";
    out->param_names[1]    = "pivot_Hz";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 650.0f;
    out->latency_samples = 0;
    return 1;
}

int eq_tilt_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxTilt* t=(FxTilt*)calloc(1,sizeof(FxTilt));
    if(!t) return 0;
    t->sr = (float)sample_rate;
    t->max_ch = max_channels?max_channels:2;
    t->tilt_dB = 0.f; t->pivot_Hz = 650.f;
    t->ls = (Biquad*)calloc(t->max_ch, sizeof(Biquad));
    t->hs = (Biquad*)calloc(t->max_ch, sizeof(Biquad));
    if(!t->ls || !t->hs){ free(t->ls); free(t->hs); free(t); return 0; }
    tilt_recalc(t);
    out_vt->process   = tilt_process;
    out_vt->set_param = tilt_set_param;
    out_vt->reset     = tilt_reset;
    out_vt->destroy   = tilt_destroy;
    *out_handle = (FxHandle*)t;
    return 1;
}
