// fx_waveshaper.c — Flexible waveshaper with multiple curves + pre/post mini‑tilt EQ
//
// Curves: 0=tanh, 1=arctan, 2=softclip, 3=foldback
//
// Params:
//   0: curve          (0..3, default 0)
//   1: drive_dB       (0..40, default 18)       — pre‑gain before shaping
//   2: bias           (-1..1, default 0)        — DC offset before shaping
//   3: pre_tilt_dB    (-12..12, default 0)      — low/high shelf pair about pivot (− on lows, + on highs)
//   4: post_tilt_dB   (-12..12, default 0)
//   5: pivot_Hz       (200..2000, default 650)  — tilt pivot
//   6: mix            (0..1, default 1)
//   7: out_gain_dB    (-24..24, default 0)
//
// Implementation notes:
// - Mini‑tilt is two RBJ shelves (±tilt/2) applied pre and post. Per‑channel biquads.
// - Shaper is applied in place, zero latency.
// - Bias lets you push into asymmetry for even harmonics.
//
// Real‑time safe: all allocations at create/reset; no mallocs in process.

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
static inline float biq_run(Biquad* b, float x){
    float y = b->b0*x + b->z1;
    b->z1 = b->b1*x - b->a1*y + b->z2;
    b->z2 = b->b2*x - b->a2*y;
    return y;
}

static inline void make_lowshelf(Biquad* b, float sr, float freq, float gain_dB){
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
static inline void make_highshelf(Biquad* b, float sr, float freq, float gain_dB){
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

typedef struct TiltPair {
    Biquad *ls, *hs; // per‑channel
} TiltPair;

typedef struct FxWaveshaper {
    float sr; unsigned max_ch;
    int curve;
    float drive_dB, bias, pre_tilt_dB, post_tilt_dB, pivot_Hz, mix, out_gain_dB;
    TiltPair pre, post;
} FxWaveshaper;

static void tilt_setup_pair(TiltPair* t, unsigned ch, float sr, float pivot, float tilt_dB){
    float half = 0.5f * tilt_dB;
    for(unsigned c=0;c<ch;++c){
        make_lowshelf(&t->ls[c], sr, pivot, -half);
        make_highshelf(&t->hs[c], sr, pivot, +half);
        biq_reset(&t->ls[c]); biq_reset(&t->hs[c]);
    }
}

static inline float apply_tilt(TiltPair* t, unsigned ch, int c, float x){
    x = biq_run(&t->ls[c], x);
    x = biq_run(&t->hs[c], x);
    return x;
}

static inline float shaper_sample(int curve, float x){
    switch(curve){
        case 0: // tanh
            return tanhf(x);
        case 1: // arctan (scaled)
            return (2.f/M_PI) * atanf((float)M_PI_2 * x);
        case 2: { // soft clip (cubic)
            float a = fabsf(x);
            if (a <= 1.f) return x - (x*x*x)/3.f;
            // hard limit beyond
            return (x>0.f?1.f:-1.f) * 2.f/3.f;
        }
        case 3: { // foldback
            float t = fmodf(fabsf(x)+1.f, 4.f);
            float y = 2.f - fabsf(t - 2.f);
            return copysignf(y-1.f, x);
        }
        default: return x;
    }
}

static void ws_set_param(FxHandle* h, uint32_t idx, float v){
    FxWaveshaper* s=(FxWaveshaper*)h;
    switch(idx){
        case 0: { int c=(int)roundf(v); if(c<0)c=0; if(c>3)c=3; s->curve=c; } break;
        case 1: s->drive_dB     = clampf(v, 0.f, 40.f); break;
        case 2: s->bias         = clampf(v, -1.f, 1.f); break;
        case 3: s->pre_tilt_dB  = clampf(v, -12.f, 12.f); tilt_setup_pair(&s->pre, s->max_ch, s->sr, s->pivot_Hz, s->pre_tilt_dB); break;
        case 4: s->post_tilt_dB = clampf(v, -12.f, 12.f); tilt_setup_pair(&s->post,s->max_ch, s->sr, s->pivot_Hz, s->post_tilt_dB); break;
        case 5: s->pivot_Hz     = clampf(v, 200.f, 2000.f);
                tilt_setup_pair(&s->pre, s->max_ch, s->sr, s->pivot_Hz, s->pre_tilt_dB);
                tilt_setup_pair(&s->post,s->max_ch, s->sr, s->pivot_Hz, s->post_tilt_dB);
                break;
        case 6: s->mix          = clampf(v, 0.f, 1.f); break;
        case 7: s->out_gain_dB  = clampf(v, -24.f, 24.f); break;
        default: break;
    }
}
static void ws_reset(FxHandle* h){
    FxWaveshaper* s=(FxWaveshaper*)h;
    for(unsigned c=0;c<s->max_ch;++c){
        biq_reset(&s->pre.ls[c]);  biq_reset(&s->pre.hs[c]);
        biq_reset(&s->post.ls[c]); biq_reset(&s->post.hs[c]);
    }
}
static void ws_destroy(FxHandle* h){
    FxWaveshaper* s=(FxWaveshaper*)h;
    free(s->pre.ls); free(s->pre.hs);
    free(s->post.ls); free(s->post.hs);
    free(s);
}

static void ws_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxWaveshaper* s=(FxWaveshaper*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > s->max_ch) ch = (int)s->max_ch;

    const float pre_gain  = db_to_lin(s->drive_dB);
    const float post_gain = db_to_lin(s->out_gain_dB);
    const float wet = s->mix, dry = 1.f - wet;

    for(int n=0;n<frames;++n){
        for(int c=0;c<ch;++c){
            int i = n*channels + c;
            float x = out[i];
            float d = x;

            // pre tilt
            d = apply_tilt(&s->pre, s->max_ch, c, d);

            // drive + bias
            d = d * pre_gain + s->bias;

            // shape
            d = shaper_sample(s->curve, d);

            // post tilt
            d = apply_tilt(&s->post, s->max_ch, c, d);

            d *= post_gain;

            out[i] = dry*x + wet*d;
        }
        for(int c=s->max_ch; c<channels; ++c){
            int i = n*channels + c;
            out[i] = out[n*channels + (c % ch)];
        }
    }
}

int waveshaper_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "Waveshaper";
    out->api_version = FX_API_VERSION;
    out->flags = 0;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 8;
    out->param_names[0]    = "curve";
    out->param_names[1]    = "drive_dB";
    out->param_names[2]    = "bias";
    out->param_names[3]    = "pre_tilt_dB";
    out->param_names[4]    = "post_tilt_dB";
    out->param_names[5]    = "pivot_Hz";
    out->param_names[6]    = "mix";
    out->param_names[7]    = "out_gain_dB";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 18.0f;
    out->param_defaults[2] = 0.0f;
    out->param_defaults[3] = 0.0f;
    out->param_defaults[4] = 0.0f;
    out->param_defaults[5] = 650.0f;
    out->param_defaults[6] = 1.0f;
    out->param_defaults[7] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int waveshaper_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                      uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxWaveshaper* s=(FxWaveshaper*)calloc(1,sizeof(FxWaveshaper));
    if(!s) return 0;
    s->sr=(float)sample_rate; s->max_ch = max_channels?max_channels:2;
    s->curve=0; s->drive_dB=18.f; s->bias=0.f;
    s->pre_tilt_dB=0.f; s->post_tilt_dB=0.f; s->pivot_Hz=650.f;
    s->mix=1.f; s->out_gain_dB=0.f;
    s->pre.ls  = (Biquad*)calloc(s->max_ch, sizeof(Biquad));
    s->pre.hs  = (Biquad*)calloc(s->max_ch, sizeof(Biquad));
    s->post.ls = (Biquad*)calloc(s->max_ch, sizeof(Biquad));
    s->post.hs = (Biquad*)calloc(s->max_ch, sizeof(Biquad));
    if(!s->pre.ls||!s->pre.hs||!s->post.ls||!s->post.hs){ free(s->pre.ls); free(s->pre.hs); free(s->post.ls); free(s->post.hs); free(s); return 0; }
    tilt_setup_pair(&s->pre,  s->max_ch, s->sr, s->pivot_Hz, s->pre_tilt_dB);
    tilt_setup_pair(&s->post, s->max_ch, s->sr, s->pivot_Hz, s->post_tilt_dB);
    ws_reset((FxHandle*)s);
    out_vt->process = ws_process;
    out_vt->set_param = ws_set_param;
    out_vt->reset = ws_reset;
    out_vt->destroy = ws_destroy;
    *out_handle = (FxHandle*)s;
    return 1;
}
