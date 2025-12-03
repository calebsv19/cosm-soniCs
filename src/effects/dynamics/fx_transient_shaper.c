// fx_transient_shaper.c — Transient shaper (attack/sustain tilt), linked stereo
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }

typedef struct FxTransient {
    float sr; unsigned max_channels;
    float attack_amt, sustain_amt, fast_ms, slow_ms, mix;
    float env_fast, env_slow, gain_slew;
} FxTransient;

static void ts_set_param(FxHandle* h, uint32_t idx, float v){
    FxTransient* t=(FxTransient*)h;
    switch(idx){
        case 0: t->attack_amt  = clampf(v, -1.f, 1.f); break;
        case 1: t->sustain_amt = clampf(v, -1.f, 1.f); break;
        case 2: t->fast_ms     = clampf(v, 1.f, 20.f); break;
        case 3: t->slow_ms     = clampf(v, 20.f, 400.f); break;
        case 4: t->mix         = clampf(v, 0.f, 1.f);  break;
        default: break;
    }
}
static void ts_reset(FxHandle* h){ FxTransient* t=(FxTransient*)h; t->env_fast=0.f; t->env_slow=0.f; t->gain_slew=1.f; }
static void ts_destroy(FxHandle* h){ free(h); }

static void ts_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxTransient* t=(FxTransient*)h;
    int ch = channels; if (ch < 1) return;

    const float a_fast = expf(-1.0f / ( (t->fast_ms * 0.001f) * t->sr ));
    const float a_slow = expf(-1.0f / ( (t->slow_ms * 0.001f) * t->sr ));
    const float wet = t->mix, dry = 1.f - wet;

    float ef = t->env_fast;
    float es = t->env_slow;
    float g  = t->gain_slew;

    const float g_slew = expf(-1.0f / (0.001f * t->sr));

    for(int n=0;n<frames;++n){
        int i = n*ch;
        float lev = 0.f;
        for(int k=0;k<ch;++k){ lev += fabsf(out[i+k]); }
        lev /= (float)ch;

        ef = a_fast*ef + (1.f - a_fast)*lev;
        es = a_slow*es + (1.f - a_slow)*lev;

        float denom = es > 1e-9f ? es : 1e-9f;
        float diff = (ef - es) / denom;
        if (diff > 1.f) diff = 1.f; else if (diff < -1.f) diff = -1.f;

        float atk = diff > 0.f ? diff : 0.f;
        float sus = diff < 0.f ? -diff : 0.f;

        float desired = 1.f + t->attack_amt * atk + t->sustain_amt * sus;
        if (desired < 0.2f) desired = 0.2f;
        if (desired > 4.0f) desired = 4.0f;

        g = g_slew * g + (1.f - g_slew) * desired;

        for(int k=0;k<ch;++k){
            float x = out[i+k];
            float y = x * g;
            out[i+k] = dry*x + wet*y;
        }
    }

    t->env_fast = ef; t->env_slow = es; t->gain_slew = g;
}

int transient_shaper_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "TransientShaper";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 5;
    out->param_names[0]    = "attack_amt";
    out->param_names[1]    = "sustain_amt";
    out->param_names[2]    = "fast_ms";
    out->param_names[3]    = "slow_ms";
    out->param_names[4]    = "mix";
    out->param_defaults[0] = 0.5f;
    out->param_defaults[1] = 0.0f;
    out->param_defaults[2] = 5.0f;
    out->param_defaults[3] = 150.0f;
    out->param_defaults[4] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int transient_shaper_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                            uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxTransient* t = (FxTransient*)calloc(1,sizeof(FxTransient));
    if(!t) return 0;
    t->sr = (float)sample_rate;
    t->max_channels = max_channels ? max_channels : 2;
    t->attack_amt = 0.5f;
    t->sustain_amt= 0.0f;
    t->fast_ms = 5.0f;
    t->slow_ms = 150.0f;
    t->mix = 1.0f;
    ts_reset((FxHandle*)t);
    out_vt->process   = ts_process;
    out_vt->set_param = ts_set_param;
    out_vt->reset     = ts_reset;
    out_vt->destroy   = ts_destroy;
    *out_handle = (FxHandle*)t;
    return 1;
}
