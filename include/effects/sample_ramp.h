#pragma once
#include <stdint.h>

// Tracks a linear parameter transition in audio samples rather than render blocks.
typedef struct FxSampleRamp {
    float current;
    float target;
    float increment;
    uint32_t remaining;
} FxSampleRamp;

// Starts at an exact authored value without a startup fade.
static inline void fx_sample_ramp_reset(FxSampleRamp* ramp, float value) {
    *ramp = (FxSampleRamp){.current = value, .target = value};
}

// Retargets from the current audible value over a fixed number of future samples.
static inline void fx_sample_ramp_target(FxSampleRamp* ramp, float value, uint32_t samples) {
    if (value == ramp->target)
        return;
    if (!samples) {
        fx_sample_ramp_reset(ramp, value);
        return;
    }
    ramp->target = value;
    ramp->remaining = samples;
    ramp->increment = (value - ramp->current) / samples;
}

// Advances one sample and lands exactly on the target at the end of the transition.
static inline float fx_sample_ramp_next(FxSampleRamp* ramp) {
    if (ramp->remaining) {
        if (--ramp->remaining)
            ramp->current += ramp->increment;
        else
            ramp->current = ramp->target;
    }
    return ramp->current;
}
