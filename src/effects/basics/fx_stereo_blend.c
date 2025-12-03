// fx_stereo_blend.c — Stereo blend / swap / center collapse utility
// Behavior:
//   balance in [-1..1]:
//     -1.0 => take 100% Left, 0% Right (mono of Left on both outputs)
//      0.0 => 50% Left + 50% Right (center, mono)
//     +1.0 => take 0% Left, 100% Right (mono of Right on both outputs)
//   keep_stereo in {0,1}:
//     0 => collapse to mono using the weighted sum (applied to BOTH outputs)
//     1 => cross-mix proportionally but keep stereo image (no full collapse)
//
// Params:
//   0: balance     (-1..+1, default 0)
//   1: keep_stereo (0..1 treated as boolean, default 0)
//
// Notes:
// - Interleaved float32, in-place safe.
// - This is intentionally simple and RT-clean (no allocs in process).

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }

typedef struct FxStereoBlend {
    float sr;
    unsigned max_channels;
    float balance;      // -1..1
    float keep_stereo;  // 0 or 1 (as float)
} FxStereoBlend;

static void stb_set_param(FxHandle* h, uint32_t idx, float v){
    FxStereoBlend* s = (FxStereoBlend*)h;
    switch(idx){
        case 0: s->balance = clampf(v, -1.f, 1.f); break;
        case 1: s->keep_stereo = v > 0.5f ? 1.f : 0.f; break;
        default: break;
    }
}
static void stb_reset(FxHandle* h){ (void)h; }
static void stb_destroy(FxHandle* h){ free(h); }

static void stb_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in; // we are in-place; 'out' already has input
    FxStereoBlend* s = (FxStereoBlend*)h;
    if (channels < 2) return; // nothing to do for mono

    const float b = s->balance;                 // -1..1
    const float wL = 0.5f * (1.0f - b);         // left weight
    const float wR = 0.5f * (1.0f + b);         // right weight
    const int ch = channels;

    if (s->keep_stereo < 0.5f){
        // Collapse to mono using weighted sum; copy same to both channels
        for(int n=0;n<frames;++n){
            int i = n*ch;
            float L = out[i+0];
            float R = out[i+1];
            float mono = wL*L + wR*R;
            out[i+0] = mono;
            out[i+1] = mono;
            // pass-through remaining channels if exist
            for(int c=2;c<ch;++c) out[i+c] = mono;
        }
    } else {
        // Keep stereo: cross-mix proportionally
        // balance<0 biases mix toward Left content on both sides; >0 toward Right content
        // Map |b| -> crossmix amount a in [0,1]
        float a = fabsf(b);
        for(int n=0;n<frames;++n){
            int i = n*ch;
            float L = out[i+0];
            float R = out[i+1];
            float outL = (1.0f - a)*L + a*R; // as b->+1 pull more R into left
            float outR = (1.0f - a)*R + a*L; // and vice versa
            out[i+0] = outL;
            out[i+1] = outR;
            for(int c=2;c<ch;++c){
                // for any extra channels, mirror the cross-mixed sum
                out[i+c] = 0.5f*(outL + outR);
            }
        }
    }
}

int stereo_blend_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "StereoBlend";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 2;
    out->param_names[0]    = "balance";
    out->param_names[1]    = "keep_stereo";
    out->param_defaults[0] = 0.0f; // center
    out->param_defaults[1] = 0.0f; // collapse by default
    out->latency_samples = 0;
    return 1;
}

int stereo_blend_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                        uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxStereoBlend* s = (FxStereoBlend*)calloc(1,sizeof(FxStereoBlend));
    if(!s) return 0;
    s->sr = (float)sample_rate;
    s->max_channels = max_channels ? max_channels : 2;
    s->balance = 0.0f;
    s->keep_stereo = 0.0f;
    out_vt->process  = stb_process;
    out_vt->set_param= stb_set_param;
    out_vt->reset    = stb_reset;
    out_vt->destroy  = stb_destroy;
    *out_handle = (FxHandle*)s;
    return 1;
}
