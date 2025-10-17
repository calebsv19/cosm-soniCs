#include "engine/engine.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

static void fail(const char* message) {
    fprintf(stderr, "engine_smoke_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

int main(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);

    Engine* engine = engine_create(&cfg);
    if (!engine) {
        fail("engine_create failed");
    }

    engine_set_logging(engine, true, true, false);

    expect(engine_get_track_count(engine) >= 1, "engine missing default track");
    const int track_index = 0;

    int clip_index = -1;
    expect(engine_add_clip_to_track(engine, track_index, "assets/audio/test.wav", 0, &clip_index),
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
    printf("engine_smoke_test: success\n");
    return 0;
}
