#include "audio/resample.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RESAMPLE_PHASES 1024
#define RESAMPLE_LOBES 32
#define RESAMPLE_MAX_RATIO 32.0
#define RESAMPLE_PI 3.14159265358979323846

// Evaluates a Blackman-windowed low-pass kernel centered on the requested source position.
static double kernel(double distance, double cutoff, int radius) {
    if (fabs(distance) >= radius)
        return 0;
    double x = RESAMPLE_PI * distance;
    double sinc = fabs(distance) < 1e-12 ? cutoff : sin(cutoff * x) / x;
    double window = .42 + .5 * cos(x / radius) + .08 * cos(2 * x / radius);
    return sinc * window;
}

// Performs bounded offline conversion with interpolated phase coefficients and exact rational positioning.
bool audio_resample_controlled(const float* input, uint64_t frames, int channels, int source_rate, int target_rate,
                    float** output, uint64_t* output_frames, bool (*cancelled)(void*), void* user) {
    if (!input || !frames || channels <= 0 || source_rate <= 0 || target_rate <= 0 || !output ||
        !output_frames || frames > SIZE_MAX / sizeof(float) / (size_t)channels)
        return false;
    double ratio = (double)target_rate / source_rate;
    if (ratio < 1 / RESAMPLE_MAX_RATIO || ratio > RESAMPLE_MAX_RATIO)
        return false;
    long double count = floorl((long double)frames * target_rate / source_rate + .5L);
    if (count < 1)
        count = 1;
    if (count > SIZE_MAX / sizeof(float) / (size_t)channels || count > UINT64_MAX)
        return false;
    uint64_t result_frames = (uint64_t)count;
    for (size_t n = 0; n < (size_t)frames * channels; ++n) {
        if ((n % 4096 == 0 && cancelled && cancelled(user)) || !isfinite(input[n])) return false;
    }
    float* result = malloc((size_t)result_frames * channels * sizeof(float));
    if (!result)
        return false;
    if (source_rate == target_rate) {
        size_t total = (size_t)frames * channels;
        for (size_t n = 0; n < total; n += 4096) {
            if (cancelled && cancelled(user)) { free(result); return false; }
            size_t count = total - n > 4096 ? 4096 : total - n;
            memcpy(result + n, input + n, count * sizeof(float));
        }
        *output = result;
        *output_frames = result_frames;
        return true;
    }
    double cutoff = .94 * fmin(1, ratio);
    int radius = (int)ceil(RESAMPLE_LOBES / cutoff), taps = radius * 2 + 1;
    float* coefficients = malloc((size_t)(RESAMPLE_PHASES + 1) * taps * sizeof(float));
    if (!coefficients) {
        free(result);
        return false;
    }
    for (int phase = 0; phase <= RESAMPLE_PHASES; ++phase) {
        if (cancelled && cancelled(user)) { free(coefficients); free(result); return false; }
        double fraction = (double)phase / RESAMPLE_PHASES, sum = 0;
        float* row = coefficients + (size_t)phase * taps;
        for (int tap = 0; tap < taps; ++tap) {
            row[tap] = (float)kernel(tap - radius - fraction, cutoff, radius);
            sum += row[tap];
        }
        for (int tap = 0; tap < taps; ++tap)
            row[tap] = (float)(row[tap] / sum);
    }
    uint64_t source_frame = 0, remainder = 0;
    for (uint64_t frame = 0; frame < result_frames; ++frame) {
        if (frame % 128 == 0 && cancelled && cancelled(user)) { free(coefficients); free(result); return false; }
        double phase = (double)remainder * RESAMPLE_PHASES / target_rate;
        int low = (int)phase;
        double fraction = phase - low;
        const float* a = coefficients + (size_t)low * taps;
        const float* b = a + taps;
        for (int ch = 0; ch < channels; ++ch) {
            double sample = 0;
            for (int tap = 0; tap < taps; ++tap) {
                int offset = tap - radius;
                uint64_t position = offset < 0 && source_frame < (uint64_t)-offset ? 0
                                    : offset < 0 ? source_frame - (uint64_t)-offset
                                                 : source_frame + (uint64_t)offset;
                if (position >= frames)
                    position = frames - 1;
                sample += input[(size_t)position * channels + ch] * (a[tap] + (b[tap] - a[tap]) * fraction);
            }
            if (!isfinite(sample) || fabs(sample) > FLT_MAX) {
                free(coefficients);
                free(result);
                return false;
            }
            result[(size_t)frame * channels + ch] = (float)sample;
        }
        remainder += (uint64_t)source_rate;
        source_frame += remainder / (uint64_t)target_rate;
        remainder %= (uint64_t)target_rate;
    }
    free(coefficients);
    *output = result;
    *output_frames = result_frames;
    return true;
}

// Preserves the synchronous converter API for existing offline callers.
bool audio_resample(const float* input, uint64_t frames, int channels, int source_rate, int target_rate,
                    float** output, uint64_t* output_frames) {
    return audio_resample_controlled(input, frames, channels, source_rate, target_rate, output, output_frames, NULL, NULL);
}
