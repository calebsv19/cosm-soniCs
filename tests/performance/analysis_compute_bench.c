#define _POSIX_C_SOURCE 200809L
#include "engine/analysis_math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Reads monotonic milliseconds for matched transform measurements.
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// Orders measured durations for within-process medians.
static int compare(const void* left, const void* right) {
    double a = *(const double*)left, b = *(const double*)right;
    return (a > b) - (a < b);
}

// Measures the old and prepared production kernels on identical inputs with alternating execution order.
int main(int argc, char** argv) {
    assert(argc == 3);
    int rate = atoi(argv[1]), frames = atoi(argv[2]), count = frames == 2048 ? 256 : 128;
    assert((rate == 44100 || rate == 48000 || rate == 96000) && (frames == 1024 || frames == 2048));
    float samples[2048], actual[256], reference[256];
    for (int i = 0; i < frames; ++i)
        samples[i] = .25 * sin(6.283185307179586 * 440 * i / rate) + .13 * cos(6.283185307179586 * 7823 * i / rate);
    EngineAnalysisPlan plan;
    double began = now_ms();
    assert(engine_analysis_prepare(&plan, frames, count, rate, 20, 20000));
    double prepare_ms = now_ms() - began, durations[2][20], cpu[2] = {0}, error = 0, checksum = 0;
    for (int k = -2; k < 20; ++k) {
        for (int order = 0; order < 2; ++order) {
            int prepared = (k + 2 + order) % 2;
            clock_t cpu_begin = clock();
            began = now_ms();
            if (prepared) engine_analysis_compute(&plan, samples, actual);
            else for (int b = 0; b < count; ++b)
                reference[b] = engine_analysis_tone_db(samples, frames, rate,
                    engine_analysis_frequency(b, count, rate, 20, 20000));
            double elapsed = now_ms() - began;
            if (k >= 0) {
                durations[prepared][k] = elapsed;
                cpu[prepared] += 1000.0 * (clock() - cpu_begin) / CLOCKS_PER_SEC;
            }
        }
        for (int b = 0; b < count; ++b) {
            double delta = fabs(actual[b] - reference[b]);
            if (delta > error) error = delta;
            assert(isfinite(actual[b]) && delta <= .002);
            checksum += actual[b] + reference[b];
        }
    }
    qsort(durations[0], 20, sizeof(double), compare);
    qsort(durations[1], 20, sizeof(double), compare);
    printf("{\"rate\":%d,\"frames\":%d,\"bins\":%d,\"prepare_ms\":%.6f,\"reference_median_ms\":%.6f,"
           "\"prepared_median_ms\":%.6f,\"reference_cpu_ms\":%.6f,\"prepared_cpu_ms\":%.6f,"
           "\"max_error_db\":%.9g,\"plan_bytes\":%zu,\"checksum\":%.9f}\n",
           rate, frames, count, prepare_ms, (durations[0][9] + durations[0][10]) * .5,
           (durations[1][9] + durations[1][10]) * .5, cpu[0], cpu[1], error, sizeof(plan), checksum);
    return 0;
}
