#include "audio/wav_writer.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void write_u32_le(FILE* f, uint32_t v){
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xff);
    b[1] = (unsigned char)((v >> 8) & 0xff);
    b[2] = (unsigned char)((v >> 16) & 0xff);
    b[3] = (unsigned char)((v >> 24) & 0xff);
    fwrite(b, 1, 4, f);
}

static void write_u16_le(FILE* f, uint16_t v){
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xff);
    b[1] = (unsigned char)((v >> 8) & 0xff);
    fwrite(b, 1, 2, f);
}

static inline uint32_t lcg_advance(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

bool wav_write_pcm16_dithered(const char* path,
                              const float* data,
                              uint64_t frames,
                              int channels,
                              int sample_rate,
                              uint32_t dither_seed) {
    if (!path || !data || frames == 0 || channels <= 0 || sample_rate <= 0) {
        return false;
    }

    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }

    const uint16_t bits_per_sample = 16;
    const uint16_t audio_format = 1; // PCM
    uint32_t data_bytes = (uint32_t)(frames * (uint64_t)channels * (bits_per_sample / 8));
    uint32_t riff_size = 36 + data_bytes;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16); // PCM fmt chunk size
    write_u16_le(f, audio_format);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, (uint32_t)sample_rate);
    uint32_t byte_rate = (uint32_t)(sample_rate * channels * (bits_per_sample / 8));
    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, bits_per_sample);

    // data chunk
    fwrite("data", 1, 4, f);
    write_u32_le(f, data_bytes);

    const float lsb = 1.0f / 32768.0f;
    bool apply_dither = dither_seed != 0;
    uint32_t rng = dither_seed;
    for (uint64_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            float v = data[i * (uint64_t)channels + (uint64_t)ch];
            if (apply_dither) {
                float r1 = (float)(lcg_advance(&rng) & 0xFFFFFF) / (float)0x1000000;
                float r2 = (float)(lcg_advance(&rng) & 0xFFFFFF) / (float)0x1000000;
                float tpdf = (r1 + r2 - 1.0f) * lsb;
                v += tpdf;
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            int16_t s = (int16_t)lrintf(v * 32767.0f);
            write_u16_le(f, (uint16_t)s);
        }
    }

    fclose(f);
    return true;
}

bool wav_write_pcm16(const char* path, const float* data, uint64_t frames, int channels, int sample_rate) {
    return wav_write_pcm16_dithered(path, data, frames, channels, sample_rate, 0);
}

bool wav_write_f32(const char* path, const float* data, uint64_t frames, int channels, int sample_rate) {
    if (!path || !data || frames == 0 || channels <= 0 || sample_rate <= 0) {
        return false;
    }

    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }

    const uint16_t bits_per_sample = 32;
    const uint16_t audio_format = 3; // IEEE float
    uint32_t data_bytes = (uint32_t)(frames * (uint64_t)channels * (bits_per_sample / 8));
    uint32_t riff_size = 36 + data_bytes;

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, audio_format);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, (uint32_t)sample_rate);
    uint32_t byte_rate = (uint32_t)(sample_rate * channels * (bits_per_sample / 8));
    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_bytes);

    size_t total = (size_t)frames * (size_t)channels;
    fwrite(data, sizeof(float), total, f);
    fclose(f);
    return true;
}
