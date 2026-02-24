// fx_spectrogram_meter.c - pass-through spectrogram meter stub

#include "effects/effects_api.h"

#include <stdlib.h>

// FxSpectrogramMeter holds analysis state for a spectrogram meter effect.
typedef struct FxSpectrogramMeter {
    float floor_db;
    float ceil_db;
    int palette;
} FxSpectrogramMeter;

// spectrogram_meter_process passes audio through unchanged for analysis-only use.
static void spectrogram_meter_process(FxHandle* h,
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

// spectrogram_meter_set_param ignores params until analysis controls are added.
static void spectrogram_meter_set_param(FxHandle* h, uint32_t idx, float value) {
    FxSpectrogramMeter* meter = (FxSpectrogramMeter*)h;
    if (!meter) {
        return;
    }
    switch (idx) {
        case 0:
            if (value < -120.0f) value = -120.0f;
            if (value > -20.0f) value = -20.0f;
            meter->floor_db = value;
            break;
        case 1:
            if (value < -24.0f) value = -24.0f;
            if (value > 12.0f) value = 12.0f;
            meter->ceil_db = value;
            break;
        case 2: {
            int palette = (int)(value + 0.5f);
            if (palette < 0) palette = 0;
            if (palette > 2) palette = 2;
            meter->palette = palette;
            break;
        }
        default:
            break;
    }
}

// spectrogram_meter_reset clears internal analysis state.
static void spectrogram_meter_reset(FxHandle* h) {
    (void)h;
}

// spectrogram_meter_destroy frees the meter instance memory.
static void spectrogram_meter_destroy(FxHandle* h) {
    if (!h) return;
    free(h);
}

// spectrogram_meter_get_desc defines the spectrogram meter as a pass-through effect.
int spectrogram_meter_get_desc(FxDesc* out) {
    if (!out) return 0;
    out->name = "SpectrogramMeter";
    out->api_version = FX_API_VERSION;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0] = "floor_db";
    out->param_names[1] = "ceil_db";
    out->param_names[2] = "palette";
    out->param_defaults[0] = -80.0f;
    out->param_defaults[1] = 0.0f;
    out->param_defaults[2] = 0.0f;
    out->flags = FX_FLAG_INPLACE_OK;
    out->latency_samples = 0;
    return 1;
}

// spectrogram_meter_create allocates a meter instance and binds its vtable.
int spectrogram_meter_create(const FxDesc* desc,
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

    FxSpectrogramMeter* meter = (FxSpectrogramMeter*)calloc(1, sizeof(FxSpectrogramMeter));
    if (!meter) return 0;
    meter->floor_db = -80.0f;
    meter->ceil_db = 0.0f;
    meter->palette = 0;

    out_vt->process = spectrogram_meter_process;
    out_vt->set_param = spectrogram_meter_set_param;
    out_vt->reset = spectrogram_meter_reset;
    out_vt->destroy = spectrogram_meter_destroy;

    *out_handle = (FxHandle*)meter;
    return 1;
}
