#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "test_assert.h"
#include "app_state.h"
#include "ui/effects_panel_meter_detail.h"
#include <math.h>

#define CHECK(value, message) daw_test_expect("engine_analysis_lifecycle_test", (value), (message))
static int fail_thread, thread_calls;
static bool fail_open, fail_start, fail_priority;

// Simulates platforms refusing priority elevation without preventing ordinary rendering.
static int lifecycle_set_priority(SDL_ThreadPriority priority) {
    return fail_priority ? -1 : SDL_SetThreadPriority(priority);
}

// Injects deterministic failures at each thread-creation stage while preserving normal SDL threads.
static SDL_Thread* lifecycle_create_thread(SDL_ThreadFunction fn, const char* name, void* userdata) {
    if (++thread_calls == fail_thread) return NULL;
    return SDL_CreateThread(fn, name, userdata);
}
// Injects endpoint opening failure without touching physical audio devices.
static bool lifecycle_device_open(AudioDevice* device, const AudioDeviceSpec* spec, AudioDeviceCallback cb, void* user) {
    return !fail_open && audio_device_open(device, spec, cb, user);
}
// Injects failure after all workers exist to verify complete startup rollback.
static bool lifecycle_device_start(AudioDevice* device) { return !fail_start && audio_device_start(device); }

#define SDL_SetThreadPriority lifecycle_set_priority
#define SDL_CreateThread lifecycle_create_thread
#define audio_device_open lifecycle_device_open
#define audio_device_start lifecycle_device_start
#include "../src/engine/engine_core.c"
#undef SDL_SetThreadPriority
#undef SDL_CreateThread
#undef audio_device_open
#undef audio_device_start

// Checks packet alignment, backpressure, target changes, and contiguous source samples without threads.
static void test_windows(void) {
    EngineAnalysisStream stream;
    RingBuffer queue = {0};
    engine_analysis_init(&stream);
    CHECK(ringbuf_init(&queue, sizeof(EngineAnalysisPacket)), "analysis queue init");
    CHECK(engine_analysis_select(&stream, 17, 1, 105, true), "select analysis source");
    float block[128];
    for (int b = 0; b < 32; ++b) {
        CHECK(engine_analysis_begin(&stream), "begin analysis block");
        for (int i = 0; i < 128; ++i) block[i] = (float)(b * 128 + i);
        engine_analysis_append(&stream, &queue, block, 128, 1, ENGINE_SPECTRUM_FFT_SIZE);
    }
    CHECK(atomic_load(&stream.queued) == 1 && atomic_load(&stream.dropped) == 1, "whole-window overflow policy");
    EngineAnalysisPacket packet;
    CHECK(engine_analysis_receive(&stream, &queue, &packet), "whole analysis packet missing");
    for (int i = 0; i < ENGINE_SPECTRUM_FFT_SIZE; ++i) CHECK(packet.samples[i] == (float)i, "noncontiguous analysis window");
    CHECK(engine_analysis_current(&stream, &packet), "current packet rejected");
    CHECK(packet.sequence == 0 && packet.first_sample == 0, "initial window stamp");
    engine_analysis_append(&stream, &queue, block, 128, 1, ENGINE_SPECTRUM_FFT_SIZE);
    CHECK(engine_analysis_select(&stream, 18, 1, 105, true), "change analysis source");
    CHECK(!engine_analysis_current(&stream, &packet), "stale packet accepted");
    for (int b = 0; b < 16; ++b) {
        CHECK(engine_analysis_begin(&stream), "begin new source");
        for (int i = 0; i < 128; ++i) block[i] = -1.0f;
        engine_analysis_append(&stream, &queue, block, 128, 1, ENGINE_SPECTRUM_FFT_SIZE);
    }
    CHECK(engine_analysis_receive(&stream, &queue, &packet), "replacement source packet missing");
    for (int i = 0; i < ENGINE_SPECTRUM_FFT_SIZE; ++i) CHECK(packet.samples[i] == -1.0f, "mixed source generations");
    CHECK(packet.sequence == 2 && packet.first_sample == 0, "dropped window or target reset lost its stamp");
    engine_analysis_invalidate(&stream);
    CHECK(!engine_analysis_current(&stream, &packet), "clear failed to invalidate packet");
    CHECK(ringbuf_available_read(&queue) == 0, "partial packet residue");
    CHECK(atomic_load(&stream.consumed) == 2 && atomic_load(&stream.backlog_high_water) == 1,
          "bounded queue consumer diagnostics");
    ringbuf_free(&queue);
}

// Verifies failure leaves no live worker, endpoint, or preallocated render buffer behind.
static void expect_stopped(Engine* engine) {
    CHECK(!engine_is_running(engine) && !engine->worker_thread && !engine->spectrum_thread &&
          !engine->spectrogram_thread, "worker survived rollback");
    CHECK(!atomic_load(&engine->worker_running) && !atomic_load(&engine->spectrum_running) &&
          !atomic_load(&engine->spectrogram_running), "running flag survived rollback");
    CHECK(!engine->render_buffer && !engine->render_track_buffer, "render buffer survived stop");
    EngineDiagnostics diagnostics;
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.worker_priority_status == 0,
          "stopped worker retained priority status");
}

// Waits for fresh publications from both real analyzer threads with a bounded timeout.
static void wait_analysis(Engine* engine, uint64_t spectrum_before, uint64_t spectrogram_before) {
    uint64_t deadline = SDL_GetTicks64() + 5000;
    while (SDL_GetTicks64() < deadline &&
           (atomic_load(&engine->spectrum_stream.published) <= spectrum_before ||
            atomic_load(&engine->spectrogram_stream.published) <= spectrogram_before)) SDL_Delay(2);
    if (atomic_load(&engine->spectrum_stream.published) <= spectrum_before ||
        atomic_load(&engine->spectrogram_stream.published) <= spectrogram_before) {
        fprintf(stderr, "analysis timeout: frame=%llu playing=%d queued=%llu/%llu published=%llu/%llu audio=%zu worker=%d\n",
                (unsigned long long)engine_get_transport_frame(engine), engine_transport_is_playing(engine),
                (unsigned long long)atomic_load(&engine->spectrum_stream.queued),
                (unsigned long long)atomic_load(&engine->spectrogram_stream.queued),
                (unsigned long long)atomic_load(&engine->spectrum_stream.published),
                (unsigned long long)atomic_load(&engine->spectrogram_stream.published),
                engine_get_queued_frames(engine), atomic_load(&engine->worker_running));
    }
    CHECK(atomic_load(&engine->spectrum_stream.published) > spectrum_before, "spectrum did not publish");
    CHECK(atomic_load(&engine->spectrogram_stream.published) > spectrogram_before, "spectrogram did not publish");
}

// Exercises real callback/mixer/analyzer threads through every startup failure and repeated live edits.
int main(void) {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0, "dummy audio selection");
    test_windows();
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000; cfg.block_size = 128;
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "engine create");
    int clip;
    CHECK(engine_add_midi_clip_to_track(engine, 0, 0, 480000, &clip), "MIDI fixture");
    CHECK(engine_clip_midi_add_note(engine, 0, clip, (EngineMidiNote){0, 480000, 60, 0.25f}, NULL), "MIDI note");
    FxInstId meter = engine_fx_track_add(engine, 0, 105);
    CHECK(meter != 0, "spectrogram meter");
    AppState* ui = calloc(1, sizeof(*ui));
    CHECK(ui != NULL, "rack UI fixture");
    ui->engine = engine;
    ui->effects_panel.target = FX_PANEL_TARGET_TRACK;
    ui->effects_panel.target_track_index = 0;
    ui->effects_panel.chain_count = 1;
    ui->effects_panel.chain[0] = (FxSlotUIState){.id = meter, .type_id = 105u, .enabled = true};
    ui->effects_panel.selected_slot_index = -1;
    CHECK(effects_panel_spectrogram_card_index(ui) == 0, "rack must choose visible meter without list detail");
    for (int failure = 0; failure < 5; ++failure) {
        fail_open = failure == 0;
        fail_thread = failure >= 1 && failure <= 3 ? failure : 0;
        fail_start = failure == 4;
        thread_calls = 0;
        CHECK(!engine_start(engine), "injected startup failure was ignored");
        expect_stopped(engine);
        CHECK(!engine->device.is_open, "failed start left endpoint open");
        FxMasterSnapshot effects;
        CHECK(engine_fx_track_snapshot(engine, 0, &effects) && effects.count == 1 && effects.items[0].id == meter,
              "failed startup changed project effects");
    }
    fail_open = fail_start = false; fail_thread = 0;
    for (int cycle = 0; cycle < 8; ++cycle) {
        fail_priority = (cycle % 2) == 0;
        engine_set_spectrum_target(engine, ENGINE_SPECTRUM_VIEW_TRACK, 0, true);
        effects_panel_update_spectrogram_card_target(ui);
        uint64_t spectrum_before = atomic_load(&engine->spectrum_stream.published);
        uint64_t spectrogram_before = atomic_load(&engine->spectrogram_stream.published);
        CHECK(engine_start(engine), "restart failed");
        SDL_Thread* worker = engine->worker_thread;
        CHECK(engine_start(engine) && worker == engine->worker_thread, "repeated start duplicated worker");
        CHECK(engine_transport_play(engine), "play command");
        wait_analysis(engine, spectrum_before, spectrogram_before);
        EngineSpectrogramSnapshot card_snapshot;
        float card_frames[ENGINE_SPECTROGRAM_HISTORY * ENGINE_SPECTROGRAM_BINS];
        CHECK(engine_get_fx_spectrogram_snapshot(engine, &card_snapshot, card_frames,
              ENGINE_SPECTROGRAM_HISTORY, ENGINE_SPECTROGRAM_BINS) && card_snapshot.frames > 0 &&
              card_snapshot.effect_id == meter && card_snapshot.track_id == engine->tracks[0].runtime_id,
              "rack subscription did not publish its original track and effect");
        ui->effects_panel.chain[0].enabled = false;
        effects_panel_update_spectrogram_card_target(ui);
        CHECK(!engine_get_fx_spectrogram_snapshot(engine, &card_snapshot, card_frames,
              ENGINE_SPECTROGRAM_HISTORY, ENGINE_SPECTROGRAM_BINS) && card_snapshot.frames == 0,
              "bypassed rack meter retained stale history");
        ui->effects_panel.chain[0].enabled = true;
        effects_panel_update_spectrogram_card_target(ui);
        EngineDiagnostics diagnostics;
        CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.worker_priority_status != 0,
              "running worker did not report priority request");
        if (fail_priority) CHECK(diagnostics.worker_priority_status == -1, "priority refusal was hidden");
        for (int edit = 0; edit < 4; ++edit) {
            CHECK(engine_insert_track(engine, 0), "live insert with analyzers");
            engine_set_spectrum_target(engine, ENGINE_SPECTRUM_VIEW_TRACK, 1, true);
            engine_set_fx_spectrogram_target(engine, 1, meter, true);
            engine_spectrogram_clear_history(engine);
            CHECK(engine_remove_track(engine, 0), "live removal with analyzers");
            engine_set_spectrum_target(engine, ENGINE_SPECTRUM_VIEW_TRACK, 0, true);
            engine_set_fx_spectrogram_target(engine, 0, meter, true);
        }
        engine_stop(engine);
        engine_stop(engine);
        expect_stopped(engine);
    }
    free(ui);
    engine_destroy(engine);
    SDL_Quit();
    puts("engine_analysis_lifecycle_test: success (5 failure stages, 8 restarts, live analyzer edits)");
    return 0;
}
