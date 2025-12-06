// fx_eq_notch.c — Surgical notch filter (true notch), frequency & Q, depth control
// Useful for killing hums/rings. Implemented via RBJ notch (biquad).
//
// Params:
//   0: freq_Hz   (40..18000, default 60)
//   1: Q         (1..30,     default 10)
//   2: depth_dB  (0..-30,    default -24) — additional attenuation via mix to center tap
//
// Note: RBJ notch has unity passband gain; to deepen the notch we blend a peaking cut.
// For simplicity, we implement depth by mixing a small peaking-cut at freq with same Q.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }

typedef struct Biquad { float b0,b1,b2,a1,a2,z1,z2; } Biquad;

static float biq_run(Biquad* b, float x){
    float y = b->b0*x + b->z1;
    b->z1 = b->b1*x - b->a1*y + b->z2;
    b->z2 = b->b2*x - b->a2*y;
    return y;
}

static void make_notch(Biquad* b, float sr, float freq, float Q){
    const float w0 = 2.f*(float)M_PI * freq / sr;
    const float cosw0 = cosf(w0), sinw0 = sinf(w0);
    const float alpha = sinw0/(2.f*Q);

    const float b0 =  1.f;
    const float b1 = -2.f*cosw0;
    const float b2 =  1.f;
    const float a0 =  1.f + alpha;
    const float a1 = -2.f*cosw0;
    const float a2 =  1.f - alpha;

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

typedef struct FxNotch {
    float sr; unsigned max_ch;
    float freq_Hz, Q, depth_dB;
    Biquad *bn, *pk; // notch + peaking cut
} FxNotch;

static void notch_recalc(FxNotch* n){
    for(unsigned c=0;c<n->max_ch;++c){
        make_notch(&n->bn[c], n->sr, n->freq_Hz, n->Q);
        make_peak (&n->pk[c], n->sr, n->freq_Hz, n->Q, n->depth_dB);
        n->bn[c].z1=n->bn[c].z2=0; n->pk[c].z1=n->pk[c].z2=0;
    }
}
static void notch_set_param(FxHandle* h, uint32_t idx, float v){
    FxNotch* n=(FxNotch*)h;
    switch(idx){
        case 0: n->freq_Hz = clampf(v, 40.f, 18000.f); break;
        case 1: n->Q       = clampf(v, 1.f, 30.f);     break;
        case 2: n->depth_dB= clampf(v, -30.f, 0.f);    break;
        default: break;
    }
    notch_recalc(n);
}
static void notch_reset(FxHandle* h){
    FxNotch* n=(FxNotch*)h;
    for(unsigned c=0;c<n->max_ch;++c){ n->bn[c].z1=n->bn[c].z2=0; n->pk[c].z1=n->pk[c].z2=0; }
}
static void notch_destroy(FxHandle* h){
    FxNotch* n=(FxNotch*)h;
    free(n->bn); free(n->pk); free(n);
}

static void notch_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxNotch* n=(FxNotch*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > n->max_ch) ch = (int)n->max_ch;

    for(int i=0;i<frames;++i){
        int idx = i*ch;
        for(int c=0;c<ch;++c){
            float x = out[idx+c];
            // true notch first, then extra peaking cut for depth
            x = biq_run(&n->bn[c], x);
            x = biq_run(&n->pk[c], x);
            out[idx+c] = x;
        }
    }
}

int eq_notch_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "EQ_Notch";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 3;
    out->param_names[0]    = "freq_Hz";
    out->param_names[1]    = "Q";
    out->param_names[2]    = "depth_dB";
    out->param_defaults[0] = 60.0f;
    out->param_defaults[1] = 10.0f;
    out->param_defaults[2] = -24.0f;
    out->latency_samples = 0;
    return 1;
}

int eq_notch_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxNotch* n=(FxNotch*)calloc(1,sizeof(FxNotch));
    if(!n) return 0;
    n->sr = (float)sample_rate;
    n->max_ch = max_channels?max_channels:2;
    n->freq_Hz = 60.f; n->Q = 10.f; n->depth_dB = -24.f;
    n->bn = (Biquad*)calloc(n->max_ch, sizeof(Biquad));
    n->pk = (Biquad*)calloc(n->max_ch, sizeof(Biquad));
    if(!n->bn || !n->pk){ free(n->bn); free(n->pk); free(n); return 0; }
    notch_recalc(n);
    out_vt->process   = notch_process;
    out_vt->set_param = notch_set_param;
    out_vt->reset     = notch_reset;
    out_vt->destroy   = notch_destroy;
    *out_handle = (FxHandle*)n;
    return 1;
}
