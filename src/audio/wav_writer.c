#include "audio/wav_writer.h"
#include "daw/save_file.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Encodes a little-endian integer independently of the host byte order.
static void wav_u32(unsigned char* bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

// Encodes a little-endian RIFF short.
static void wav_u16(unsigned char* bytes, uint16_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

// Advances the deterministic dither generator.
static uint32_t lcg_advance(uint32_t* state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

// Opens a private WAV candidate with a provisional header and bounded conversion state.
bool wav_stream_begin(WavStreamWriter* writer, const char* path, int channels, int rate,
                      uint32_t seed, bool floating) {
    if (!writer) return false;
    *writer = (WavStreamWriter){0};
    unsigned width = floating ? 4 : 2;
    if (!path || channels < 1 || channels > UINT16_MAX / (int)width || rate < 1 ||
        (uint64_t)rate * channels * width > UINT32_MAX) return false;
    writer->channels = channels;
    writer->rate = rate;
    writer->floating = floating;
    writer->dither = seed != 0;
    writer->rng = seed;
    if (!daw_save_file_begin(&writer->save, path)) return false;
    writer->active = true;
    unsigned char header[44] = {0};
    if (fwrite(header, 1, sizeof(header), writer->save.file) != sizeof(header)) {
        wav_stream_abort(writer);
        return false;
    }
    return true;
}

// Converts complete interleaved frames while carrying deterministic dither across chunk boundaries.
bool wav_stream_append(WavStreamWriter* writer, const float* data, uint64_t frames) {
    if (!writer || !writer->active || writer->failed) return false;
    if (!data || !frames) { writer->failed = true; return false; }
    unsigned width = writer->floating ? 4 : 2;
    uint64_t limit = (UINT32_MAX - 36u) / ((uint64_t)writer->channels * width);
    if (frames > limit - writer->frames || frames > SIZE_MAX / sizeof(float) / writer->channels) {
        writer->failed = true;
        return false;
    }
    unsigned char chunk[16384];
    uint64_t total = frames * writer->channels;
    for (uint64_t offset = 0; offset < total;) {
        size_t count = total - offset;
        if (count > sizeof(chunk) / width) count = sizeof(chunk) / width;
        for (size_t i = 0; i < count; ++i) {
            float value = data[offset + i];
            if (!isfinite(value)) { writer->failed = true; return false; }
            if (writer->floating) {
                uint32_t bits;
                memcpy(&bits, &value, sizeof(bits));
                wav_u32(chunk + i * width, bits);
            } else {
                if (writer->dither) {
                    float a = (float)(lcg_advance(&writer->rng) & 0xffffff) / 16777216.0f;
                    float b = (float)(lcg_advance(&writer->rng) & 0xffffff) / 16777216.0f;
                    value += (a + b - 1) / 32768.0f;
                }
                if (value > 1) value = 1;
                if (value < -1) value = -1;
                wav_u16(chunk + i * width, (uint16_t)(int16_t)lrintf(value * 32767.0f));
            }
        }
        if (fwrite(chunk, width, count, writer->save.file) != count) {
            writer->failed = true;
            return false;
        }
        offset += count;
    }
    writer->frames += frames;
    return true;
}

// Removes an unpublished candidate while preserving any preceding destination.
void wav_stream_abort(WavStreamWriter* writer) {
    if (!writer || !writer->active) return;
    daw_save_file_abort(&writer->save);
    writer->active = false;
}

// Seals the actual frame count before durable publication and reports directory uncertainty separately.
DawSaveResult wav_stream_finish(WavStreamWriter* writer) {
    if (!writer || !writer->active) return DAW_SAVE_FAILED;
    if (writer->failed || !writer->frames) { wav_stream_abort(writer); return DAW_SAVE_FAILED; }
    unsigned width = writer->floating ? 4 : 2;
    uint32_t bytes = (uint32_t)(writer->frames * writer->channels * width);
    unsigned char header[44] = {0};
    memcpy(header, "RIFF", 4); wav_u32(header + 4, 36 + bytes); memcpy(header + 8, "WAVEfmt ", 8);
    wav_u32(header + 16, 16); wav_u16(header + 20, writer->floating ? 3 : 1);
    wav_u16(header + 22, (uint16_t)writer->channels); wav_u32(header + 24, (uint32_t)writer->rate);
    wav_u32(header + 28, (uint32_t)writer->rate * writer->channels * width);
    wav_u16(header + 32, (uint16_t)(writer->channels * width));
    wav_u16(header + 34, (uint16_t)(width * 8)); memcpy(header + 36, "data", 4); wav_u32(header + 40, bytes);
    if (fseek(writer->save.file, 0, SEEK_SET) != 0 ||
        fwrite(header, 1, sizeof(header), writer->save.file) != sizeof(header) ||
        !daw_save_file_prepare(&writer->save)) {
        wav_stream_abort(writer);
        return DAW_SAVE_FAILED;
    }
    writer->active = false;
    return daw_save_file_commit(&writer->save);
}

// Delegates contiguous callers to the same checked streaming conversion and publication path.
static DawSaveResult wav_write_atomic(const char* path, const float* data, uint64_t frames,
                                     int channels, int rate, uint32_t seed, bool floating) {
    WavStreamWriter writer;
    if (!data || !frames || !wav_stream_begin(&writer, path, channels, rate, seed, floating))
        return DAW_SAVE_FAILED;
    if (!wav_stream_append(&writer, data, frames)) { wav_stream_abort(&writer); return DAW_SAVE_FAILED; }
    return wav_stream_finish(&writer);
}

// Returns the exact publication/durability result for recorded PCM audio.
DawSaveResult wav_write_pcm16_dithered_result(const char* path, const float* data, uint64_t frames,
                                             int channels, int rate, uint32_t seed) {
    return wav_write_atomic(path, data, frames, channels, rate, seed, false);
}

// Reports success only after both file and directory synchronization succeed.
bool wav_write_pcm16_dithered(const char* path, const float* data, uint64_t frames, int channels, int rate, uint32_t seed) {
    return wav_write_pcm16_dithered_result(path, data, frames, channels, rate, seed) == DAW_SAVE_SYNCED;
}

// Writes checked PCM16 without adding dither.
bool wav_write_pcm16(const char* path, const float* data, uint64_t frames, int channels, int rate) {
    return wav_write_pcm16_dithered(path, data, frames, channels, rate, 0);
}

// Writes checked float32 WAV data without quantization or host-endian assumptions.
bool wav_write_f32(const char* path, const float* data, uint64_t frames, int channels, int rate) {
    return wav_write_atomic(path, data, frames, channels, rate, 0, true) == DAW_SAVE_SYNCED;
}
