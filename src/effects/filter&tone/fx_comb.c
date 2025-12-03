// fx_comb.c — Feed-forward comb filter with fractional delay (linear interp), mix
// y[n] = x[n] + g * x[n - D], D in samples (fractional supported).
//
// Params:
//   0: delay_ms   (1..50,   default 10)
//   1: gain       (-1..1,   default 0.7)  — feed-forward coefficient
//   2: mix        (0..1,    default 1.0)
//
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct FxComb {
    float sr; unsigned max_ch; unsigned max_block;
    float delay_ms; float gain; float mix;
    // per-channel delay lines
    float **buf;
    unsigned *size;
    unsigned *wpos;
} FxComb;

static void comb_free(FxComb* c){
    if(!c) return;
    if(c->buf){
        for(unsigned ch=0; ch<c->max_ch; ++ch) free(c->buf[ch]);
        free(c->buf);
    }
    free(c->size); free(c->wpos);
}

static void comb_alloc_buffers(FxComb* c){
    unsigned max_delay_samps = (unsigned)(c->sr * 0.050f) + c->max_block + 8; // 50ms + margin
    c->buf  = (float**)calloc(c->max_ch, sizeof(float*));
    c->size = (unsigned*)calloc(c->max_ch, sizeof(unsigned));
    c->wpos = (unsigned*)calloc(c->max_ch, sizeof(unsigned));
    for(unsigned ch=0; ch<c->max_ch; ++ch){
        c->buf[ch] = (float*)calloc(max_delay_samps, sizeof(float));
        c->size[ch]= max_delay_samps;
        c->wpos[ch]= 0u;
    }
}

static void comb_set_param(FxHandle* h, uint32_t idx, float v){
    FxComb* c=(FxComb*)h;
    switch(idx){
        case 0: c->delay_ms = clampf(v, 1.f, 50.f); break;
        case 1: c->gain     = clampf(v, -1.f, 1.f); break;
        case 2: c->mix      = clampf(v, 0.f, 1.f);  break;
        default: break;
    }
}

static void comb_reset(FxHandle* h){
    FxComb* c=(FxComb*)h;
    for(unsigned ch=0; ch<c->max_ch; ++ch){
        for(unsigned i=0;i<c->size[ch];++i) c->buf[ch][i]=0.f;
        c->wpos[ch]=0u;
    }
}

static void comb_destroy(FxHandle* h){
    FxComb* c=(FxComb*)h;
    comb_free(c);
    free(c);
}

static inline float delay_read_lin(float* buf, unsigned size, unsigned wpos, float delay_samples){
    // read "delay_samples" behind write head with linear interpolation
    float rpos = (float)wpos - delay_samples;
    while (rpos < 0) rpos += size;
    unsigned i0 = ((unsigned)rpos) % size;
    unsigned i1 = (i0 + 1) % size;
    float frac = rpos - floorf(rpos);
    return buf[i0]*(1.f-frac) + buf[i1]*frac;
}

static void comb_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxComb* c=(FxComb*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > c->max_ch) ch = (int)c->max_ch;
    const float wet = c->mix, dry = 1.f - wet;
    float delay_samps = (c->delay_ms * 0.001f) * c->sr;
    if (delay_samps > (float)(c->size[0]-c->max_block-4)) delay_samps = (float)(c->size[0]-c->max_block-4);
    if (delay_samps < 1.f) delay_samps = 1.f;

    for(int n=0;n<frames;++n){
        for(int k=0;k<ch;++k){
            unsigned size = c->size[k];
            unsigned w = c->wpos[k];
            int idx = n*channels + k;
            float x = out[idx];

            float xd = delay_read_lin(c->buf[k], size, w, delay_samps);
            float y = x + c->gain * xd;

            // write input (feed-forward comb)
            c->buf[k][w] = x;
            w = (w + 1) % size;
            c->wpos[k] = w;

            out[idx] = dry*x + wet*y;
        }
        // fold extra channels if any
        for(int k=c->max_ch; k<channels; ++k){
            int idx = n*channels + k;
            out[idx] = out[n*channels + (k % ch)];
        }
    }
}

int comb_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "CombFF";
    out->api_version = FX_API_VERSION;
    out->flags = 0; // not strictly in-place safe due to external buffer? Processing is still in-place; leave 0.
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 3;
    out->param_names[0]    = "delay_ms";
    out->param_names[1]    = "gain";
    out->param_names[2]    = "mix";
    out->param_defaults[0] = 10.0f;
    out->param_defaults[1] = 0.7f;
    out->param_defaults[2] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int comb_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;
    FxComb* c=(FxComb*)calloc(1,sizeof(FxComb));
    if(!c) return 0;
    c->sr=(float)sample_rate; c->max_block=max_block; c->max_ch = max_channels?max_channels:2;
    c->delay_ms=10.f; c->gain=0.7f; c->mix=1.f;
    comb_alloc_buffers(c);
    comb_reset((FxHandle*)c);
    out_vt->process = comb_process;
    out_vt->set_param = comb_set_param;
    out_vt->reset = comb_reset;
    out_vt->destroy = comb_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
