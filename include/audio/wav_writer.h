#pragma once

#include <stdbool.h>
#include <stdint.h>

// Write 16-bit PCM. `dither_seed` may be 0 to disable TPDF dither; non-zero enables it.
bool wav_write_pcm16_dithered(const char* path,
                              const float* data,
                              uint64_t frames,
                              int channels,
                              int sample_rate,
                              uint32_t dither_seed);

// Backwards compatible helper (no dither).
bool wav_write_pcm16(const char* path, const float* data, uint64_t frames, int channels, int sample_rate);
bool wav_write_f32(const char* path, const float* data, uint64_t frames, int channels, int sample_rate);
