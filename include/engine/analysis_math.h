#pragma once
#include <math.h>

// Evaluates a flat, Hann-windowed tonal peak amplitude at one frequency (not noise density or loudness).
static inline float engine_analysis_tone_db(const float* samples, int frames, int rate, double hz) {
    if (!samples || frames < 3 || rate <= 0 || hz < 0 || hz > rate * 0.5)
        return -160.0f;
    double real = 0, imaginary = 0, weight = 0;
    const double tau = 6.2831853071795864769;
    for (int i = 0; i < frames; ++i) {
        double window = 0.5 - 0.5 * cos(tau * i / (frames - 1));
        double value = isfinite(samples[i]) ? samples[i] * window : 0;
        double phase = tau * hz * i / rate;
        real += value * cos(phase);
        imaginary -= value * sin(phase);
        weight += window;
    }
    double scale = (hz == 0 || hz == rate * 0.5) ? 1.0 : 2.0;
    double amplitude = scale * hypot(real, imaginary) / weight;
    return (float)(20.0 * log10(fmax(amplitude, 1e-8)));
}

// Uses the same logarithmic frequency grid in analysis and display, bounded by Nyquist.
static inline float engine_analysis_frequency(int bin, int count, int rate, float low, float high) {
    if (rate <= 0 || count < 2)
        return 0;
    high = fminf(high, rate * 0.5f);
    low = fminf(low, high);
    if (bin <= 0)
        return low;
    if (bin >= count - 1)
        return high;
    return low * powf(high / low, (float)bin / (count - 1));
}

// Converts an arithmetic mean of window powers back to tonal amplitude dB.
static inline float engine_analysis_power_mean_db(const float* db, int count, int stride) {
    if (!db || count < 1 || stride < 1)
        return -160.0f;
    double power = 0;
    for (int i = 0; i < count; ++i)
        power += pow(10.0, db[i * stride] / 10.0);
    return (float)(10.0 * log10(fmax(power / count, 1e-16)));
}

#define ENGINE_ANALYSIS_MAX_FRAMES 2048
#define ENGINE_ANALYSIS_MAX_BINS 256

// Holds bounded worker-owned coefficients for the existing arbitrary-frequency tonal transform.
typedef struct EngineAnalysisPlan {
    int frames, bins;
    double window[ENGINE_ANALYSIS_MAX_FRAMES];
    double cosine[ENGINE_ANALYSIS_MAX_BINS], sine[ENGINE_ANALYSIS_MAX_BINS], scale[ENGINE_ANALYSIS_MAX_BINS];
} EngineAnalysisPlan;

// Prepares the Hann weights and oscillator steps once for an immutable worker configuration.
static inline int engine_analysis_prepare(EngineAnalysisPlan* plan, int frames, int bins,
                                          int rate, float low, float high) {
    if (!plan || frames < 3 || frames > ENGINE_ANALYSIS_MAX_FRAMES || bins < 2 || bins > ENGINE_ANALYSIS_MAX_BINS || rate <= 0 ||
        !isfinite(low) || !isfinite(high) || low <= 0 || high < low) return 0;
    plan->frames = frames;
    plan->bins = bins;
    double weight = 0;
    const double tau = 6.2831853071795864769;
    for (int i = 0; i < frames; ++i) {
        plan->window[i] = .5 - .5 * cos(tau * i / (frames - 1));
        weight += plan->window[i];
    }
    for (int b = 0; b < bins; ++b) {
        double hz = engine_analysis_frequency(b, bins, rate, low, high);
        plan->cosine[b] = cos(tau * hz / rate);
        plan->sine[b] = sin(tau * hz / rate);
        plan->scale[b] = (hz == 0 || hz == rate * .5 ? 1.0 : 2.0) / weight;
    }
    return 1;
}

// Evaluates the prepared log grid with double-precision oscillators and one window pass per packet.
static inline void engine_analysis_compute(const EngineAnalysisPlan* plan, const float* samples,
                                           float* bins) {
    double weighted[ENGINE_ANALYSIS_MAX_FRAMES];
    for (int i = 0; i < plan->frames; ++i)
        weighted[i] = isfinite(samples[i]) ? samples[i] * plan->window[i] : 0;
    for (int b = 0; b < plan->bins; ++b) {
        double real = 0, imaginary = 0, c = 1, s = 0;
        double dc = plan->cosine[b], ds = plan->sine[b];
        for (int i = 0; i < plan->frames; ++i) {
            real += weighted[i] * c;
            imaginary -= weighted[i] * s;
            double next_c = c * dc - s * ds;
            s = s * dc + c * ds;
            c = next_c;
        }
        double amplitude = hypot(real, imaginary) * plan->scale[b];
        bins[b] = (float)(20.0 * log10(fmax(amplitude, 1e-8)));
    }
}
