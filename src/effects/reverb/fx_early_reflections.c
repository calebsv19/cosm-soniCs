
// fx_early_reflections.c — Early Reflections stub (few taps + width + mix)
//
// Design:
//  - Fixed multi-tap pattern (left/right offset for stereo width).
//  - Width blends L<->R contribution of ER taps (0 = mono center, 1 = hard stereo).
//  - Pre-delay to push ER cluster later if desired.
//  - Intended to be placed before a late reverb for realism.
//
// Params:
//   0: predelay_ms (0..100, default 10)
//   1: width       (0..1,   default 0.5)   — 0 mono, 1 wide
//   2: mix         (0..1,   default 0.25)
//
// Implementation: in-place, small per-channel buffers sized from max tap.
// RT-safe (no alloc in process).
//
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct DelayLine {
    float* buf;
    int len;
    int w;
} DelayLine;

static inline void dl_write(DelayLine* d, float x){
    d->buf[d->w] = x;
    d->w++; if(d->w>=d->len) d->w=0;
}
static inline float dl_read(const DelayLine* d, int tap){
    int r = d->w - tap; while(r<0) r += d->len; return d->buf[r];
}
static inline void dl_clear(DelayLine* d){
    if(!d||!d->buf) return;
    for(int i=0;i<d->len;++i) d->buf[i]=0.f; d->w=0;
}

typedef struct FxEarlyRef {
    uint32_t sr; unsigned max_ch;
    float predelay_ms;
    float width;
    float mix;
    // derived
    int pre_samp;
    // per-channel delay for ER bank (single buffer big enough for max tap)
    DelayLine dlL, dlR;
} FxEarlyRef;

// ER tap times (ms) and gains — small cluster; simple Haas-like pattern
static const float TAP_MS[]   = { 7.f, 11.f, 17.f, 23.f, 31.f };
static const float TAP_GAIN[] = { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f };
#define NUM_TAPS (int)(sizeof(TAP_MS)/sizeof(TAP_MS[0]))

static int alloc_dl(DelayLine* d, int len){
    d->buf = (float*)calloc((size_t)len, sizeof(float));
    if(!d->buf) return 0;
    d->len = len; d->w = 0; return 1;
}
static void free_dl(DelayLine* d){
    free(d->buf); d->buf=NULL; d->len=0; d->w=0;
}

static void er_recalc(FxEarlyRef* e){
    int max_ms = (int)(e->predelay_ms + 40.f) + 2; // taps up to ~31ms + margin
    int need = (int)((e->sr * max_ms)/1000.0f) + 8;
    if (need < 64) need = 64;
    // reallocate if needed
    if (e->dlL.len < need){
        float* nb = (float*)realloc(e->dlL.buf, (size_t)need * sizeof(float));
        if(nb){ int old=e->dlL.len; e->dlL.buf=nb; for(int i=old;i<need;++i) e->dlL.buf[i]=0.f; e->dlL.len=need; if(e->dlL.w>=e->dlL.len) e->dlL.w=0; }
    }
    if (e->dlR.len < need){
        float* nb = (float*)realloc(e->dlR.buf, (size_t)need * sizeof(float));
        if(nb){ int old=e->dlR.len; e->dlR.buf=nb; for(int i=old;i<need;++i) e->dlR.buf[i]=0.f; e->dlR.len=need; if(e->dlR.w>=e->dlR.len) e->dlR.w=0; }
    }
    e->pre_samp = (int)(e->predelay_ms * 0.001f * (float)e->sr + 0.5f);
    if (e->pre_samp < 0) e->pre_samp = 0;
}

static void er_set_param(FxHandle* h, uint32_t idx, float v){
    FxEarlyRef* e=(FxEarlyRef*)h;
    switch(idx){
        case 0: e->predelay_ms = clampf(v, 0.f, 100.f); break;
        case 1: e->width       = clampf(v, 0.f, 1.f);   break;
        case 2: e->mix         = clampf(v, 0.f, 1.f);   break;
        default: break;
    }
    er_recalc(e);
}
static void er_reset(FxHandle* h){
    FxEarlyRef* e=(FxEarlyRef*)h;
    dl_clear(&e->dlL); dl_clear(&e->dlR);
}
static void er_destroy(FxHandle* h){
    FxEarlyRef* e=(FxEarlyRef*)h;
    free_dl(&e->dlL); free_dl(&e->dlR); free(e);
}

static void er_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxEarlyRef* e=(FxEarlyRef*)h;
    int ch = channels; if (ch < 1) return;
    const float wet = e->mix, dry = 1.f - wet;
    for(int n=0;n<frames;++n){
        float inL = out[n*channels + 0];
        float inR = (channels>=2) ? out[n*channels + 1] : inL;
        // write into pre-delay buffers
        dl_write(&e->dlL, inL);
        dl_write(&e->dlR, inR);

        // build ER sums with small interaural differences (alternate tap offsets)
        float erL = 0.f, erR = 0.f;
        for(int t=0;t<NUM_TAPS;++t){
            int tapSamp = (int)( (TAP_MS[t] * 0.001f) * (float)e->sr + 0.5f );
            int tapSampR = tapSamp + (t&1 ? 1 : -1); if (tapSampR<1) tapSampR=1;
            float g = TAP_GAIN[t];
            erL += g * dl_read(&e->dlL, e->pre_samp + tapSamp);
            erR += g * dl_read(&e->dlR, e->pre_samp + tapSampR);
        }

        // width: cross-blend center vs side
        float mid = 0.5f*(erL + erR);
        float side= 0.5f*(erL - erR);
        float w = e->width;
        float oL = mid + w*side;
        float oR = mid - w*side;

        out[n*channels + 0] = dry*inL + wet*oL;
        if (channels>=2){
            out[n*channels + 1] = dry*inR + wet*oR;
        }
        for(int c=2;c<channels;++c){
            out[n*channels + c] = out[n*channels + (c&1)];
        }
    }
}

int early_reflections_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "EarlyReflections";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 3;
    out->param_names[0]    = "predelay_ms";
    out->param_names[1]    = "width";
    out->param_names[2]    = "mix";
    out->param_defaults[0] = 10.0f;
    out->param_defaults[1] = 0.5f;
    out->param_defaults[2] = 0.25f;
    out->latency_samples = 0;
    return 1;
}

int early_reflections_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                             uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxEarlyRef* e=(FxEarlyRef*)calloc(1,sizeof(FxEarlyRef));
    if(!e) return 0;
    e->sr=sample_rate; e->max_ch = max_channels?max_channels:2;
    e->predelay_ms=10.f; e->width=0.5f; e->mix=0.25f;
    if(!alloc_dl(&e->dlL, 2048) || !alloc_dl(&e->dlR, 2048)){ er_destroy((FxHandle*)e); return 0; }
    er_recalc(e); er_reset((FxHandle*)e);
    out_vt->process = er_process;
    out_vt->set_param = er_set_param;
    out_vt->reset = er_reset;
    out_vt->destroy = er_destroy;
    *out_handle = (FxHandle*)e;
    return 1;
}
