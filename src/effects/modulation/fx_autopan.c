// fx_autopan.c — Stereo AutoPan (Hz), equal‑power, depth + phase + mix
//
// Params:
//   0: rate_hz    (0.01..10, default 1.0)
//   1: depth      (0..1,     default 1.0)  — 0 = center, 1 = full L/R
//   2: phase_deg  (0..360,   default 0)    — LFO start phase
//   3: mix        (0..1,     default 1.0)  — wet/dry (wet = panned, dry = original)
//
// Notes:
// - Equal‑power panner: pan ∈ [−1, +1] → θ = (pan+1)/2 * (π/2), gL = cos θ, gR = sin θ
// - For mono input, acts like tremolo (equal‑power gain around unity).
// - For >2 channels, processes first two and mirrors remaining channels.
//
// RT‑safe, in‑place.
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct FxAutoPan {
    float sr;
    unsigned max_ch;
    float rate_hz, depth, phase_deg, mix;
    float phase; // 0..1
} FxAutoPan;

static void apn_set_param(FxHandle* h, uint32_t idx, float v){
    FxAutoPan* a=(FxAutoPan*)h;
    switch(idx){
        case 0: a->rate_hz  = clampf(v, 0.01f, 10.f); break;
        case 1: a->depth    = clampf(v, 0.f, 1.f);    break;
        case 2: a->phase_deg= clampf(v, 0.f, 360.f);  a->phase = a->phase_deg/360.f; break;
        case 3: a->mix      = clampf(v, 0.f, 1.f);    break;
        default: break;
    }
}
static void apn_reset(FxHandle* h){
    FxAutoPan* a=(FxAutoPan*)h;
    a->phase = a->phase_deg/360.f;
}
static void apn_destroy(FxHandle* h){ free(h); }

static void apn_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxAutoPan* a=(FxAutoPan*)h;
    int ch = channels;
    if (ch < 1) return;
    float phase = a->phase;
    const float inc = a->rate_hz / a->sr;
    const float depth = a->depth;
    const float wet = a->mix, dry = 1.f - wet;

    for(int n=0;n<frames;++n){
        float pan = depth * sinf(2.f*(float)M_PI * phase); // −depth..+depth
        float theta = ( (pan + 1.f) * 0.5f ) * (float)M_PI * 0.5f;
        float gL = cosf(theta);
        float gR = sinf(theta);

        if (ch >= 2){
            int iL = n*channels + 0;
            int iR = n*channels + 1;
            float xL = out[iL];
            float xR = out[iR];
            float mono = 0.5f*(xL + xR);
            float yL = mono * gL;
            float yR = mono * gR;
            out[iL] = dry*xL + wet*yL;
            out[iR] = dry*xR + wet*yR;
            // fold extra channels by mirroring
            for(int c=2;c<channels;++c){
                int i = n*channels + c;
                out[i] = out[n*channels + (c & 1)];
            }
        }else{
            // Mono: equal‑power gain around unity (approx)
            int i = n*channels + 0;
            float x = out[i];
            float g = 0.7071f*(gL + gR); // equal‑power sum ≈ 1
            out[i] = dry*x + wet*(g*x);
        }

        phase += inc;
        if (phase >= 1.f) phase -= 1.f;
        if (phase < 0.f)  phase += 1.f;
    }
    a->phase = phase;
}

int autopan_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "AutoPan";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 4;
    out->param_names[0]    = "rate_hz";
    out->param_names[1]    = "depth";
    out->param_names[2]    = "phase_deg";
    out->param_names[3]    = "mix";
    out->param_defaults[0] = 1.0f;
    out->param_defaults[1] = 1.0f;
    out->param_defaults[2] = 0.0f;
    out->param_defaults[3] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int autopan_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxAutoPan* a=(FxAutoPan*)calloc(1,sizeof(FxAutoPan));
    if(!a) return 0;
    a->sr=(float)sample_rate; a->max_ch = max_channels?max_channels:2;
    a->rate_hz=1.0f; a->depth=1.0f; a->phase_deg=0.f; a->mix=1.0f; a->phase=0.f;
    out_vt->process = apn_process;
    out_vt->set_param = apn_set_param;
    out_vt->reset = apn_reset;
    out_vt->destroy = apn_destroy;
    *out_handle = (FxHandle*)a;
    return 1;
}
