
// fx_mute.c — Click-free mute with short ramp (interleaved, in-place)
// Params:
//   0: mute      (0 or 1) — bool
//   1: ramp_ms   (0.1..50) — fade time for toggling
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}

typedef struct FxMute{
    float sr; unsigned max_channels;
    float mute, ramp_ms;
    float g_cur, g_target, alpha;
} FxMute;

static void mu_update(FxMute* m){
    m->g_target = (m->mute >= 0.5f) ? 0.0f : 1.0f;
    float ms = clampf(m->ramp_ms, 0.1f, 50.0f);
    float tau = ms * 0.001f;
    m->alpha = 1.0f - expf(-1.0f / (tau * m->sr));
}

static void mu_set_param(FxHandle* h, uint32_t idx, float value){
    FxMute* m = (FxMute*)h;
    switch(idx){
        case 0: m->mute = value; break;
        case 1: m->ramp_ms = value; break;
        default: break;
    }
    mu_update(m);
}
static void mu_reset(FxHandle* h){ FxMute* m=(FxMute*)h; m->g_cur=m->g_target; }
static void mu_destroy(FxHandle* h){ free(h); }

static void mu_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxMute* m = (FxMute*)h;
    float cur = m->g_cur, tgt = m->g_target, a = m->alpha;
    for (int n=0;n<frames;++n){
        cur += (tgt - cur) * a;
        int base = n*channels;
        for (int ch=0; ch<channels; ++ch){
            out[base+ch] *= cur;
        }
    }
    m->g_cur = cur;
}

int mute_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "Mute";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "mute";
    out->param_names[1] = "ramp_ms";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 5.0f;
    out->latency_samples = 0;
    return 1;
}

int mute_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;
    FxMute* m = (FxMute*)calloc(1,sizeof(FxMute));
    if(!m) return 0;
    m->sr = (float)sample_rate; m->max_channels = max_channels?max_channels:2;
    m->mute=0.0f; m->ramp_ms=5.0f; mu_update(m); mu_reset((FxHandle*)m);
    out_vt->process = mu_process;
    out_vt->set_param = mu_set_param;
    out_vt->reset = mu_reset;
    out_vt->destroy = mu_destroy;
    *out_handle = (FxHandle*)m;
    return 1;
}
