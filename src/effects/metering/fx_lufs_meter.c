// fx_lufs_meter.c - pass-through LUFS meter stub

#include "effects/effects_api.h"

#include <stdlib.h>

// FxLufsMeter holds analysis state for a LUFS meter effect.
typedef struct FxLufsMeter {
    int mode;
    float gate_db;
} FxLufsMeter;

// lufs_meter_process passes audio through unchanged for analysis-only use.
static void lufs_meter_process(FxHandle* h,
                               const float* input,
                               float* output,
                               int frames,
                               int channels) {
    (void)h;
    if (!input || !output) return;
    const int n = frames * channels;
    for (int i = 0; i < n; ++i) {
        output[i] = input[i];
    }
}

// lufs_meter_set_param ignores params until analysis controls are added.
static void lufs_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    FxLufsMeter* meter = (FxLufsMeter*)h;
    if (!meter) {
        return;
    }
    switch (idx) {
        case 0: {
            int mode = (int)(value + 0.5f);
            if (mode < 0) mode = 0;
            if (mode > 2) mode = 2;
            meter->mode = mode;
            break;
        }
        case 1:
            if (value < -70.0f) value = -70.0f;
            if (value > -10.0f) value = -10.0f;
            meter->gate_db = value;
            break;
        default:
            break;
    }
}

// lufs_meter_reset clears internal analysis state.
static void lufs_meter_reset(FxHandle* h) {
    (void)h;
}

// lufs_meter_destroy frees the meter instance memory.
static void lufs_meter_destroy(FxHandle* h) {
    if (h) {
        free(h);
    }
}

// lufs_meter_get_desc defines the LUFS meter as a pass-through effect.
int lufs_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "LufsMeter";
    out->api_version = FX_API_VERSION;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "mode";
    out->param_names[1] = "gate_db";
    out->param_defaults[0] = 1.0f;
    out->param_defaults[1] = -70.0f;
    out->flags = FX_FLAG_INPLACE_OK;
    out->latency_samples = 0;
    return 1;
}

// lufs_meter_create allocates a meter instance and binds its vtable.
int lufs_meter_create(const FxDesc* desc,
                      FxHandle** out_handle,
                      FxVTable* out_vt,
                      uint32_t sample_rate,
                      uint32_t max_block,
                      uint32_t max_channels) {
    (void)desc;
    (void)sample_rate;
    (void)max_block;
    (void)max_channels;
    if (!out_handle || !out_vt) return 0;

    FxLufsMeter* meter = (FxLufsMeter*)calloc(1, sizeof(FxLufsMeter));
    if (!meter) return 0;
    meter->mode = 1;
    meter->gate_db = -70.0f;

    out_vt->process = lufs_meter_process;
    out_vt->set_param = lufs_meter_set_param;
    out_vt->reset = lufs_meter_reset;
    out_vt->destroy = lufs_meter_destroy;
    *out_handle = (FxHandle*)meter;
    return 1;
}
