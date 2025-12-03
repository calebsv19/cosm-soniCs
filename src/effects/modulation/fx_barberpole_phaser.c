// fx_barberpole_phaser.c — Barberpole Phaser (continuous rise/fall illusion)
// Cheap approximation: multi‑stage one‑pole allpass chain whose per‑stage
// cutoff sweeps logarithmically between [min_Hz, max_Hz] with phase offsets.
// The positions wrap (fract), yielding a seamless rise. Feedback + mix included.
//
// Params:
//   0: rate_hz    (0.05..3,    default 0.5)
//   1: depth      (0..1,       default 1.0)      — sweep span within [min,max]
//   2: min_Hz     (100..600,   default 200)
//   3: max_Hz     (800..4000,  default 2000)
//   4: stages     (4..8 even,  default 6)
//   5: feedback   (-0.9..0.9,  default 0.3)
//   6: mix        (0..1,       default 1.0)
//   7: direction  (-1..+1,     default +1)      — +1 rising, -1 falling
//
// RT‑safe, in‑place. Coefficients computed per‑sample (cheap tanf).

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float fractf(float x){ return x - floorf(x); }

typedef struct Allpass1 { float z; } Allpass1;

#define BP_MAX_STAGES 8

typedef struct FxBarber {
    float sr; unsigned max_ch;
    // params
    float rate_hz, depth, min_Hz, max_Hz;
    int   stages;
    float feedback;
    float mix;
    float direction;
    // state
    float pos; // running position (unwrapped), wrap via fract
    Allpass1 ap[2][BP_MAX_STAGES];
    float fb_state[2];
} FxBarber;

static inline float ap_coeff_from_freq(float sr, float f){
    float w = (float)M_PI * (f / sr) * 2.f;
    float t = tanf(w * 0.5f);
    if (t >= 0.9999f) t = 0.9999f;
    float a = (1.f - t) / (1.f + t);
    if (a > 0.9999f) a = 0.9999f;
    if (a < -0.9999f) a = -0.9999f;
    return a;
}
static inline float ap1_run(Allpass1* s, float a, float x){
    float y = -a*x + s->z;
    s->z = x + a*y;
    return y;
}

static void bb_set_param(FxHandle* h, uint32_t idx, float v){
    FxBarber* b=(FxBarber*)h;
    switch(idx){
        case 0: b->rate_hz = clampf(v, 0.05f, 3.f); break;
        case 1: b->depth   = clampf(v, 0.f, 1.f);   break;
        case 2: b->min_Hz  = clampf(v, 100.f, 600.f); break;
        case 3: b->max_Hz  = clampf(v, 800.f, 4000.f); break;
        case 4: { int st=(int)roundf(v); if(st<4)st=4; if(st>8)st=8; if(st&1)++st; b->stages=st; } break;
        case 5: b->feedback= clampf(v, -0.9f, 0.9f); break;
        case 6: b->mix     = clampf(v, 0.f, 1.f);    break;
        case 7: b->direction = (v<0.f?-1.f:1.f); break;
        default: break;
    }
}
static void bb_reset(FxHandle* h){
    FxBarber* b=(FxBarber*)h;
    b->pos = 0.f;
    for(unsigned c=0;c<b->max_ch && c<2;++c){
        for(int s=0;s<BP_MAX_STAGES;++s) b->ap[c][s].z = 0.f;
        b->fb_state[c] = 0.f;
    }
}
static void bb_destroy(FxHandle* h){ free(h); }

static void bb_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxBarber* b=(FxBarber*)h;
    int ch = channels; if (ch < 1) return;
    if (ch > 2) ch = 2;

    float pos = b->pos;
    const float inc = (b->rate_hz / b->sr) * (b->direction >= 0.f ? 1.f : -1.f);
    const float wet=b->mix, dry=1.f-wet;
    const int stages=b->stages;
    const float minF=b->min_Hz, maxF=b->max_Hz;
    const float span = logf(maxF/minF) * b->depth; // logarithmic sweep span

    for(int n=0;n<frames;++n){
        float base = fractf(pos);
        // per-sample we compute each stage's frequency
        for(int c=0;c<ch;++c){
            int idx = n*channels + c;
            float x = out[idx] + b->feedback * b->fb_state[c];
            float y = x;
            for(int s=0;s<stages;++s){
                float ps = fractf(base + (float)s/(float)stages);
                // map ps ∈ [0,1) to freq in [minF, minF*exp(span)]
                float f = minF * expf(span * (ps*2.f - 1.f) * 0.5f + span*0.5f);
                if (f < 40.f) f = 40.f; if (f > 6000.f) f = 6000.f;
                float a = ap_coeff_from_freq(b->sr, f);
                y = ap1_run(&b->ap[c][s], a, y);
            }
            b->fb_state[c] = y;
            out[idx] = dry*x + wet*y;
        }
        // fold extras
        for(int c=2;c<channels;++c){
            int idx = n*channels + c;
            out[idx] = out[n*channels + (c&1)];
        }
        pos += inc;
        if (pos > 1e9f || pos < -1e9f) pos = fractf(pos);
    }
    b->pos = pos;
}

int barberpole_phaser_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "BarberpolePhaser";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 8;
    out->param_names[0]    = "rate_hz";
    out->param_names[1]    = "depth";
    out->param_names[2]    = "min_Hz";
    out->param_names[3]    = "max_Hz";
    out->param_names[4]    = "stages";
    out->param_names[5]    = "feedback";
    out->param_names[6]    = "mix";
    out->param_names[7]    = "direction";
    out->param_defaults[0] = 0.5f;
    out->param_defaults[1] = 1.0f;
    out->param_defaults[2] = 200.0f;
    out->param_defaults[3] = 2000.0f;
    out->param_defaults[4] = 6.0f;
    out->param_defaults[5] = 0.3f;
    out->param_defaults[6] = 1.0f;
    out->param_defaults[7] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int barberpole_phaser_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                             uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxBarber* b=(FxBarber*)calloc(1,sizeof(FxBarber));
    if(!b) return 0;
    b->sr=(float)sample_rate; b->max_ch = max_channels?max_channels:2;
    b->rate_hz=0.5f; b->depth=1.0f; b->min_Hz=200.f; b->max_Hz=2000.f;
    b->stages=6; b->feedback=0.3f; b->mix=1.0f; b->direction=1.0f; b->pos=0.f;
    bb_reset((FxHandle*)b);
    out_vt->process = bb_process;
    out_vt->set_param = bb_set_param;
    out_vt->reset = bb_reset;
    out_vt->destroy = bb_destroy;
    *out_handle = (FxHandle*)b;
    return 1;
}
