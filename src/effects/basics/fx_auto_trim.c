// fx_auto_trim.c — Real-time auto leveler (target RMS), gentle and RT-safe
// Idea: measure running RMS and nudge gain toward a target level with a smooth
// time constant. Applies a single gain to all channels (linked stereo).
//
// Params:
//   0: target_dB      (-36..-6, default -18)
//   1: speed_ms       (5..1000, default 150)  — how fast to correct (attack-ish)
//   2: max_gain_dB    (0..36, default 24)     — clamp maximum makeup
//   3: gate_thresh_dB (-90..-30, default -60) — below this, freeze gain (avoid boosting silence)
//
// Notes:
// - Interleaved float32, in-place, no allocations in process.
// - Uses simple exponential averaging for detector and for gain slewing.
// - This is NOT a compressor; it’s a slow makeup to land near a nominal level.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float db_to_lin(float dB){ return powf(10.0f, dB/20.0f); }

typedef struct FxAutoTrim {
    float sr;
    unsigned max_channels;

    // params
    float target_dB;
    float speed_ms;
    float max_gain_dB;
    float gate_thresh_dB;

    // state
    float env_rms;     // running RMS estimate (linear)
    float cur_gain;    // current applied gain (linear)
} FxAutoTrim;

static void at_set_param(FxHandle* h, uint32_t idx, float v){
    FxAutoTrim* a = (FxAutoTrim*)h;
    switch(idx){
        case 0: a->target_dB = clampf(v, -36.f, -6.f); break;
        case 1: a->speed_ms  = clampf(v, 5.f, 1000.f); break;
        case 2: a->max_gain_dB = clampf(v, 0.f, 36.f); break;
        case 3: a->gate_thresh_dB = clampf(v, -90.f, -30.f); break;
        default: break;
    }
}
static void at_reset(FxHandle* h){
    FxAutoTrim* a = (FxAutoTrim*)h;
    a->env_rms = 0.0f;
    a->cur_gain = 1.0f;
}
static void at_destroy(FxHandle* h){ free(h); }

static void at_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxAutoTrim* a = (FxAutoTrim*)h;
    if (channels < 1) return;

    // detector smoothing coefficient from speed_ms (convert to seconds)
    const float tau = a->speed_ms * 0.001f;
    const float alpha = (tau <= 0.f) ? 1.f : expf(-1.0f / (tau * a->sr));

    const float target_lin = db_to_lin(a->target_dB);
    const float gate_lin   = db_to_lin(a->gate_thresh_dB);
    const float max_gain   = db_to_lin(a->max_gain_dB);

    const int ch = channels;
    float env = a->env_rms;
    float g = a->cur_gain;

    // measure block RMS across all channels
    double acc = 0.0;
    for(int n=0;n<frames;++n){
        int i = n*ch;
        // mono-linked detector: average across channels
        double s = 0.0;
        for(int c=0;c<ch;++c){
            float v = out[i+c];
            s += (double)v * (double)v;
        }
        acc += s / (double)ch;
    }
    float block_rms = frames > 0 ? sqrtf((float)(acc / (double)frames)) : 0.0f;

    // smooth detector
    env = alpha * env + (1.0f - alpha) * block_rms;

    // compute desired gain: target / env (with guards)
    float desired = (env > 1e-9f) ? (target_lin / env) : 1.0f;

    // gate: if very quiet, freeze to avoid cranking noise
    if (env < gate_lin) desired = 1.0f;

    // clamp max boost
    if (desired > max_gain) desired = max_gain;

    // slew current gain toward desired using same alpha (gentle)
    g = alpha * g + (1.0f - alpha) * desired;

    // apply
    for(int n=0;n<frames;++n){
        int i = n*ch;
        for(int c=0;c<ch;++c){
            out[i+c] *= g;
        }
    }

    a->env_rms = env;
    a->cur_gain = g;
}

int auto_trim_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "AutoTrim";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 4;
    out->param_names[0]    = "target_dB";
    out->param_names[1]    = "speed_ms";
    out->param_names[2]    = "max_gain_dB";
    out->param_names[3]    = "gate_thresh_dB";
    out->param_defaults[0] = -18.0f;
    out->param_defaults[1] = 150.0f;
    out->param_defaults[2] = 24.0f;
    out->param_defaults[3] = -60.0f;
    out->latency_samples = 0;
    return 1;
}

int auto_trim_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxAutoTrim* a = (FxAutoTrim*)calloc(1,sizeof(FxAutoTrim));
    if(!a) return 0;
    a->sr = (float)sample_rate;
    a->max_channels = max_channels ? max_channels : 2;
    a->target_dB = -18.0f;
    a->speed_ms  = 150.0f;
    a->max_gain_dB = 24.0f;
    a->gate_thresh_dB = -60.0f;
    at_reset((FxHandle*)a);
    out_vt->process  = at_process;
    out_vt->set_param= at_set_param;
    out_vt->reset    = at_reset;
    out_vt->destroy  = at_destroy;
    *out_handle = (FxHandle*)a;
    return 1;
}
