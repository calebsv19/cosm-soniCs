// fx_decimator.c — Downsampler / Bit‑Depth Reducer with jitter and optional post LPF
//
// Params:
//   0: hold_N          (1..64,   default 6)     — sample & hold length in samples per channel (1 = no SR reduction)
//   1: bit_depth       (4..24,   default 10)    — target bit depth (24 ~ nearly bypass quantizer)
//   2: jitter          (0..1,    default 0.15)  — white noise added pre‑hold to de‑glass aliasing
//   3: post_lowpass_Hz (1000..20000, default 9000)
//   4: mix             (0..1,    default 1.0)
//
// Implementation:
// - Per‑channel sample‑and‑hold counter; when it hits 0 we sample x + jitter and reset to N.
// - Quantizer applied each sample (or we can quantize only on new holds; we quantize held value).
// - Jitter amplitude is proportional to 1/(2^bit_depth) for musically useful dither.
//
// Real‑time safe: prealloc only small per‑channel state; no big buffers.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct FxDecimator {
    float sr; unsigned max_ch;
    int   hold_N;
    int   bit_depth;
    float jitter;
    float lp_Hz;
    float mix;
    // per‑channel state
    int   *ctr;
    float *held;
    float *lp; // one‑pole post low‑pass memory
    float lpf_a;
    // rng state
    unsigned int rng;
} FxDecimator;

static inline float frand01(unsigned int* s){ *s = 1664525u*(*s) + 1013904223u; return ((*s)>>8) * (1.0f/16777216.0f); }

static void de_update_lpf(FxDecimator* d){
    float fc = d->lp_Hz;
    if (fc < 1000.f) fc = 1000.f;
    if (fc > 20000.f) fc = 20000.f;
    float a = expf(-2.f*(float)M_PI * fc / d->sr);
    d->lpf_a = a; // y = (1-a)*x + a*y_prev
}

static float quantize(float x, int bits){
    if (bits >= 24) return x;
    if (bits < 1) bits = 1;
    float Q = (float)(1 << (bits - 1)); // map -1..1 to ~[-Q,Q]
    float y = roundf(x * Q) / Q;
    // prevent NaNs from inf/overflow
    if (!isfinite(y)) y = 0.f;
    return y;
}

static void de_set_param(FxHandle* h, uint32_t idx, float v){
    FxDecimator* d=(FxDecimator*)h;
    switch(idx){
        case 0: d->hold_N   = (int)roundf(clampf(v, 1.f, 64.f)); break;
        case 1: d->bit_depth= (int)roundf(clampf(v, 4.f, 24.f)); break;
        case 2: d->jitter   = clampf(v, 0.f, 1.f); break;
        case 3: d->lp_Hz    = clampf(v, 1000.f, 20000.f); de_update_lpf(d); break;
        case 4: d->mix      = clampf(v, 0.f, 1.f); break;
        default: break;
    }
}
static void de_reset(FxHandle* h){
    FxDecimator* d=(FxDecimator*)h;
    for(unsigned c=0;c<d->max_ch;++c){ d->ctr[c]=0; d->held[c]=0.f; d->lp[c]=0.f; }
    d->rng = 22222u;
}
static void de_destroy(FxHandle* h){
    FxDecimator* d=(FxDecimator*)h;
    free(d->ctr); free(d->held); free(d->lp);
    free(d);
}

static void de_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxDecimator* d=(FxDecimator*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > d->max_ch) ch = (int)d->max_ch;
    const float wet = d->mix, dry = 1.f - wet;

    // jit amplitude roughly one LSB of the target bit depth
    float lsb = (d->bit_depth >= 24) ? 0.f : 1.0f / (float)(1 << (d->bit_depth - 1));
    float jit_amp = d->jitter * lsb;

    for(int n=0;n<frames;++n){
        for(int c=0;c<ch;++c){
            int i = n*channels + c;
            float x = out[i];

            if (d->ctr[c] <= 0){
                float noise = (frand01(&d->rng) * 2.f - 1.f) * jit_amp;
                float s = x + noise;
                // quantize on latch
                s = quantize(s, d->bit_depth);
                d->held[c] = s;
                d->ctr[c] = d->hold_N;
            }
            d->ctr[c]--;

            float y = d->held[c];

            // simple one‑pole post LPF to tame edges
            y = (1.f - d->lpf_a)*y + d->lpf_a * d->lp[c];
            d->lp[c] = y;

            out[i] = dry*x + wet*y;
        }
        for(int c=d->max_ch; c<channels; ++c){
            int i = n*channels + c;
            out[i] = out[n*channels + (c % ch)];
        }
    }
}

int decimator_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "Decimator";
    out->api_version = FX_API_VERSION;
    out->flags = 0;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 5;
    out->param_names[0]    = "hold_N";
    out->param_names[1]    = "bit_depth";
    out->param_names[2]    = "jitter";
    out->param_names[3]    = "post_lowpass_Hz";
    out->param_names[4]    = "mix";
    out->param_defaults[0] = 6.0f;
    out->param_defaults[1] = 10.0f;
    out->param_defaults[2] = 0.15f;
    out->param_defaults[3] = 9000.0f;
    out->param_defaults[4] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int decimator_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxDecimator* d=(FxDecimator*)calloc(1,sizeof(FxDecimator));
    if(!d) return 0;
    d->sr=(float)sample_rate; d->max_ch = max_channels?max_channels:2;
    d->hold_N=6; d->bit_depth=10; d->jitter=0.15f; d->lp_Hz=9000.f; d->mix=1.f;
    d->ctr  = (int*)  calloc(d->max_ch, sizeof(int));
    d->held = (float*)calloc(d->max_ch, sizeof(float));
    d->lp   = (float*)calloc(d->max_ch, sizeof(float));
    if(!d->ctr || !d->held || !d->lp){ free(d->ctr); free(d->held); free(d->lp); free(d); return 0; }
    de_update_lpf(d); de_reset((FxHandle*)d);
    out_vt->process = de_process;
    out_vt->set_param = de_set_param;
    out_vt->reset = de_reset;
    out_vt->destroy = de_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
