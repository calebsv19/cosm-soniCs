#define _POSIX_C_SOURCE 200809L
#include "audio/media_clip.h"
#include "audio/resample.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PI 3.14159265358979323846
static int fail_after = -1;
// Injects allocation failure into the production decoder and converter objects.
static bool fail_allocation(void) {
    if (fail_after < 0)
        return false;
    if (!fail_after)
        return true;
    --fail_after;
    return false;
}
// Wraps production allocation for deterministic failure-boundary coverage.
void* daw_test_malloc(size_t size) { return fail_allocation() ? NULL : malloc(size); }
// Wraps production zeroed allocation for deterministic failure-boundary coverage.
void* daw_test_calloc(size_t count, size_t size) { return fail_allocation() ? NULL : calloc(count, size); }
// Preserves realloc ownership when an injected allocation is rejected.
void* daw_test_realloc(void* p, size_t size) { return fail_allocation() ? NULL : realloc(p, size); }
// Releases production-owned memory through the ordinary allocator.
void daw_test_free(void* p) { free(p); }

// Emits an integer without depending on host endianness.
static void integer(FILE* file, uint32_t value, int bytes) {
    for (int n = 0; n < bytes; ++n)
        assert(fputc((value >> (8 * n)) & 255, file) != EOF);
}
// Creates a tiny native/extensible WAV with an odd metadata chunk and exact representable levels.
static void fixture(const char* path, int bits, bool floating, bool extensible, int channels) {
    FILE* f = fopen(path, "wb");
    assert(f);
    unsigned size = 3 * channels * (bits / 8), fmt = extensible ? 40 : 16;
    fwrite("RIFF", 1, 4, f);
    integer(f, 4 + 8 + fmt + 10 + 8 + size + (size & 1), 4);
    fwrite("WAVEfmt ", 1, 8, f);
    integer(f, fmt, 4);
    integer(f, extensible ? 0xfffe : floating ? 3 : 1, 2);
    integer(f, channels, 2);
    integer(f, 48000, 4);
    integer(f, 48000 * channels * (bits / 8), 4);
    integer(f, channels * (bits / 8), 2);
    integer(f, bits, 2);
    if (extensible) {
        integer(f, 22, 2);
        integer(f, bits, 2);
        integer(f, channels == 1 ? 4 : 3, 4);
        integer(f, floating ? 3 : 1, 4);
        const unsigned char tail[12] = {0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113};
        fwrite(tail, 1, 12, f);
    }
    fwrite("JUNK", 1, 4, f);
    integer(f, 1, 4);
    integer(f, 123, 1);
    integer(f, 0, 1);
    fwrite("data", 1, 4, f);
    integer(f, size, 4);
    for (int frame = 0; frame < 3; ++frame)
        for (int ch = 0; ch < channels; ++ch) {
            float value = (frame - 1) * .5f * (ch ? -1 : 1);
            uint32_t raw;
            if (floating)
                memcpy(&raw, &value, 4);
            else if (bits == 8)
                raw = (uint32_t)(value * 128 + 128);
            else
                raw = (uint32_t)(int64_t)(value * (double)(INT64_C(1) << (bits - 1)));
            integer(f, raw, bits / 8);
        }
    if (size & 1)
        integer(f, 0, 1);
    assert(fclose(f) == 0);
}
// Verifies native PCM, float, extensible formats, chunk padding, and malformed frame rejection.
static void formats(void) {
    char path[] = "/tmp/daw-media-formats-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    const int depths[] = {8, 16, 24, 32};
    for (int ext = 0; ext < 2; ++ext)
        for (int ch = 1; ch <= 2; ++ch)
            for (int depth = 0; depth < 5; ++depth) {
                bool floating = depth == 4;
                fixture(path, floating ? 32 : depths[depth], floating, ext, ch);
                AudioMediaClip clip = {0};
                assert(audio_media_clip_load_wav(path, 48000, &clip));
                assert(clip.channels == ch && clip.sample_rate == 48000 && clip.frame_count == 3);
                for (int frame = 0; frame < 3; ++frame)
                    for (int c = 0; c < ch; ++c)
                        assert(clip.samples[frame * ch + c] == (frame - 1) * .5f * (c ? -1 : 1));
                audio_media_clip_free(&clip);
            }
    fixture(path, 24, false, false, 2);
    FILE* file = fopen(path, "r+b");
    assert(file && !fseek(file, 32, SEEK_SET));
    integer(file, 1, 2);
    fclose(file);
    AudioMediaClip unchanged = {.frame_count = 777};
    assert(!audio_media_clip_load_wav(path, 48000, &unchanged) && unchanged.frame_count == 777);
    fixture(path, 16, false, false, 1);
    file = fopen(path, "r+b");
    assert(file && !fseek(file, 20, SEEK_SET));
    integer(file, 6, 2); // A-law is deliberately outside the native PCM/float contract.
    fclose(file);
    assert(!audio_media_clip_load_wav(path, 48000, &unchanged) && unchanged.frame_count == 777);
    fixture(path, 8, false, false, 1);
    file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    fclose(file);
    assert(!truncate(path, length - 1));
    assert(!audio_media_clip_load_wav(path, 48000, &unchanged));
    fixture(path, 32, true, false, 1);
    file = fopen(path, "r+b");
    assert(file && !fseek(file, 54, SEEK_SET));
    integer(file, 0x7fc00000, 4);
    fclose(file);
    assert(!audio_media_clip_load_wav(path, 48000, &unchanged));
    fixture(path, 24, false, true, 2);
    for (int failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        assert(!audio_media_clip_load_wav(path, 44100, &unchanged));
        fail_after = -1;
        assert(unchanged.frame_count == 777 && !unchanged.samples);
    }
    assert(unlink(path) == 0);
}
// Measures a sinusoidal component independently of the converter's implementation.
static double amplitude(const float* data, uint64_t frames, int rate, double hz) {
    double real = 0, imaginary = 0;
    uint64_t count = 0;
    for (uint64_t n = 1024; n + 1024 < frames; ++n) {
        double phase = 2 * PI * hz * n / rate;
        real += data[n] * cos(phase);
        imaginary += data[n] * sin(phase);
        ++count;
    }
    return 2 * hypot(real, imaginary) / count;
}
// Measures passband gain and rejection of a known above-destination-Nyquist tone.
static void spectrum(void) {
    const int frames = 48000;
    float* input = malloc(frames * sizeof(float));
    assert(input);
    double measured[2];
    const double tones[] = {1000, 30000};
    for (int tone = 0; tone < 2; ++tone) {
        for (int n = 0; n < frames; ++n)
            input[n] = .5f * sin(2 * PI * tones[tone] * n / 96000);
        float* output = NULL;
        uint64_t count = 0;
        assert(audio_resample(input, frames, 1, 96000, 48000, &output, &count) && count == 24000);
        measured[tone] = amplitude(output, count, 48000, tone ? 18000 : 1000);
        free(output);
    }
    assert(fabs(measured[0] - .5) < .001 && measured[1] < .00002);
    for (int n = 0; n < frames; ++n)
        input[n] = .5f * sin(2 * PI * 10000 * n / 24000);
    float* output = NULL;
    uint64_t count = 0;
    assert(audio_resample(input, frames, 1, 24000, 48000, &output, &count));
    assert(fabs(amplitude(output, count, 48000, 10000) - .5) < .001);
    assert(amplitude(output, count, 48000, 14000) < .00002);
    printf("media_conversion_test: passband %.8f, downsample alias %.9f\n", measured[0], measured[1]);
    free(output);
    free(input);
}
// Checks rate-pair duration, constant amplitude, channel identity, short edges, and exact pass-through.
static void boundaries(void) {
    const int rates[] = {8000, 44100, 48000, 96000, 192000};
    float input[514];
    for (int i = 0; i < 257; ++i) {
        input[i * 2] = .25f;
        input[i * 2 + 1] = -.5f;
    }
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b) {
            float* output = NULL;
            uint64_t count = 0;
            assert(audio_resample(input, 257, 2, rates[a], rates[b], &output, &count));
            assert(count == (uint64_t)llround(257.0 * rates[b] / rates[a]));
            for (uint64_t n = 0; n < count; ++n)
                assert(fabsf(output[n * 2] - .25f) < 1e-6f && fabsf(output[n * 2 + 1] + .5f) < 1e-6f);
            if (a == b)
                assert(!memcmp(input, output, sizeof(input)));
            free(output);
        }
    float* output = NULL;
    uint64_t count = 123;
    assert(audio_resample(input, 1, 2, 96000, 8000, &output, &count) && count == 1);
    free(output);
    output = NULL;
    assert(!audio_resample(input, 257, 2, 96000, 1, &output, &count));
    assert(!audio_resample(input, UINT64_MAX, 2, 48000, 44100, &output, &count));
    input[0] = NAN;
    assert(!audio_resample(input, 257, 2, 48000, 48000, &output, &count));
}
// Verifies an interior impulse stays centered, symmetric, and isolated to its original channel.
static void impulse(void) {
    float input[4096] = {0};
    input[1024 * 2] = 1;
    float* output = NULL;
    uint64_t frames = 0;
    assert(audio_resample(input, 2048, 2, 96000, 48000, &output, &frames) && frames == 1024);
    double sum = 0;
    int peak = 0;
    for (int n = 0; n < 1024; ++n) {
        assert(output[n * 2 + 1] == 0);
        if (fabsf(output[n * 2]) > fabsf(output[peak * 2]))
            peak = n;
        sum += output[n * 2];
    }
    assert(peak == 512 && fabs(sum - .5) < 1e-5);
    for (int n = 1; n < 128; ++n)
        assert(fabsf(output[(512 - n) * 2] - output[(512 + n) * 2]) < 1e-7f);
    free(output);
}

// Checks the platform/fallback MP3 decoder and common conversion against the same decoded source duration.
static void mp3_decode(const char* path) {
    AudioMediaClip source = {0}, converted = {0};
    assert(audio_media_clip_load(path, 0, &source) && audio_media_clip_load(path, 96000, &converted));
    assert(source.sample_rate == 48000 && converted.sample_rate == 96000 &&
           source.channels == converted.channels);
    assert(converted.frame_count == source.frame_count * 2 && source.frame_count >= 48000);
    double energy = 0;
    for (uint64_t n = 0; n < converted.frame_count * converted.channels; ++n) {
        assert(isfinite(converted.samples[n]));
        energy += (double)converted.samples[n] * converted.samples[n];
    }
    assert(energy > 1);
    audio_media_clip_free(&source);
    audio_media_clip_free(&converted);
    puts("media_conversion_test: MP3 decode and duration conversion passed");
}

// Runs native import and sample-rate conversion contracts against instrumented production objects.
int main(int argc, char** argv) {
    formats();
    boundaries();
    spectrum();
    impulse();
    if (argc > 1)
        mp3_decode(argv[1]);
    puts("media_conversion_test: success (PCM8/16/24/32, float, extensible, malformed input, rate pairs, "
         "filtering, allocation failures)");
    return 0;
}
