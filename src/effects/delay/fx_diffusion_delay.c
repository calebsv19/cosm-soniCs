// fx_diffusion_delay.c — Diffusion delay (allpasses in the feedback path) for smear
//
// Params:
//   0: delay_ms    (20..800, default 120)
//   1: feedback    (0..0.95, default 0.6)
//   2: diffusion   (0..0.9,  default 0.6)  — allpass coefficient
//   3: stages      (1..4,    default 2)    — number of tiny allpass filters
//   4: mix         (0..1,    default 0.35)
//   5: highcut_Hz  (2000..14000, default 8000)
//
// Implementation:
// y = delayed(x + feedback * AP_chain(delayed_y)) where AP are tiny allpasses.
// Each allpass uses a very short internal delay (fixed small taps) for diffusion.
// High-cut applied after AP chain in the feedback path.
//
// Realtime safe: pre-alloc delay lines. In-place process.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct AP1 {
    // simple allpass: y = -a*x + z; z = x + a*y (with an internal 1-sample memory)
    float a;
    float z;
} AP1;

typedef struct APDelay {
    // allpass with short internal delay line (D samples), using a circular buffer
    float a;
    float *buf;
    unsigned size;
    unsigned pos;
} APDelay;

static inline float apd_process(APDelay* ap, float x){
    // classic allpass with delay D and coefficient a:
    // y = -a*x + d; d(next) = x + a*y
    float d = ap->buf[ap->pos];
    float y = -ap->a * x + d;
    ap->buf[ap->pos] = x + ap->a * y;
    ap->pos = (ap->pos + 1) % ap->size;
    return y;
}

typedef struct FxDiffDelay {
    float sr; unsigned max_ch; unsigned max_block;
    // params
    float delay_ms, feedback, diffusion;
    int stages;
    float mix;
    float highcut_Hz;
    // main delay
    float **buf;
    unsigned *size;
    unsigned *wpos;
    // diffusion APs per channel (up to 4)
    APDelay *aps; // flattened [max_ch * 4]
    // highcut state per channel
    float *lp;
    float lpf_a;
} FxDiffDelay;

static void dd_free(FxDiffDelay* d){
    if(!d) return;
    if (d->buf){
        for(unsigned c=0;c<d->max_ch;++c) free(d->buf[c]);
        free(d->buf);
    }
    if (d->aps){
        for(unsigned c=0;c<d->max_ch;++c){
            for(int s=0;s<4;++s){
                APDelay* ap = &d->aps[c*4 + s];
                free(ap->buf);
            }
        }
        free(d->aps);
    }
    free(d->size); free(d->wpos); free(d->lp);
}

static void dd_alloc(FxDiffDelay* d){
    unsigned max_delay_samps = (unsigned)(d->sr * 0.9f) + d->max_block + 8; // up to 800ms + margin
    d->buf  = (float**)calloc(d->max_ch, sizeof(float*));
    d->size = (unsigned*)calloc(d->max_ch, sizeof(unsigned));
    d->wpos = (unsigned*)calloc(d->max_ch, sizeof(unsigned));
    d->lp   = (float*)calloc(d->max_ch, sizeof(float));
    for(unsigned c=0;c<d->max_ch;++c){
        d->buf[c]  = (float*)calloc(max_delay_samps, sizeof(float));
        d->size[c] = max_delay_samps;
        d->wpos[c] = 0u;
        d->lp[c]   = 0.f;
    }

    // tiny diffusion delays (prime-ish small sizes)
    const unsigned taps[4] = { 89u, 113u, 137u, 167u };
    d->aps = (APDelay*)calloc(d->max_ch * 4, sizeof(APDelay));
    for(unsigned c=0;c<d->max_ch;++c){
        for(int s=0;s<4;++s){
            APDelay* ap = &d->aps[c*4 + s];
            ap->a = 0.6f;
            ap->size = taps[s];
            ap->pos  = 0u;
            ap->buf  = (float*)calloc(ap->size, sizeof(float));
        }
    }
}

static void dd_update_lowpass(FxDiffDelay* d){
    float fc = d->highcut_Hz;
    if (fc < 2000.f) fc = 2000.f;
    if (fc > 14000.f) fc = 14000.f;
    float x = expf(-2.f * (float)M_PI * fc / d->sr);
    d->lpf_a = x; // y = (1-a)*x + a*y_prev
}

static inline float read_frac(float* buf, unsigned size, unsigned wpos, float delay){
    float rpos = (float)wpos - delay;
    while (rpos < 0) rpos += size;
    unsigned i0 = ((unsigned)rpos) % size;
    unsigned i1 = (i0 + 1) % size;
    float frac = rpos - floorf(rpos);
    return buf[i0]*(1.f-frac) + buf[i1]*frac;
}

static void dd_set_param(FxHandle* h, uint32_t idx, float v){
    FxDiffDelay* d=(FxDiffDelay*)h;
    switch(idx){
        case 0: d->delay_ms = clampf(v, 20.f, 800.f); break;
        case 1: d->feedback = clampf(v, 0.f, 0.95f);  break;
        case 2: d->diffusion= clampf(v, 0.f, 0.9f);   break;
        case 3: { int st=(int)roundf(v); if(st<1)st=1; if(st>4)st=4; d->stages=st; } break;
        case 4: d->mix      = clampf(v, 0.f, 1.f);    break;
        case 5: d->highcut_Hz = clampf(v, 2000.f, 14000.f); dd_update_lowpass(d); break;
        default: break;
    }
}

static void dd_reset(FxHandle* h){
    FxDiffDelay* d=(FxDiffDelay*)h;
    for(unsigned c=0;c<d->max_ch;++c){
        for(unsigned i=0;i<d->size[c];++i) d->buf[c][i]=0.f;
        d->wpos[c]=0u;
        d->lp[c]=0.f;
    }
    if (d->aps){
        for(unsigned c=0;c<d->max_ch;++c){
            for(int s=0;s<4;++s){
                APDelay* ap = &d->aps[c*4 + s];
                for(unsigned i=0;i<ap->size;++i) ap->buf[i]=0.f;
                ap->pos=0u;
            }
        }
    }
}

static void dd_destroy(FxHandle* h){
    FxDiffDelay* d=(FxDiffDelay*)h;
    dd_free(d);
    free(d);
}

static void dd_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxDiffDelay* d=(FxDiffDelay*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > d->max_ch) ch = (int)d->max_ch;

    const float wet = d->mix, dry = 1.f - wet;
    const float base_delay = (d->delay_ms * 0.001f) * d->sr;

    for(int n=0;n<frames;++n){
        float dly = base_delay;
        if (dly < 1.f) dly = 1.f;
        if (dly > (float)(d->size[0] - d->max_block - 4)) dly = (float)(d->size[0] - d->max_block - 4);

        for(int c=0;c<ch;++c){
            unsigned size = d->size[c];
            unsigned w = d->wpos[c];
            int idx = n*channels + c;
            float x = out[idx];

            // read from main delay
            float delayed = read_frac(d->buf[c], size, w, dly);

            // feedback path: run through tiny allpass diffuser chain
            float yfb = delayed;
            for(int s=0;s<d->stages; ++s){
                APDelay* ap = &d->aps[c*4 + s];
                ap->a = d->diffusion;
                yfb = apd_process(ap, yfb);
            }
            // high-cut after diffusion
            yfb = (1.f - d->lpf_a)*yfb + d->lpf_a * d->lp[c];
            d->lp[c] = yfb;

            float y = dry*x + wet*delayed;

            d->buf[c][w] = x + d->feedback * yfb;
            w = (w + 1) % size;
            d->wpos[c] = w;

            out[idx] = y;
        }
        for(int c=d->max_ch; c<channels; ++c){
            int idx = n*channels + c;
            out[idx] = out[n*channels + (c % ch)];
        }
    }
}

int diffusion_delay_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "DiffusionDelay";
    out->api_version = FX_API_VERSION;
    out->flags = 0;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 6;
    out->param_names[0]    = "delay_ms";
    out->param_names[1]    = "feedback";
    out->param_names[2]    = "diffusion";
    out->param_names[3]    = "stages";
    out->param_names[4]    = "mix";
    out->param_names[5]    = "highcut_Hz";
    out->param_defaults[0] = 120.0f;
    out->param_defaults[1] = 0.60f;
    out->param_defaults[2] = 0.60f;
    out->param_defaults[3] = 2.0f;
    out->param_defaults[4] = 0.35f;
    out->param_defaults[5] = 8000.0f;
    out->latency_samples = 0;
    return 1;
}

int diffusion_delay_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                           uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;
    FxDiffDelay* d=(FxDiffDelay*)calloc(1,sizeof(FxDiffDelay));
    if(!d) return 0;
    d->sr=(float)sample_rate; d->max_block=max_block; d->max_ch = max_channels?max_channels:2;
    d->delay_ms=120.f; d->feedback=0.6f; d->diffusion=0.6f; d->stages=2; d->mix=0.35f; d->highcut_Hz=8000.f;
    dd_alloc(d); dd_update_lowpass(d); dd_reset((FxHandle*)d);
    out_vt->process = dd_process;
    out_vt->set_param = dd_set_param;
    out_vt->reset = dd_reset;
    out_vt->destroy = dd_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
