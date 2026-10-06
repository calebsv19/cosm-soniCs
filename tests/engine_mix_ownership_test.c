#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include "effects/effects_builtin.h"
#include "test_assert.h"

#include <math.h>
#include <unistd.h>

#define CHECK(value, message) daw_test_expect("engine_mix_ownership_test", (value), (message))

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

// Renders the full track/EQ/FX/meter path while control-side track and effect structures change.
static int read_sources(void* userdata) {
    SourceReader* reader = userdata;
    float block[256], track[256];
    while (!atomic_load(&reader->stop)) {
        if (!atomic_load(&reader->stress)) {
            SDL_SemWait(reader->run);
            if (atomic_load(&reader->stop)) break;
        }
        engine_mix_tracks(reader->engine, 0, 128, block, track, 2);
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

// Compares uninterrupted EQ rendering with a track insertion between blocks to detect history resets.
static void test_eq_history(const EngineRuntimeConfig* config, const char* path) {
    Engine* baseline = engine_create(config);
    Engine* edited = engine_create(config);
    CHECK(baseline && edited, "EQ history engines");
    int clip;
    CHECK(engine_add_clip_to_track(baseline, 0, path, 0, &clip), "EQ baseline clip");
    CHECK(engine_add_clip_to_track(edited, 0, path, 0, &clip), "EQ edited clip");
    EngineEqCurve curve = {0};
    curve.low_cut.enabled = true;
    curve.low_cut.freq_hz = 100.0f;
    CHECK(engine_set_track_eq_curve(baseline, 0, &curve) && engine_set_track_eq_curve(edited, 0, &curve), "EQ curves");
    float expected[256], actual[256], scratch[256];
    engine_mix_tracks(baseline, 0, 128, expected, scratch, 2);
    engine_mix_tracks(edited, 0, 128, actual, scratch, 2);
    CHECK(engine_insert_track(edited, 0), "EQ shift track");
    engine_mix_tracks(baseline, 128, 128, expected, scratch, 2);
    engine_mix_tracks(edited, 128, 128, actual, scratch, 2);
    for (int i = 0; i < 256; ++i) CHECK(fabsf(expected[i] - actual[i]) < 1e-7f, "track movement reset EQ history");
    engine_destroy(edited);
    engine_destroy(baseline);
}

// Checks compact publication payloads, alternating banks, lifetime rejection, and clear semantics.
static void test_meter_publication(const EngineRuntimeConfig* config) {
    Engine* engine = engine_create(config);
    EngineMixState* mix = calloc(1, sizeof(*mix));
    EngineFxMeterBank* track_bank = calloc(1, sizeof(*track_bank));
    CHECK(engine && mix && track_bank, "meter publication fixture");
    EngineTrack track = engine->tracks[0];
    EngineMeterState meter = {.peak = 0.5f, .rms = 0.25f};
    mix->owner = engine;
    mix->tracks = &track;
    mix->track_count = 1;
    mix->track_meters = &meter;
    mix->track_fx_meters = track_bank;
    for (int frame = 0; frame < 4; ++frame) {
        mix->master_fx_meters.count = track_bank->count = FX_MASTER_MAX;
        for (int i = 0; i < FX_MASTER_MAX; ++i) {
            EngineFxMeterTap* tap = &track_bank->taps[i];
            tap->id = (FxInstId)(100 + i);
            memset(&tap->snapshot, 0, sizeof(tap->snapshot));
            tap->snapshot.valid = true;
            tap->snapshot.peak = (float)(frame + i) / 32.0f;
            mix->master_fx_meters.taps[i] = *tap;
        }
        engine_publish_mix_meters(mix);
        for (int i = 0; i < FX_MASTER_MAX; ++i) {
            EngineFxMeterSnapshot actual;
            CHECK(engine_get_track_fx_meter_snapshot(engine, 0, track_bank->taps[i].id, &actual), "track FX read");
            CHECK(memcmp(&actual, &track_bank->taps[i].snapshot, sizeof(actual)) == 0, "track FX payload changed");
            CHECK(engine_get_master_fx_meter_snapshot(engine, track_bank->taps[i].id, &actual), "master FX read");
            CHECK(memcmp(&actual, &track_bank->taps[i].snapshot, sizeof(actual)) == 0, "master FX payload changed");
        }
    }
    EngineFxMeterSnapshot actual;
    ++track.runtime_id;
    engine_publish_mix_meters(mix);
    CHECK(!engine_get_track_fx_meter_snapshot(engine, 0, 100, &actual), "stale track identity accepted");
    --track.runtime_id;
    track_bank->count = mix->master_fx_meters.count = 0;
    engine_publish_mix_meters(mix);
    CHECK(!engine_get_master_fx_meter_snapshot(engine, 100, &actual), "removed meter still visible");
    CHECK(!engine_get_track_fx_meter_snapshot(engine, 0, 100, &actual), "removed track meter still visible");
    engine_fx_meter_clear_all(engine);
    for (int row = 0; row < 2; ++row) {
        CHECK(engine->master_fx_meter_snapshots[row].count == 0, "master clear");
        CHECK(engine->track_fx_meter_snapshots[row * engine->track_fx_meter_capacity].count == 0, "track clear");
    }
    printf("meter storage: history_bank=%zu published_bank=%zu double_buffer_saving=%zu bytes per track/master\n",
           sizeof(EngineFxMeterBank), sizeof(EngineFxMeterSnapshotBank),
           2 * (sizeof(EngineFxMeterBank) - sizeof(EngineFxMeterSnapshotBank)));
    free(track_bank);
    free(mix);
    engine_destroy(engine);
}

// Proves old mixer state survives track deletion, then stresses structural and scalar edit publication.
int main(void) {
    const char* path = "tmp/mix_ownership_fixture.wav";
    float fixture[2048];
    for (int i = 0; i < 2048; ++i) fixture[i] = 0.25f;
    CHECK(wav_write_f32(path, fixture, 1024, 2, 48000), "fixture write");
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;
    test_eq_history(&cfg, path);
    test_meter_publication(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "engine create");
    int index = -1;
    CHECK(engine_add_clip_to_track(engine, 0, path, 0, &index), "initial clip");
    CHECK(engine->fxm != NULL, "control effects create");
    FxInstId gain = engine_fx_track_add(engine, 0, 1);
    CHECK(gain != 0, "initial gain effect");
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
    CHECK(engine_remove_track(engine, 0), "track delete");
    CHECK(engine->media_cache.count == 1, "active plan failed to pin media after deletion");
    step_reader(&reader);
    CHECK(reader.last_sample == 0.25f, "deleted control source invalidated active render source");
    engine_source_plan_collect(engine);
    CHECK(engine->media_cache.count == 0, "retired media not reclaimed");
    step_reader(&reader);
    CHECK(reader.last_sample == 0.0f, "deletion not adopted at next boundary");

    SDL_LockMutex(engine->meter_mutex);
    step_reader(&reader); // Publishing contention must not block the audio worker.
    SDL_UnlockMutex(engine->meter_mutex);
    atomic_store(&reader.stress, true);
    SDL_SemPost(reader.run);
    for (int i = 0; i < 300; ++i) {
        CHECK(engine_insert_track(engine, 0), "stress track insert");
        CHECK(engine_add_clip_to_track(engine, 0, path, 0, &index), "stress add");
        gain = engine_fx_track_add(engine, 0, 1);
        CHECK(gain != 0, "stress FX add");
        CHECK(engine_fx_track_set_param(engine, 0, gain, 0, -6.0f), "stress FX param");
        CHECK(engine_track_set_pan(engine, 0, -0.5f), "stress pan");
        EngineEqCurve curve = {0};
        curve.low_cut.enabled = true;
        curve.low_cut.freq_hz = 100.0f;
        CHECK(engine_set_track_eq_curve(engine, 0, &curve), "stress EQ");
        CHECK(engine_insert_track(engine, 0), "stress shift track");
        FxMasterSnapshot effects = {0};
        CHECK(engine_fx_track_snapshot(engine, 1, &effects) && effects.count == 1 && effects.items[0].id == gain,
              "track insertion retargeted effects");
        EngineMeterSnapshot meter;
        if (engine_get_track_meter_snapshot(engine, 1, &meter)) CHECK(isfinite(meter.peak), "invalid meter");
        CHECK(engine_remove_track(engine, 0), "stress remove inserted track");
        CHECK(engine_fx_track_remove(engine, 0, gain), "stress FX remove");
        CHECK(engine_remove_track(engine, 0), "stress track remove");
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
    puts("engine_mix_ownership_test: success (300 live track/FX/EQ/meter edit cycles)");
    return 0;
}
