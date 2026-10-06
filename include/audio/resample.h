#pragma once
#include <stdbool.h>
#include <stdint.h>

// Converts interleaved finite samples offline with a normalized windowed-sinc filter; caller owns output.
bool audio_resample(const float* input, uint64_t frames, int channels, int source_rate, int target_rate,
                    float** output, uint64_t* output_frames);

// Converts with cooperative cancellation, preserving the ordinary converter's exact sample math.
bool audio_resample_controlled(const float* input, uint64_t frames, int channels, int source_rate,
                               int target_rate, float** output, uint64_t* output_frames,
                               bool (*cancelled)(void*), void* user);
