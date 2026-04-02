#include "engine/engine.h"
#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void fail(const char* message) {
    fprintf(stderr, "engine_smoke_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static void write_u16_le(FILE* fp, uint16_t value) {
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_u32_le(FILE* fp, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_test_wav_or_fail(const char* path, int sample_rate) {
    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t frames = (uint32_t)(sample_rate > 0 ? sample_rate / 10 : 4410);
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = (uint32_t)sample_rate * (uint32_t)block_align;
    const uint32_t data_size = frames * (uint32_t)block_align;
    const uint32_t riff_size = 36u + data_size;
    FILE* fp = NULL;
    uint32_t i = 0;

    if (mkdir("tmp", 0755) != 0 && errno != EEXIST) {
        fail("failed to create tmp directory");
    }

    fp = fopen(path, "wb");
    if (!fp) {
        fail("failed to create wav fixture");
    }

    fwrite("RIFF", 1, 4, fp);
    write_u32_le(fp, riff_size);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    write_u32_le(fp, 16u);
    write_u16_le(fp, 1u);
    write_u16_le(fp, channels);
    write_u32_le(fp, (uint32_t)sample_rate);
    write_u32_le(fp, byte_rate);
    write_u16_le(fp, block_align);
    write_u16_le(fp, bits_per_sample);
    fwrite("data", 1, 4, fp);
    write_u32_le(fp, data_size);

    for (i = 0; i < frames; ++i) {
        write_u16_le(fp, 0u);
    }

    fclose(fp);
}

int main(void) {
    EngineRuntimeConfig cfg;
    const char* clip_path = "tmp/engine_smoke_test.wav";
    config_set_defaults(&cfg);

    write_test_wav_or_fail(clip_path, cfg.sample_rate);

    Engine* engine = engine_create(&cfg);
    if (!engine) {
        fail("engine_create failed");
    }

    engine_set_logging(engine, true, true, false);

    expect(engine_get_track_count(engine) >= 1, "engine missing default track");
    const int track_index = 0;

    int clip_index = -1;
    expect(engine_add_clip_to_track(engine, track_index, clip_path, 0, &clip_index),
           "failed to add clip");
    expect(clip_index >= 0, "invalid clip index");

    const uint64_t fade_frames = (uint64_t)(cfg.sample_rate * 0.01f);
    expect(engine_clip_set_fades(engine, track_index, clip_index, fade_frames, fade_frames),
           "failed to set fades");

    const uint64_t seek_frame = (uint64_t)cfg.sample_rate / 2;
    expect(engine_transport_seek(engine, seek_frame), "transport seek failed");

    expect(engine_transport_set_loop(engine, true, 0, cfg.sample_rate), "loop setup failed");
    expect(engine_transport_play(engine), "transport play failed");
    expect(engine_transport_is_playing(engine), "transport should report playing");
    expect(engine_transport_stop(engine), "transport stop failed");
    expect(!engine_transport_is_playing(engine), "transport should report stopped");

    engine_destroy(engine);
    (void)unlink(clip_path);
    printf("engine_smoke_test: success\n");
    return 0;
}
