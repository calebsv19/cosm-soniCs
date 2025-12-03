// fx_softclip.c - light saturation via tanh (interleaved, in-place)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

typedef struct FxSoftClip {
    float drive;     // pre-gain before tanh
    float makeup;    // output makeup gain (linear)
} FxSoftClip;

// stable clamps (no <math.h> fminf/fmaxf dependency variability on some toolchains)
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float db_to_lin(float db) {
    return powf(10.0f, db * (1.0f / 20.0f));
}

static void softclip_process(FxHandle* h, const float* in, float* out, int frames, int channels) 
{
    (void)in; // process in-place
    FxSoftClip* sc = (FxSoftClip*)h;
    const int n = frames * channels;
    float* p = out;
    const float drive  = sc->drive;
    const float makeup = sc->makeup;

    for (int i = 0; i < n; ++i) {
        float x = p[i] * drive;
        // tanh soft saturation; fast enough at audio block sizes
        float y = tanhf(x);
        p[i] = y * makeup;
    }
}

static void softclip_set_param(FxHandle* h, uint32_t idx, float value) {
    FxSoftClip* sc = (FxSoftClip*)h;
    switch (idx) {
        case 0: // drive (linear)
            sc->drive = clampf(value, 0.1f, 20.0f);
            break;
        case 1: // makeupGain_dB
            sc->makeup = db_to_lin(clampf(value, -24.0f, 24.0f));
            break;
        default:
            break;
    }
}

static void softclip_reset(FxHandle* h) {
    (void)h; // stateless
}

static void softclip_destroy(FxHandle* h) {
    free(h);
}

int softclip_get_desc(FxDesc *out) {
    if (!out) return 0;
    out->name          = "SoftClip";
    out->api_version   = FX_API_VERSION;
    out->flags         = FX_FLAG_INPLACE_OK;
    out->num_inputs    = 1;
    out->num_outputs   = 1;
    out->num_params    = 2;
    out->param_names[0]    = "drive";
    out->param_names[1]    = "makeup_dB";
    out->param_defaults[0] = 1.0f;   // unity
    out->param_defaults[1] = 0.0f;   // 0 dB
    out->latency_samples   = 0;
    return 1;
}

int softclip_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels) {
    (void)desc; (void)sample_rate; (void)max_block; (void)max_channels;

    FxSoftClip* sc = (FxSoftClip*)calloc(1, sizeof(FxSoftClip));
    if (!sc) return 0;

    sc->drive  = 1.0f;
    sc->makeup = 1.0f;

    out_vt->process   = softclip_process;
    out_vt->set_param = softclip_set_param;
    out_vt->reset     = softclip_reset;
    out_vt->destroy   = softclip_destroy;

    *out_handle = (FxHandle*)sc;
    return 1;
}

