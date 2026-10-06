#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include "test_assert.h"

#include <math.h>
#include <unistd.h>

#define CHECK(value, message) daw_test_expect("engine_source_lifetime_test", (value), (message))

// Coordinates a real graph reader with deterministic adoption and retirement boundaries.
typedef struct SourceReader {
    Engine* engine;
    SDL_sem* run;
    SDL_sem* done;
    atomic_bool stop;
    atomic_bool stress;
    atomic_bool invalid;
    float last_sample;
} SourceReader;

// Renders only the source layer so this test isolates source ownership from later track/FX work.
static int read_sources(void* userdata) {
    SourceReader* reader = userdata;
    float block[256];
    while (!atomic_load(&reader->stop)) {
        if (!atomic_load(&reader->stress)) {
            SDL_SemWait(reader->run);
            if (atomic_load(&reader->stop)) break;
        }
        engine_graph_render_track(engine_render_source_graph(reader->engine), block, 128, 0, 0);
        for (int i = 0; i < 256; ++i) {
            if (!isfinite(block[i]) || fabsf(block[i]) > 0.251f) atomic_store(&reader->invalid, true);
        }
        reader->last_sample = block[0];
        engine_source_plan_apply(reader->engine);
        if (!atomic_load(&reader->stress)) SDL_SemPost(reader->done);
    }
    return 0;
}

// Requests one reader step and waits until its read and block-boundary adoption finish.
static void step_reader(SourceReader* reader) {
    SDL_SemPost(reader->run);
    CHECK(SDL_SemWaitTimeout(reader->done, 5000) == 0, "reader did not complete");
}

// Proves old sources/media survive deletion, then stresses edit publication against a live reader.
int main(void) {
    const char* path = "tmp/source_lifetime_fixture.wav";
    float fixture[2048];
    for (int i = 0; i < 2048; ++i) fixture[i] = 0.25f;
    CHECK(wav_write_f32(path, fixture, 1024, 2, 48000), "fixture write");
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "engine create");
    CHECK(engine_add_track(engine) == 1, "move stress destination track");
    int index = -1;
    CHECK(engine_add_clip_to_track(engine, 0, path, 0, &index), "initial clip");
    EngineAudioSource* original_source = engine->tracks[0].clips[index].source;
    char original_id[MEDIA_ID_MAX];
    SDL_strlcpy(original_id, original_source->media_id, sizeof(original_id));
    for (int i = 0; i < 128; ++i) {
        char id[MEDIA_ID_MAX];
        snprintf(id, sizeof(id), "registry-growth-%d", i);
        CHECK(engine_audio_source_get_or_create(engine, id, path) != NULL, "grow source registry");
    }
    CHECK(engine_audio_source_get(engine, original_id) == original_source, "registry growth relocated a borrowed source");
    CHECK(!strcmp(original_source->path, path), "registry growth corrupted source metadata");
    engine_audio_source_clear_all(engine);
    CHECK(engine->tracks[0].clips[index].source == NULL && engine_audio_source_get(engine, original_id) == NULL,
          "registry clear left dangling metadata references");
    SourceReader reader = {.engine = engine};
    reader.run = SDL_CreateSemaphore(0);
    reader.done = SDL_CreateSemaphore(0);
    atomic_init(&reader.stop, false);
    atomic_init(&reader.stress, false);
    atomic_init(&reader.invalid, false);
    CHECK(reader.run && reader.done, "semaphore create");
    // A dedicated source reader takes the same publication path as the render worker, without a device.
    engine->device_started = true;
    engine->worker_thread = SDL_CreateThread(read_sources, "source_reader", &reader);
    CHECK(engine->worker_thread != NULL, "reader create");
    CHECK(engine_remove_clip(engine, 0, index), "clip delete");
    CHECK(engine->media_cache.count == 1, "active plan failed to pin media after deletion");
    step_reader(&reader);
    CHECK(reader.last_sample == 0.25f, "deleted control source invalidated active render source");
    engine_source_plan_collect(engine);
    CHECK(engine->media_cache.count == 0, "retired media not reclaimed");
    step_reader(&reader);
    CHECK(reader.last_sample == 0.0f, "deletion not adopted at next boundary");

    atomic_store(&reader.stress, true);
    SDL_SemPost(reader.run);
    for (int i = 0; i < 2000; ++i) {
        CHECK(engine_add_clip_to_track(engine, 0, path, 0, &index), "stress add");
        CHECK(engine_clip_set_fades(engine, 0, index, 32, 32), "stress fades");
        CHECK(engine_clip_set_fade_curves(engine, 0, index, i % ENGINE_FADE_CURVE_COUNT,
              (i + 1) % ENGINE_FADE_CURVE_COUNT), "stress fade curves");
        CHECK(engine_clip_set_region(engine, 0, index, 16, 512), "stress trim");
        CHECK(engine_clip_add_automation_point(engine, 0, index, ENGINE_AUTOMATION_TARGET_VOLUME,
              0, -0.5f, NULL), "stress automation insert");
        CHECK(engine_clip_update_automation_point(engine, 0, index, ENGINE_AUTOMATION_TARGET_VOLUME,
              0, 64, -0.75f, NULL), "stress automation update");
        CHECK(engine_clip_remove_automation_point(engine, 0, index, ENGINE_AUTOMATION_TARGET_VOLUME, 0),
              "stress automation remove");
        CHECK(engine_move_clip_to_track(engine, 0, index, 1, 64, &index), "stress audio transfer");
        CHECK(engine_move_clip_to_track(engine, 1, index, 0, 0, &index), "stress audio return");
        int anchor_index;
        CHECK(engine_add_clip_to_track(engine, 0, path, 128, &anchor_index), "stress overlap anchor");
        CHECK(engine_clip_set_region(engine, 0, anchor_index, 0, 64), "stress overlap anchor bounds");
        CHECK(engine_track_apply_no_overlap(engine, 0, engine->tracks[0].clips[anchor_index].sampler, NULL),
              "stress overlap transaction");
        while (engine->tracks[0].clip_count > 0)
            CHECK(engine_remove_clip(engine, 0, engine->tracks[0].clip_count - 1), "stress delete");
        CHECK(engine_add_midi_clip_to_track(engine, 0, 0, 1024, &index), "stress MIDI add");
        EngineMidiNote note = {.duration_frames = 512, .note = 60, .velocity = 0.05f};
        CHECK(engine_clip_midi_add_note(engine, 0, index, note, NULL), "stress MIDI insert");
        note.note = 72;
        CHECK(engine_clip_midi_update_note(engine, 0, index, 0, note, NULL), "stress MIDI update");
        CHECK(engine_move_clip_to_track(engine, 0, index, 1, 64, &index), "stress MIDI transfer");
        CHECK(engine_move_clip_to_track(engine, 1, index, 0, 0, &index), "stress MIDI return");
        CHECK(engine_clip_midi_remove_note(engine, 0, index, 0), "stress MIDI remove");
        CHECK(engine_remove_clip(engine, 0, index), "stress MIDI clip delete");
    }
    atomic_store(&reader.stop, true);
    SDL_SemPost(reader.run);
    SDL_WaitThread(engine->worker_thread, NULL);
    engine->worker_thread = NULL;
    engine->device_started = false;
    engine_source_plan_apply(engine);
    engine_source_plan_collect(engine);
    CHECK(!atomic_load(&reader.invalid), "source reader observed invalid samples");
    CHECK(engine->media_cache.count == 0, "stress leaked media pins");
    SDL_DestroySemaphore(reader.run);
    SDL_DestroySemaphore(reader.done);
    engine_destroy(engine);
    unlink(path);
    puts("engine_source_lifetime_test: success (2000 live audio/automation/overlap/MIDI/transfer edit cycles)");
    return 0;
}
