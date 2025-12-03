
// fx_pan.c — Constant-power stereo panner (interleaved, in-place)
// Params:
//   0: pan (-1..+1)  — -1 = full L, 0 = center, +1 = full R
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}

typedef struct FxPan{
    float sr; unsigned max_channels;
    float pan;
} FxPan;

static void pan_set_param(FxHandle* h, uint32_t idx, float value){
    FxPan* p = (FxPan*)h;
    if (idx==0) p->pan = clampf(value, -1.0f, 1.0f);
}
static void pan_reset(FxHandle* h){ (void)h; }
static void pan_destroy(FxHandle* h){ free(h); }

static void pan_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxPan* p = (FxPan*)h;
    if (channels < 2) return; // needs stereo
    // constant power law using sqrt
    float t = 0.5f * (p->pan + 1.0f); // 0..1
    float gL = sqrtf(1.0f - t);
    float gR = sqrtf(t);
    for (int n=0;n<frames;++n){
        int base = n*channels;
        float L = out[base+0];
        float R = out[base+1];
        out[base+0] = L * gL;
        out[base+1] = R * gR;
        // passthrough any extra channels untouched
    }
}

int pan_get_desc(FxDesc* out){
    if(!out) return 0;
    out->name = "Pan";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 1;
    out->param_names[0] = "pan";
    out->param_defaults[0] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int pan_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
               uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;(void)max_block;(void)max_channels;(void)sample_rate;
    FxPan* p = (FxPan*)calloc(1,sizeof(FxPan));
    if(!p) return 0;
    p->sr = (float)sample_rate; p->max_channels = max_channels?max_channels:2;
    p->pan = 0.0f;
    out_vt->process = pan_process;
    out_vt->set_param = pan_set_param;
    out_vt->reset = pan_reset;
    out_vt->destroy = pan_destroy;
    *out_handle = (FxHandle*)p;
    return 1;
}
