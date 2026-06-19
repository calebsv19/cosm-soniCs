#include "engine/engine.h"
#include "config.h"

#include "test_wav_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void write_test_wav_or_fail(const char* path, int sample_rate) {
    const uint32_t frames = (uint32_t)(sample_rate > 0 ? sample_rate / 10 : 4410);
    daw_test_wav_write_silence_or_fail(path, sample_rate, frames, "engine_smoke_test");
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

    int inserted_track = engine_add_track(engine);
    expect(inserted_track >= 1, "failed to add teardown track");
    expect(engine_insert_track(engine, 0), "failed to insert teardown track");
    expect(engine_remove_track(engine, 0), "failed to remove inserted teardown track");
    expect(engine_remove_track(engine, inserted_track), "failed to remove teardown track");

    engine_destroy(engine);
    (void)unlink(clip_path);
    printf("engine_smoke_test: success\n");
    return 0;
}
