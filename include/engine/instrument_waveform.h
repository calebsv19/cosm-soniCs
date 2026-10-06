#pragma once
#include <math.h>
#define INSTRUMENT_WAVE_PI 3.14159265358979323846

// Removes components whose fundamental is outside the representable sample-rate band.
static inline double instrument_sine(double phase, double frequency, int rate) {
    return frequency > 0 && frequency < .5 * rate ? sin(phase) : 0;
}

// Corrects a saw discontinuity across one sample on either side of its wrap.
static inline double instrument_poly_blep(double phase, double step) {
    if (phase < step) {
        double t = phase / step;
        return t + t - t * t - 1;
    }
    if (phase > 1 - step) {
        double t = (phase - 1) / step;
        return t * t + t + t + 1;
    }
    return 0;
}

// Preserves the existing centered saw phase while reducing its aliased wrap discontinuity.
static inline double instrument_saw(double phase, double frequency, int rate) {
    if (!(frequency > 0 && frequency < .5 * rate))
        return 0;
    double cycle = phase / (2 * INSTRUMENT_WAVE_PI) + .5;
    cycle -= floor(cycle);
    return 2 * cycle - 1 - instrument_poly_blep(cycle, frequency / rate);
}

// Builds the existing triangle shape from only representable odd harmonics, capped at 63 for bounded work.
static inline double instrument_triangle(double phase, double frequency, int rate) {
    if (!(frequency > 0 && frequency < .5 * rate))
        return 0;
    double value = 0;
    for (int harmonic = 1; harmonic <= 63 && harmonic * frequency < .5 * rate; harmonic += 2)
        value -= cos(harmonic * phase) / ((double)harmonic * harmonic);
    return value * 8 / (INSTRUMENT_WAVE_PI * INSTRUMENT_WAVE_PI);
}
