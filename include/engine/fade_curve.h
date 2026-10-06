#pragma once

#include <stdint.h>
#include <math.h>

// Defines the supported fade curve shapes for clip in/out ramps.
typedef enum {
    ENGINE_FADE_CURVE_LINEAR = 0,
    ENGINE_FADE_CURVE_S_CURVE,
    ENGINE_FADE_CURVE_LOGARITHMIC,
    ENGINE_FADE_CURVE_EXPONENTIAL,
    ENGINE_FADE_CURVE_COUNT
} EngineFadeCurve;

// Evaluates the existing editor curve definitions independently of the UI.
static inline float engine_fade_curve_eval(EngineFadeCurve curve, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    switch (curve) {
    case ENGINE_FADE_CURVE_S_CURVE:
        return t * t * (3.0f - 2.0f * t);
    case ENGINE_FADE_CURVE_LOGARITHMIC:
        return powf(t, 0.35f);
    case ENGINE_FADE_CURVE_EXPONENTIAL:
        return powf(t, 2.2f);
    case ENGINE_FADE_CURVE_LINEAR:
    default:
        return t;
    }
}
