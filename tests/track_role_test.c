#include "config.h"
#include "engine/engine.h"
#include "engine/track_role.h"

#include "test_wav_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char* message) {
    fprintf(stderr, "track_role_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static Engine* create_engine(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;
    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");
    expect(engine_get_track_count(engine) >= 1, "default track missing");
    return engine;
}

static void write_audio_fixture(const char* path) {
    daw_test_wav_write_silence_or_fail(path, 48000, 256, "track_role_test");
}

static void expect_role(const Engine* engine,
                        int track_index,
                        EngineTrackRole expected,
                        const char* message) {
    EngineTrackRole role = ENGINE_TRACK_ROLE_MIXED;
    expect(engine_track_role_resolve(engine, track_index, &role), message);
    if (role != expected) {
        fprintf(stderr,
                "track_role_test: %s: expected %s got %s\n",
                message,
                engine_track_role_label(expected),
                engine_track_role_label(role));
        exit(1);
    }
}

static void test_empty_track_role(void) {
    Engine* engine = create_engine();

    expect_role(engine, 0, ENGINE_TRACK_ROLE_EMPTY, "empty default track role");
    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks != NULL, "tracks missing");
    expect(engine_track_role_from_track(&tracks[0]) == ENGINE_TRACK_ROLE_EMPTY,
           "empty role from track mismatch");

    engine_destroy(engine);
}

static void test_audio_track_role(void) {
    Engine* engine = create_engine();
    char path[] = "tmp/track_role_audio_XXXXXX.wav";
    int fd = mkstemps(path, 4);
    expect(fd >= 0, "mkstemps failed");
    close(fd);
    write_audio_fixture(path);

    int clip_index = -1;
    expect(engine_add_clip_to_track(engine, 0, path, 0, &clip_index), "add audio clip failed");
    expect(clip_index >= 0, "audio clip index missing");
    expect_role(engine, 0, ENGINE_TRACK_ROLE_AUDIO, "audio track role");

    unlink(path);
    engine_destroy(engine);
}

static void test_midi_track_role(void) {
    Engine* engine = create_engine();

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, 48000, &clip_index), "add MIDI clip failed");
    expect(clip_index >= 0, "MIDI clip index missing");
    expect_role(engine, 0, ENGINE_TRACK_ROLE_MIDI, "MIDI track role");

    engine_destroy(engine);
}

static void test_mixed_track_role(void) {
    Engine* engine = create_engine();
    char path[] = "tmp/track_role_mixed_XXXXXX.wav";
    int fd = mkstemps(path, 4);
    expect(fd >= 0, "mkstemps failed");
    close(fd);
    write_audio_fixture(path);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, 48000, &clip_index), "add MIDI clip failed");
    expect(engine_add_clip_to_track(engine, 0, path, 96000, &clip_index), "add audio clip failed");
    expect_role(engine, 0, ENGINE_TRACK_ROLE_MIXED, "mixed MIDI then audio track role");

    int track_index = engine_add_track(engine);
    expect(track_index >= 0, "add track failed");
    expect(engine_add_clip_to_track(engine, track_index, path, 0, &clip_index), "add audio clip to second track failed");
    expect(engine_add_midi_clip_to_track(engine, track_index, 48000, 48000, &clip_index),
           "add MIDI clip to second track failed");
    expect_role(engine, track_index, ENGINE_TRACK_ROLE_MIXED, "mixed audio then MIDI track role");

    unlink(path);
    engine_destroy(engine);
}

static void test_invalid_track_resolve_fails_without_changing_contract(void) {
    Engine* engine = create_engine();

    EngineTrackRole role = ENGINE_TRACK_ROLE_AUDIO;
    expect(!engine_track_role_resolve(engine, -1, &role), "negative track should fail");
    expect(role == ENGINE_TRACK_ROLE_EMPTY, "failed resolve should reset role to empty");
    expect(!engine_track_role_resolve(engine, 99, &role), "out-of-range track should fail");
    expect(role == ENGINE_TRACK_ROLE_EMPTY, "out-of-range resolve should reset role to empty");
    expect(engine_track_role_label((EngineTrackRole)99) != NULL, "unknown label should be stable");

    engine_destroy(engine);
}

int main(void) {
    mkdir("tmp", 0755);
    test_empty_track_role();
    test_audio_track_role();
    test_midi_track_role();
    test_mixed_track_role();
    test_invalid_track_resolve_fails_without_changing_contract();
    puts("track_role_test: success");
    return 0;
}
