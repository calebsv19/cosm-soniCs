#pragma once

#include <stdint.h>

// Defines the supported fade curve shapes for clip in/out ramps.
typedef enum {
    ENGINE_FADE_CURVE_LINEAR = 0,
    ENGINE_FADE_CURVE_S_CURVE,
    ENGINE_FADE_CURVE_LOGARITHMIC,
    ENGINE_FADE_CURVE_EXPONENTIAL,
    ENGINE_FADE_CURVE_COUNT
} EngineFadeCurve;
