// fx_phaser.c — Multi‑stage phaser (4/6/8 one‑pole allpass), sine LFO, feedback, mix.
// In-place, RT-safe. Coeffs updated per-sample from LFO (cheap trig).
//
// Params:
//   0: rate_hz    (0.05..5,  default 0.6)
//   1: depth      (0..1,     default 0.9)         — sweep depth
//   2: center_Hz  (200..1800,default 700)         — sweep center
//   3: stages     (4..8,     default 6)           — even only: 4,6,8
//   4: feedback   (-0.95..0.95, default 0.5)      — resonance
//   5: mix        (0..1,     default 1.0)
//
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct Allpass1 {
    float a;
    float z;
} Allpass1;

#define PHASER_MAX_STAGES 8

typedef struct FxPhaser {
    float sr; unsigned max_ch;
    // params
    float rate_hz, depth, center_Hz;
    int   stages;
    float feedback;
    float mix;
    // state
    float phase; // LFO 0..1
    Allpass1 ap[2][PHASER_MAX_STAGES]; // [ch][stage], z only (a is shared per-sample)
    float fb_state[2]; // per-channel feedback sample
} FxPhaser;

static void ap_reset(Allpass1* a){ a->z = 0.f; }
static inline float ap_process(Allpass1* s, float a, float x){
    // One-pole allpass (BLT): y = -a*x + z; z = x + a*y
    float y = -a*x + s->z;
    s->z = x + a*y;
    return y;
}

// compute one-pole allpass coefficient from cutoff frequency (approx)
// a = (1 - tan(w/2)) / (1 + tan(w/2))
static inline float ap_coeff_from_freq(float sr, float f){
    float w = (float)M_PI * (f / sr) * 2.f;
    float t = tanf(w * 0.5f);
    if (t >= 0.9999f) t = 0.9999f;
    float a = (1.f - t) / (1.f + t);
    if (a > 0.9999f) a = 0.9999f;
    if (a < -0.9999f) a = -0.9999f;
    return a;
}

static void ph_set_param(FxHandle* h, uint32_t idx, float v){
    FxPhaser* p=(FxPhaser*)h;
    switch(idx){
        case 0: p->rate_hz   = clampf(v, 0.05f, 5.f); break;
        case 1: p->depth     = clampf(v, 0.f, 1.f);   break;
        case 2: p->center_Hz = clampf(v, 200.f, 1800.f); break;
        case 3: {
            int st = (int)roundf(v);
            if (st < 4) st = 4; if (st > 8) st = 8;
            if (st % 2) st += 1;
            p->stages = st; break;
        }
        case 4: p->feedback  = clampf(v, -0.95f, 0.95f); break;
        case 5: p->mix       = clampf(v, 0.f, 1.f); break;
        default: break;
    }
}
static void ph_reset(FxHandle* h){
    FxPhaser* p=(FxPhaser*)h;
    p->phase = 0.f;
    for(unsigned c=0;c<p->max_ch && c<2;++c){
        for(int s=0;s<PHASER_MAX_STAGES;++s) ap_reset(&p->ap[c][s]);
        p->fb_state[c] = 0.f;
    }
}
static void ph_destroy(FxHandle* h){ free(h); }

static void ph_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxPhaser* p=(FxPhaser*)h;
    int ch = channels; if (ch < 1) return;
    if (ch > 2) ch = 2; // process first two channels; fold extras
    float phase = p->phase;
    const float inc = p->rate_hz / p->sr;
    const float center = p->center_Hz;
    const float depth = p->depth;
    const float wet = p->mix, dry = 1.f - wet;
    const int stages = p->stages;

    for(int n=0;n<frames;++n){
        float lfo = 0.5f + 0.5f*sinf(2.f*(float)M_PI * phase); // 0..1
        // sweep range ~2 octaves around center
        float f = center * powf(2.f, (depth * (lfo*2.f - 1.f))); // center * 2^(±depth)
        if (f < 40.f) f = 40.f; if (f > 5000.f) f = 5000.f;
        float a = ap_coeff_from_freq(p->sr, f);

        for(int c=0;c<ch;++c){
            int i = n*channels + c;
            float x = out[i];
            x += p->feedback * p->fb_state[c];

            float y = x;
            for(int s=0;s<stages;++s){
                y = ap_process(&p->ap[c][s], a, y);
            }
            p->fb_state[c] = y;

            out[i] = dry*x + wet*y;
        }
        // fold extra channels if any
        for(int c=2;c<channels;++c){
            int i = n*channels + c;
            out[i] = out[n*channels + (c&1)]; // mirror L/R
        }
        phase += inc;
        if (phase >= 1.f) phase -= 1.f;
        if (phase < 0.f)  phase += 1.f;
    }
    p->phase = phase;
}

int phaser_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "Phaser";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 6;
    out->param_names[0]    = "rate_hz";
    out->param_names[1]    = "depth";
    out->param_names[2]    = "center_Hz";
    out->param_names[3]    = "stages";
    out->param_names[4]    = "feedback";
    out->param_names[5]    = "mix";
    out->param_defaults[0] = 0.6f;
    out->param_defaults[1] = 0.9f;
    out->param_defaults[2] = 700.0f;
    out->param_defaults[3] = 6.0f;
    out->param_defaults[4] = 0.5f;
    out->param_defaults[5] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int phaser_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                  uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxPhaser* p=(FxPhaser*)calloc(1,sizeof(FxPhaser));
    if(!p) return 0;
    p->sr=(float)sample_rate; p->max_ch = max_channels?max_channels:2;
    p->rate_hz=0.6f; p->depth=0.9f; p->center_Hz=700.f; p->stages=6; p->feedback=0.5f; p->mix=1.f;
    ph_reset((FxHandle*)p);
    out_vt->process = ph_process;
    out_vt->set_param = ph_set_param;
    out_vt->reset = ph_reset;
    out_vt->destroy = ph_destroy;
    *out_handle = (FxHandle*)p;
    return 1;
}
