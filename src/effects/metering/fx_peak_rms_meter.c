// fx_peak_rms_meter.c - pass-through peak/RMS meter stub

#include "effects/effects_api.h"

#include <stdlib.h>

// FxPeakRmsMeter holds analysis state for a peak/RMS meter effect.
typedef struct FxPeakRmsMeter {
    int reserved;
} FxPeakRmsMeter;

// peak_rms_meter_process passes audio through unchanged for analysis-only use.
static void peak_rms_meter_process(FxHandle* h,
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

// peak_rms_meter_set_param ignores params until analysis controls are added.
static void peak_rms_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    (void)h;
    (void)idx;
    (void)value;
}

// peak_rms_meter_reset clears internal analysis state.
static void peak_rms_meter_reset(FxHandle* h) {
    (void)h;
}

// peak_rms_meter_destroy frees the meter instance memory.
static void peak_rms_meter_destroy(FxHandle* h) {
    if (h) {
        free(h);
    }
}

// peak_rms_meter_get_desc defines the peak/RMS meter as a pass-through effect.
int peak_rms_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "PeakRmsMeter";
    out->num_params = 0;
    out->flags = FX_FLAG_INPLACE_OK;
    return 1;
}

// peak_rms_meter_create allocates a meter instance and binds its vtable.
int peak_rms_meter_create(const FxDesc* desc,
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

    FxPeakRmsMeter* meter = (FxPeakRmsMeter*)calloc(1, sizeof(FxPeakRmsMeter));
    if (!meter) return 0;

    out_vt->process = peak_rms_meter_process;
    out_vt->set_param = peak_rms_meter_set_param;
    out_vt->reset = peak_rms_meter_reset;
    out_vt->destroy = peak_rms_meter_destroy;
    *out_handle = (FxHandle*)meter;
    return 1;
}
