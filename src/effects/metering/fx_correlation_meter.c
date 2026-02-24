// fx_correlation_meter.c - pass-through correlation meter stub
#include <stdlib.h>
#include <string.h>
#include "effects/effects_api.h"

// FxCorrelationMeter holds analysis state for a correlation meter effect.
typedef struct FxCorrelationMeter {
    float window_ms;
    float smooth_ms;
} FxCorrelationMeter;

// correlation_meter_process passes audio through unchanged for analysis-only use.
static void correlation_meter_process(FxHandle* h,
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

// correlation_meter_set_param ignores params until analysis controls are added.
static void correlation_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    FxCorrelationMeter* meter = (FxCorrelationMeter*)h;
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

// correlation_meter_reset clears internal analysis state.
static void correlation_meter_reset(FxHandle* h) {
    (void)h;
}

// correlation_meter_destroy frees the meter instance memory.
static void correlation_meter_destroy(FxHandle* h) {
    free(h);
}

// correlation_meter_get_desc defines the correlation meter as a pass-through effect.
int correlation_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "CorrelationMeter";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "window_ms";
    out->param_names[1] = "smooth_ms";
    out->param_defaults[0] = 300.0f;
    out->param_defaults[1] = 120.0f;
    out->latency_samples = 0;
    return 1;
}

// correlation_meter_create allocates a meter instance and binds its vtable.
int correlation_meter_create(const FxDesc* desc,
                             FxHandle** out_handle,
                             FxVTable* out_vt,
                             uint32_t sample_rate,
                             uint32_t max_block,
                             uint32_t max_channels) {
    (void)desc;
    (void)sample_rate;
    (void)max_block;
    (void)max_channels;

    FxCorrelationMeter* meter = (FxCorrelationMeter*)calloc(1, sizeof(FxCorrelationMeter));
    if (!meter) return 0;
    meter->window_ms = 300.0f;
    meter->smooth_ms = 120.0f;

    out_vt->process = correlation_meter_process;
    out_vt->set_param = correlation_meter_set_param;
    out_vt->reset = correlation_meter_reset;
    out_vt->destroy = correlation_meter_destroy;

    *out_handle = (FxHandle*)meter;
    return 1;
}
