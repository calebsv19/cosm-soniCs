// fx_upward_comp.c — Upward compressor / parallel comp (linked stereo)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }
static inline float lin_to_db(float x){ return 20.0f*log10f(x <= 1e-20f ? 1e-20f : x); }

typedef struct FxUpwardComp {
    float sr; unsigned max_channels;
    float threshold_dB, ratio, attack_ms, release_ms, mix, makeup_dB;
    float env, gain_lin;
} FxUpwardComp;

static void uc_set_param(FxHandle* h, uint32_t idx, float v){
    FxUpwardComp* c=(FxUpwardComp*)h;
    switch(idx){
        case 0: c->threshold_dB = clampf(v, -60.f, 0.f); break;
        case 1: c->ratio        = clampf(v, 1.f, 8.f);   break;
        case 2: c->attack_ms    = clampf(v, 2.f, 50.f);  break;
        case 3: c->release_ms   = clampf(v, 20.f, 500.f);break;
        case 4: c->mix          = clampf(v, 0.f, 1.f);   break;
        case 5: c->makeup_dB    = clampf(v, 0.f, 24.f);  break;
        default: break;
    }
}
static void uc_reset(FxHandle* h){ FxUpwardComp* c=(FxUpwardComp*)h; c->env=0.f; c->gain_lin=1.f; }
static void uc_destroy(FxHandle* h){ free(h); }

static void uc_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxUpwardComp* c=(FxUpwardComp*)h;
    int ch = channels; if (ch < 1) return;

    const float atk_a = expf(-1.0f / ( (c->attack_ms  * 0.001f) * c->sr ));
    const float rel_a = expf(-1.0f / ( (c->release_ms * 0.001f) * c->sr ));

    const float wet = c->mix, dry = 1.f - wet;
    const float makeup = db_to_lin(c->makeup_dB);
    float env = c->env;
    float g   = c->gain_lin;

    for(int n=0;n<frames;++n){
        int i = n*ch;
        float lev = 0.f;
        for(int k=0;k<ch;++k){ lev += fabsf(out[i+k]); }
        lev /= (float)ch;

        if (lev > env) env = atk_a * env + (1.f - atk_a) * lev;
        else           env = rel_a * env + (1.f - rel_a) * lev;

        float desired_g = 1.f;
        if (env > 1e-9f){
            float LdB = lin_to_db(env);
            if (LdB < c->threshold_dB){
                float k = 1.f - 1.f / c->ratio;
                float gain_db = (c->threshold_dB - LdB) * k;
                if (gain_db > 24.f) gain_db = 24.f;
                desired_g = db_to_lin(gain_db);
            }
        }

        if (desired_g > g) g = atk_a * g + (1.f - atk_a) * desired_g;
        else               g = rel_a * g + (1.f - rel_a) * desired_g;

        float gg = g * makeup;
        for(int k=0;k<ch;++k){
            float x = out[i+k];
            float y = x * gg;
            out[i+k] = dry*x + wet*y;
        }
    }

    c->env = env; c->gain_lin = g;
}

int upward_comp_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "UpwardComp";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 6;
    out->param_names[0]    = "threshold_dB";
    out->param_names[1]    = "ratio";
    out->param_names[2]    = "attack_ms";
    out->param_names[3]    = "release_ms";
    out->param_names[4]    = "mix";
    out->param_names[5]    = "makeup_dB";
    out->param_defaults[0] = -24.0f;
    out->param_defaults[1] = 2.0f;
    out->param_defaults[2] = 10.0f;
    out->param_defaults[3] = 120.0f;
    out->param_defaults[4] = 1.0f;
    out->param_defaults[5] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int upward_comp_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                       uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxUpwardComp* c = (FxUpwardComp*)calloc(1,sizeof(FxUpwardComp));
    if(!c) return 0;
    c->sr = (float)sample_rate;
    c->max_channels = max_channels ? max_channels : 2;
    c->threshold_dB = -24.f;
    c->ratio = 2.f;
    c->attack_ms = 10.f;
    c->release_ms= 120.f;
    c->mix = 1.f;
    c->makeup_dB = 0.f;
    uc_reset((FxHandle*)c);
    out_vt->process   = uc_process;
    out_vt->set_param = uc_set_param;
    out_vt->reset     = uc_reset;
    out_vt->destroy   = uc_destroy;
    *out_handle = (FxHandle*)c;
    return 1;
}
