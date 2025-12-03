// fx_expander.c — Downward expander with hysteresis (linked stereo)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }
static inline float lin_to_db(float x){ return 20.0f*log10f(x <= 1e-20f ? 1e-20f : x); }

typedef struct FxExpander {
    float sr; unsigned max_channels;
    float threshold_dB, ratio, hysteresis_dB, attack_ms, release_ms;
    float env, gain_lin;
    int   is_expanding;
} FxExpander;

static void ex_set_param(FxHandle* h, uint32_t idx, float v){
    FxExpander* e=(FxExpander*)h;
    switch(idx){
        case 0: e->threshold_dB = clampf(v, -80.f, 0.f); break;
        case 1: e->ratio        = clampf(v, 1.f, 8.f);   break;
        case 2: e->hysteresis_dB= clampf(v, 0.f, 12.f);  break;
        case 3: e->attack_ms    = clampf(v, 2.f, 50.f);  break;
        case 4: e->release_ms   = clampf(v, 20.f, 500.f);break;
        default: break;
    }
}
static void ex_reset(FxHandle* h){ FxExpander* e=(FxExpander*)h; e->env=0.f; e->gain_lin=1.f; e->is_expanding=0; }
static void ex_destroy(FxHandle* h){ free(h); }

static void ex_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxExpander* e=(FxExpander*)h;
    int ch = channels; if (ch < 1) return;

    const float atk_a = expf(-1.0f / ( (e->attack_ms  * 0.001f) * e->sr ));
    const float rel_a = expf(-1.0f / ( (e->release_ms * 0.001f) * e->sr ));

    const float thr = e->threshold_dB;
    const float hys = e->hysteresis_dB;
    const float thr_open  = thr + 0.5f*hys;
    const float thr_close = thr - 0.5f*hys;

    float env = e->env;
    float g   = e->gain_lin;
    int expanding = e->is_expanding;

    for(int n=0;n<frames;++n){
        int i = n*ch;
        float lev = 0.f;
        for(int k=0;k<ch;++k){ lev += fabsf(out[i+k]); }
        lev /= (float)ch;

        if (lev > env) env = atk_a * env + (1.f - atk_a) * lev;
        else           env = rel_a * env + (1.f - rel_a) * lev;

        float LdB = lin_to_db(env);
        if (expanding){
            if (LdB > thr_open) expanding = 0;
        } else {
            if (LdB < thr_close) expanding = 1;
        }

        float desired = 1.f;
        if (expanding && env > 1e-9f){
            float k = 1.f - 1.f / e->ratio;
            float gain_db = (LdB - thr) * k; // negative
            if (gain_db < -60.f) gain_db = -60.f;
            desired = db_to_lin(gain_db);
        }

        if (desired < g) g = atk_a * g + (1.f - atk_a) * desired;
        else             g = rel_a * g + (1.f - rel_a) * desired;

        for(int k=0;k<ch;++k){
            out[i+k] *= g;
        }
    }

    e->env = env; e->gain_lin = g; e->is_expanding = expanding;
}

int expander_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "Expander";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 5;
    out->param_names[0]    = "threshold_dB";
    out->param_names[1]    = "ratio";
    out->param_names[2]    = "hysteresis_dB";
    out->param_names[3]    = "attack_ms";
    out->param_names[4]    = "release_ms";
    out->param_defaults[0] = -45.0f;
    out->param_defaults[1] = 2.0f;
    out->param_defaults[2] = 3.0f;
    out->param_defaults[3] = 5.0f;
    out->param_defaults[4] = 100.0f;
    out->latency_samples = 0;
    return 1;
}

int expander_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxExpander* e = (FxExpander*)calloc(1,sizeof(FxExpander));
    if(!e) return 0;
    e->sr = (float)sample_rate;
    e->max_channels = max_channels ? max_channels : 2;
    e->threshold_dB = -45.f;
    e->ratio = 2.f;
    e->hysteresis_dB = 3.f;
    e->attack_ms = 5.f;
    e->release_ms= 100.f;
    ex_reset((FxHandle*)e);
    out_vt->process   = ex_process;
    out_vt->set_param = ex_set_param;
    out_vt->reset     = ex_reset;
    out_vt->destroy   = ex_destroy;
    *out_handle = (FxHandle*)e;
    return 1;
}
