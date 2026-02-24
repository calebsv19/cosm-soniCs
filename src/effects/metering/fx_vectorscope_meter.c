// fx_vectorscope_meter.c - pass-through vectorscope meter stub
#include <stdlib.h>
#include <string.h>
#include "effects/effects_api.h"

// FxVectorscopeMeter holds analysis state for a vectorscope meter effect.
typedef struct FxVectorscopeMeter {
    int mode;
    float decay_ms;
} FxVectorscopeMeter;

// vectorscope_meter_process passes audio through unchanged for analysis-only use.
static void vectorscope_meter_process(FxHandle* h,
                                      const float* in,
                                      float* out,
                                      int frames,
                                      int channels) {
    (void)h;
    int count = frames * channels;
    if (out != in) {
        memcpy(out, in, (size_t)count * sizeof(float));
    }
}

// vectorscope_meter_set_param ignores params until analysis controls are added.
static void vectorscope_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    FxVectorscopeMeter* meter = (FxVectorscopeMeter*)h;
    if (!meter) {
        return;
    }
    switch (idx) {
        case 0:
            meter->mode = value >= 0.5f ? 1 : 0;
            break;
        case 1:
            if (value < 10.0f) value = 10.0f;
            if (value > 2000.0f) value = 2000.0f;
            meter->decay_ms = value;
            break;
        default:
            break;
    }
}

// vectorscope_meter_reset clears internal analysis state.
static void vectorscope_meter_reset(FxHandle* h) {
    (void)h;
}

// vectorscope_meter_destroy frees the meter instance memory.
static void vectorscope_meter_destroy(FxHandle* h) {
    free(h);
}

// vectorscope_meter_get_desc defines the vectorscope meter as a pass-through effect.
int vectorscope_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "VectorScope";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "mode";
    out->param_names[1] = "decay_ms";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 350.0f;
    out->latency_samples = 0;
    return 1;
}

// vectorscope_meter_create allocates a meter instance and binds its vtable.
int vectorscope_meter_create(const FxDesc* desc,
                             FxHandle** out_handle,
                             FxVTable* out_vt,
                             uint32_t sample_rate,
                             uint32_t max_block,
                             uint32_t max_channels) {
    (void)desc;
    (void)sample_rate;
    (void)max_block;
    (void)max_channels;

    FxVectorscopeMeter* meter = (FxVectorscopeMeter*)calloc(1, sizeof(FxVectorscopeMeter));
    if (!meter) return 0;
    meter->mode = 0;
    meter->decay_ms = 350.0f;

    out_vt->process = vectorscope_meter_process;
    out_vt->set_param = vectorscope_meter_set_param;
    out_vt->reset = vectorscope_meter_reset;
    out_vt->destroy = vectorscope_meter_destroy;

    *out_handle = (FxHandle*)meter;
    return 1;
}
