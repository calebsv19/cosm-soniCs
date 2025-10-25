// fx_gain.c - simple linear gain (interleaved, in-place)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_LN10
#define M_LN10 2.302585093f
#endif

typedef struct FxGain {
    float gain_lin;   // linear amplitude
} FxGain;

static inline float db_to_lin(float db) {
    // 20*log10(x) => x = 10^(db/20)
    return powf(10.0f, db * (1.0f / 20.0f));
}

static void gain_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    (void)in; // in==out (in-place); manager may pass same pointer
    FxGain* g = (FxGain*)h;
    const int n = frames * channels;
    float* p = out;
    const float k = g->gain_lin;

    for (int i = 0; i < n; ++i) {
        p[i] = p[i] * k;
    }
}

static void gain_set_param(FxHandle* h, uint32_t idx, float value) {
    FxGain* g = (FxGain*)h;
    switch (idx) {
        case 0: // gain_dB
            if (value < -96.0f) value = -96.0f;
            if (value > 24.0f)  value = 24.0f;
            g->gain_lin = db_to_lin(value);
            break;
        default:
            break;
    }
}

static void gain_reset(FxHandle* h) {
    (void)h; // stateless
}

static void gain_destroy(FxHandle* h) {
    free(h);
}

int gain_get_desc(FxDesc *out) {
    if (!out) return 0;
    out->name          = "Gain";
    out->api_version   = FX_API_VERSION;
    out->flags         = FX_FLAG_INPLACE_OK;  // safe to process in-place
    out->num_inputs    = 1;
    out->num_outputs   = 1;
    out->num_params    = 1;
    out->param_names[0]    = "gain_dB";
    out->param_defaults[0] = 0.0f;
    out->latency_samples   = 0;
    return 1;
}

int gain_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels) {
    (void)desc; (void)sample_rate; (void)max_block; (void)max_channels;

    FxGain* g = (FxGain*)calloc(1, sizeof(FxGain));
    if (!g) return 0;

    // defaults
    g->gain_lin = 1.0f;

    out_vt->process   = gain_process;
    out_vt->set_param = gain_set_param;
    out_vt->reset     = gain_reset;
    out_vt->destroy   = gain_destroy;

    *out_handle = (FxHandle*)g;
    return 1;
}

