// fx_mid_side_meter.c - pass-through mid/side meter stub
#include <stdlib.h>
#include <string.h>
#include "effects/effects_api.h"

// FxMidSideMeter holds analysis state for a mid/side meter effect.
typedef struct FxMidSideMeter {
    float window_ms;
    float smooth_ms;
} FxMidSideMeter;

// mid_side_meter_process passes audio through unchanged for analysis-only use.
static void mid_side_meter_process(FxHandle* h,
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

// mid_side_meter_set_param ignores params until analysis controls are added.
static void mid_side_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    FxMidSideMeter* meter = (FxMidSideMeter*)h;
    if (!meter) {
        return;
    }
    switch (idx) {
        case 0:
            if (value < 10.0f) value = 10.0f;
            if (value > 2000.0f) value = 2000.0f;
            meter->window_ms = value;
            break;
        case 1:
            if (value < 5.0f) value = 5.0f;
            if (value > 1000.0f) value = 1000.0f;
            meter->smooth_ms = value;
            break;
        default:
            break;
    }
}

// mid_side_meter_reset clears internal analysis state.
static void mid_side_meter_reset(FxHandle* h) {
    (void)h;
}

// mid_side_meter_destroy frees the meter instance memory.
static void mid_side_meter_destroy(FxHandle* h) {
    free(h);
}

// mid_side_meter_get_desc defines the mid/side meter as a pass-through effect.
int mid_side_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "MidSideMeter";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "window_ms";
    out->param_names[1] = "smooth_ms";
    out->param_defaults[0] = 200.0f;
    out->param_defaults[1] = 80.0f;
    out->latency_samples = 0;
    return 1;
}

// mid_side_meter_create allocates a meter instance and binds its vtable.
int mid_side_meter_create(const FxDesc* desc,
                          FxHandle** out_handle,
                          FxVTable* out_vt,
                          uint32_t sample_rate,
                          uint32_t max_block,
                          uint32_t max_channels) {
    (void)desc;
    (void)sample_rate;
    (void)max_block;
    (void)max_channels;

    FxMidSideMeter* meter = (FxMidSideMeter*)calloc(1, sizeof(FxMidSideMeter));
    if (!meter) return 0;
    meter->window_ms = 200.0f;
    meter->smooth_ms = 80.0f;

    out_vt->process = mid_side_meter_process;
    out_vt->set_param = mid_side_meter_set_param;
    out_vt->reset = mid_side_meter_reset;
    out_vt->destroy = mid_side_meter_destroy;

    *out_handle = (FxHandle*)meter;
    return 1;
}
