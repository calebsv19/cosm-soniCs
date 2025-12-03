// fx_eq_fixed3.c — Fixed 3‑Band EQ (Low Shelf @100Hz, Peak @1kHz (Q param), High Shelf @8kHz)
// Friendly wrapper for common moves, using RBJ biquad formulas. In-place, RT-safe.
//
// Params:
//   0: low_gain_dB   (-15..+15, default 0)
//   1: mid_gain_dB   (-15..+15, default 0)
//   2: high_gain_dB  (-15..+15, default 0)
//   3: mid_Q         (0.3..4.0,  default 0.707)
//
// Notes:
// - Linked across channels only by shared params; per-channel filter state.
// - Coeffs recomputed in set_param/reset; no heavy work in process.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }

typedef struct Biquad {
    float b0,b1,b2,a1,a2;
    float z1,z2;
} Biquad;

typedef struct FxFixed3 {
    float sr;
    unsigned ch, max_ch;
    // params
    float low_g, mid_g, high_g; // dB
    float mid_Q;
    // per-channel filters
    Biquad *ls, *pk, *hs;
} FxFixed3;

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
    const float S = 1.0f; // slope=1 simple
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
static void make_peak(Biquad* b, float sr, float freq, float Q, float gain_dB){
    const float A = db_to_lin(gain_dB);
    const float w0 = 2.f*(float)M_PI * freq / sr;
    const float cosw0 = cosf(w0), sinw0 = sinf(w0);
    const float alpha = sinw0/(2.f*Q);

    const float b0 =   1.f + alpha*A;
    const float b1 =  -2.f*cosw0;
    const float b2 =   1.f - alpha*A;
    const float a0 =   1.f + alpha/A;
    const float a1 =  -2.f*cosw0;
    const float a2 =   1.f - alpha/A;

    b->b0 = b0/a0; b->b1 = b1/a0; b->b2 = b2/a0;
    b->a1 = a1/a0; b->a2 = a2/a0;
}

static void f3_update(FxFixed3* f){
    // musical centers
    const float fL = 100.f;
    const float fM = 1000.f;
    const float fH = 8000.f;
    for(unsigned c=0;c<f->max_ch;++c){
        make_lowshelf(&f->ls[c], f->sr, fL, f->low_g);
        biq_reset(&f->ls[c]);
        make_peak(&f->pk[c], f->sr, fM, f->mid_Q, f->mid_g);
        biq_reset(&f->pk[c]);
        make_highshelf(&f->hs[c], f->sr, fH, f->high_g);
        biq_reset(&f->hs[c]);
    }
}

static void f3_set_param(FxHandle* h, uint32_t idx, float v){
    FxFixed3* f = (FxFixed3*)h;
    switch(idx){
        case 0: f->low_g  = clampf(v, -15.f, 15.f); break;
        case 1: f->mid_g  = clampf(v, -15.f, 15.f); break;
        case 2: f->high_g = clampf(v, -15.f, 15.f); break;
        case 3: f->mid_Q  = clampf(v, 0.3f, 4.0f);  break;
        default: break;
    }
    f3_update(f);
}
static void f3_reset(FxHandle* h){
    FxFixed3* f = (FxFixed3*)h;
    for(unsigned c=0;c<f->max_ch;++c){
        biq_reset(&f->ls[c]); biq_reset(&f->pk[c]); biq_reset(&f->hs[c]);
    }
}
static void f3_destroy(FxHandle* h){
    FxFixed3* f = (FxFixed3*)h;
    free(f->ls); free(f->pk); free(f->hs);
    free(f);
}

static void f3_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxFixed3* f = (FxFixed3*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > f->max_ch) ch = (int)f->max_ch;

    for(int n=0;n<frames;++n){
        int i = n*ch;
        for(int c=0;c<ch;++c){
            float x = out[i+c];
            x = biq_run(&f->ls[c], x);
            x = biq_run(&f->pk[c], x);
            x = biq_run(&f->hs[c], x);
            out[i+c] = x;
        }
    }
}

int eq_fixed3_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "EQ_Fixed3";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 4;
    out->param_names[0]    = "low_gain_dB";
    out->param_names[1]    = "mid_gain_dB";
    out->param_names[2]    = "high_gain_dB";
    out->param_names[3]    = "mid_Q";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 0.0f;
    out->param_defaults[2] = 0.0f;
    out->param_defaults[3] = 0.707f;
    out->latency_samples = 0;
    return 1;
}

int eq_fixed3_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxFixed3* f = (FxFixed3*)calloc(1,sizeof(FxFixed3));
    if(!f) return 0;
    f->sr = (float)sample_rate;
    f->max_ch = max_channels?max_channels:2;
    f->low_g=0.f; f->mid_g=0.f; f->high_g=0.f; f->mid_Q=0.707f;
    f->ls = (Biquad*)calloc(f->max_ch, sizeof(Biquad));
    f->pk = (Biquad*)calloc(f->max_ch, sizeof(Biquad));
    f->hs = (Biquad*)calloc(f->max_ch, sizeof(Biquad));
    if(!f->ls||!f->pk||!f->hs){ free(f->ls); free(f->pk); free(f->hs); free(f); return 0; }
    f3_update(f);
    out_vt->process   = f3_process;
    out_vt->set_param = f3_set_param;
    out_vt->reset     = f3_reset;
    out_vt->destroy   = f3_destroy;
    *out_handle = (FxHandle*)f;
    return 1;
}
