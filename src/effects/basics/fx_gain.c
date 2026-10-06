// Existing Gain effect with a 20 ms sample-counted amplitude transition.
#include "effects/effects_api.h"
#include "effects/sample_ramp.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

// Owns the audible gain ramp and distinguishes authored initialization from live edits.
typedef struct FxGain {
    FxSampleRamp gain;
    uint32_t ramp_samples;
    bool rendered;
} FxGain;

// Applies one shared gain per frame for in-place or separate interleaved buffers.
static void gain_process(FxHandle* handle, const float* in, float* out, int frames, int channels) {
    FxGain* gain = (FxGain*)handle;
    if (!in || !out || frames <= 0 || channels <= 0)
        return;
    gain->rendered = true;
    for (int frame = 0; frame < frames; ++frame) {
        float value = fx_sample_ramp_next(&gain->gain);
        for (int ch = 0; ch < channels; ++ch)
            out[(size_t)frame * channels + ch] = in[(size_t)frame * channels + ch] * value;
    }
}

// Accepts finite dB values and ramps only after the instance has begun rendering.
static void gain_set_param(FxHandle* handle, uint32_t index, float value) {
    FxGain* gain = (FxGain*)handle;
    if (index || !isfinite(value))
        return;
    float linear = powf(10, fminf(24, fmaxf(-96, value)) * .05f);
    if (gain->rendered)
        fx_sample_ramp_target(&gain->gain, linear, gain->ramp_samples);
    else
        fx_sample_ramp_reset(&gain->gain, linear);
}

// Snaps to the accepted target for deterministic initialization or transport reset.
static void gain_reset(FxHandle* handle) {
    FxGain* gain = (FxGain*)handle;
    fx_sample_ramp_reset(&gain->gain, gain->gain.target);
    gain->rendered = false;
}

// Releases the control state prepared before rendering.
static void gain_destroy(FxHandle* handle) { free(handle); }

// Describes the existing Gain control and its processor-owned sample smoothing.
int gain_get_desc(FxDesc* out) {
    if (!out)
        return 0;
    *out = (FxDesc){.name = "Gain",
                    .api_version = FX_API_VERSION,
                    .flags = FX_FLAG_INPLACE_OK | FX_FLAG_SAMPLE_PARAM_SMOOTHING,
                    .num_inputs = 1,
                    .num_outputs = 1,
                    .num_params = 1,
                    .param_names = {"gain_dB"},
                    .param_defaults = {0}};
    return 1;
}

// Prepares the existing default unity gain and the rate-scaled transition length.
int gain_create(const FxDesc* desc, FxHandle** out, FxVTable* vt, uint32_t rate, uint32_t block,
                uint32_t channels) {
    (void)desc;
    (void)block;
    (void)channels;
    if (!out || !vt || !rate)
        return 0;
    FxGain* gain = calloc(1, sizeof(*gain));
    if (!gain)
        return 0;
    gain->ramp_samples = (uint32_t)fmax(1, round(rate * .020));
    fx_sample_ramp_reset(&gain->gain, 1);
    *vt = (FxVTable){
        .process = gain_process, .set_param = gain_set_param, .reset = gain_reset, .destroy = gain_destroy};
    *out = (FxHandle*)gain;
    return 1;
}
