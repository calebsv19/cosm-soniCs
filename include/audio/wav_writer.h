#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "daw/save_file.h"

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

// Distinguishes prepublication failure from published bytes whose directory sync failed.
DawSaveResult wav_write_pcm16_dithered_result(const char* path, const float* data, uint64_t frames,
                                             int channels, int rate, uint32_t seed);

// Owns one unpublished WAV candidate and fixed-size interleaved conversion state.
typedef struct WavStreamWriter {
    DawSaveFile save;
    uint64_t frames;
    int channels, rate;
    uint32_t rng;
    bool floating, dither, active, failed;
} WavStreamWriter;

// Starts a candidate whose final frame count is determined by successful appends.
bool wav_stream_begin(WavStreamWriter* writer, const char* path, int channels, int rate,
                      uint32_t seed, bool floating);
// Appends finite interleaved frames without resetting the deterministic dither sequence.
bool wav_stream_append(WavStreamWriter* writer, const float* data, uint64_t frames);
// Publishes only a complete nonempty candidate and preserves the exact durability result.
DawSaveResult wav_stream_finish(WavStreamWriter* writer);
// Discards an unpublished candidate without changing the destination.
void wav_stream_abort(WavStreamWriter* writer);
