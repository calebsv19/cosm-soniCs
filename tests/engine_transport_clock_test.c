#define _POSIX_C_SOURCE 200809L
#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include "core_time.h"
#include "test_assert.h"
#include <math.h>
#include <unistd.h>

#define CHECK(v, m) daw_test_expect("engine_transport_clock_test", (v), (m))
static uint64_t fake_ns;

// Supplies deterministic callback/interpolation times before any real worker starts.
static bool test_now(void* context, CoreTimeNs* result) { (void)context; *result = fake_ns; return true; }

// Reads a stable clock, allowing short concurrent callback/worker publications to complete.
static EngineClockSnapshot clock_read(Engine* engine) {
    EngineClockSnapshot result;
    for (int i = 0; i < 2000; ++i) {
        EngineDiagnostics diagnostics;
        CHECK(engine_get_diagnostics(engine, &diagnostics), "live diagnostics read");
        CHECK(diagnostics.pending_commands <= engine->command_queue.capacity / sizeof(EngineCommand),
              "concurrent command occupancy escaped capacity");
        if (engine_get_clock_snapshot(engine, &result)) return result;
        SDL_Delay(1);
    }
    CHECK(false, "clock snapshot never stabilized");
    return (EngineClockSnapshot){0};
}

// Waits only for accepted command execution, not a guessed render delay.
static void wait_transport(Engine* engine) {
    for (int i = 0; i < 2000; ++i) {
        if (!clock_read(engine).pending) return;
        SDL_Delay(1);
    }
    CHECK(false, "transport acknowledgement timeout");
}

// Waits for the producer to fill enough audio before a manually timed callback.
static void wait_audio(Engine* engine, size_t frames) {
    for (int i = 0; i < 2000; ++i) {
        if (clock_read(engine).queued_frames >= frames) return;
        SDL_Delay(1);
    }
    CHECK(false, "audio preparation timeout");
}

// Exercises partial delivery, bounded interpolation, underrun, flush, pause, and inactive audition clocks.
static void test_clock_math(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    Engine* engine = engine_create(&config);
    CHECK(engine && audio_queue_init(&engine->output_queue, 2, 64), "offline fixture");
    fake_ns = 1000000000;
    CHECK(core_time_set_provider((CoreTimeProvider){.now_fn = test_now}), "test clock provider");
    CHECK(engine_transport_seek(engine, 100) && engine_transport_play(engine), "offline seek/play");
    float input[32], output[64];
    for (int i = 0; i < 32; ++i) input[i] = 0.25f;
    CHECK(engine_clock_write(engine, input, 16) == 16, "prepare block");
    EngineClockSnapshot s = clock_read(engine);
    CHECK(s.rendered_frame == 116 && s.consumed_frame == 100 && s.presentation_frame == 100 && s.queued_frames == 16,
          "render-ahead leaked into delivered/display clock");
    engine_audio_callback(output, 4, 2, engine);
    s = clock_read(engine);
    CHECK(s.consumed_frame == 104 && s.presentation_frame == 100 && s.queued_frames == 12 && !s.hardware_position_known,
          "partial callback clock");
    fake_ns += 50000; // 2.4 frames.
    CHECK(clock_read(engine).presentation_frame == 102, "bounded callback interpolation");
    fake_ns += 1000000000;
    CHECK(clock_read(engine).presentation_frame == 104, "presentation ran beyond submitted audio");
    engine_audio_callback(output, 32, 2, engine);
    for (int i = 24; i < 64; ++i) CHECK(output[i] == 0, "underrun not silent");
    s = clock_read(engine);
    CHECK(s.consumed_frame == 116 && s.consumed_frames == 16 && s.queued_frames == 0, "underrun advanced project clock");
    fake_ns += 1000000000;
    engine_audio_callback(output, 32, 2, engine);
    CHECK(clock_read(engine).presentation_frame == 116, "empty callback advanced project clock");
    CHECK(engine_clock_write(engine, input, 16) == 16, "queued pause fixture");
    CHECK(engine_transport_pause(engine), "pause");
    s = clock_read(engine);
    CHECK(!s.playing && s.consumed_frame == 116 && s.queued_frames == 0, "pause skipped render-ahead frames");
    atomic_store(&engine->command_safety_pending, 1u);
    engine_process_commands(engine);
    CHECK(clock_read(engine).consumed_frame == 116, "redundant safety notification turned pause into stop");
    CHECK(engine_clock_write(engine, input, 16) == 16, "paused audition fixture");
    engine_audio_callback(output, 8, 2, engine);
    CHECK(clock_read(engine).consumed_frame == 116, "audition advanced project timeline");
    CHECK(engine_transport_play(engine) && clock_read(engine).queued_frames == 0, "resume retained audition backlog");
    CHECK(engine_transport_seek(engine, 500), "playing seek");
    s = clock_read(engine);
    CHECK(s.playing && s.rendered_frame == 500 && s.consumed_frame == 500 && !s.pending, "seek epoch/state");
    uint64_t epoch = s.epoch;
    CHECK(!engine_transport_set_loop(engine, true, 9, 9) && clock_read(engine).epoch == epoch, "invalid loop mutated state");
    CHECK(engine_transport_stop(engine), "stop");
    CHECK(clock_read(engine).consumed_frame == 0 && !clock_read(engine).playing, "stop did not return to start");
    CHECK(engine_clock_advance(1, 100, 2, 5) == 2 && engine_clock_advance(5, 0, 2, 5) == 2 &&
          engine_clock_advance(UINT64_MAX - 1, 20, 0, 0) == UINT64_MAX, "loop/overflow arithmetic");
    FxInstId delay = engine_fx_master_add(engine, 50);
    CHECK(delay && engine_fx_master_set_param(engine, delay, 0, 1) &&
          engine_fx_master_set_param(engine, delay, 1, 0) && engine_fx_master_set_param(engine, delay, 2, 1), "delay reset fixture");
    CHECK(engine_transport_seek(engine, 0), "reset prepared effect targets");
    EngineMixState* mix = engine_render_mix_state(engine);
    float impulse[128] = {0}, tail[128] = {0};
    impulse[0] = impulse[1] = 1;
    fxm_render_master(mix->fxm, impulse, 24, 2);
    fxm_render_master(mix->fxm, tail, 64, 2);
    bool has_tail = false;
    for (int i = 0; i < 128; ++i) has_tail |= fabsf(tail[i]) > 0.01f;
    CHECK(has_tail, "delay-tail baseline is silent");
    CHECK(engine_transport_seek(engine, 0), "reprime delay");
    memset(impulse, 0, sizeof(impulse)); impulse[0] = impulse[1] = 1;
    fxm_render_master(mix->fxm, impulse, 24, 2);
    CHECK(engine_transport_seek(engine, 1000), "discontinuous seek resets FX");
    memset(tail, 0, sizeof(tail));
    fxm_render_master(engine_render_mix_state(engine)->fxm, tail, 64, 2);
    for (int i = 0; i < 128; ++i) CHECK(tail[i] == 0, "old delay tail survived seek");
    EngineDiagnostics diagnostics;
    CHECK(engine_get_diagnostics(engine, &diagnostics), "diagnostics snapshot");
    CHECK(diagnostics.callback_count == 4 && diagnostics.underrun_callbacks == 2 &&
          diagnostics.underrun_frames == 52, "exact playing underrun counts");
    engine_audio_callback(output, 32, 2, engine);
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.underrun_frames == 52,
          "stopped silence incorrectly reported as underrun");
    CHECK(diagnostics.queue_high_water_frames == 16 && diagnostics.queued_frames == 0,
          "queue diagnostics survive transport epochs");
    engine_diagnostics_render(engine, 1000000, 48);
    engine_diagnostics_render(engine, 1000001, 48);
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.render_blocks == 2 &&
          diagnostics.render_over_budget == 1 && diagnostics.render_max_ns == 1000001,
          "nominal render budget boundary");
    uint64_t budget = (uint64_t)engine->config.block_size * 1000000000ULL / engine->config.sample_rate;
    engine_diagnostics_worker(engine, budget, 1, false);
    engine_diagnostics_worker(engine, budget + 1, budget + 1, true);
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.worker_cycles == 2 &&
          diagnostics.worker_render_cycles == 1 && diagnostics.worker_over_budget == 1 &&
          diagnostics.worker_max_ns == budget + 1 && diagnostics.service_max_ns == budget + 1,
          "complete worker budget and service-only accounting");
    EngineCommand command = {.type = ENGINE_CMD_MIDI_AUDITION_ALL_OFF};
    CHECK(engine_post_command(engine, &command), "timestamped command");
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.pending_commands == 1,
          "pending command observation");
    fake_ns += 7000000;
    engine_process_commands(engine);
    CHECK(engine_get_diagnostics(engine, &diagnostics) && diagnostics.command_last_age_ns == 7000000 &&
          diagnostics.command_max_age_ns >= 7000000 && diagnostics.pending_commands == 0,
          "command age from FIFO admission to dispatch");
    char text[256];
    CHECK(engine_format_diagnostics(engine, text, sizeof(text)) && strstr(text, "gaps 2 (52 frames)") &&
          strstr(text, "slow blocks 1"), "actionable lifetime status");
    core_time_reset_provider();
    engine_destroy(engine);
}

// Calls the real callback with SDL's normal exclusion while its device is paused for deterministic delivery.
static void consume(Engine* engine, float* output, int frames) {
    SDL_LockAudioDevice(engine->device.device_id);
    engine_audio_callback(output, frames, 2, engine);
    SDL_UnlockAudioDevice(engine->device.device_id);
}

// Proves actual worker loop samples and transport barriers with a paused dummy endpoint.
static void test_worker_transitions(int queue_blocks) {
    char path[] = "/tmp/daw-clock-audio-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "media fixture");
    close(fd);
    float audio[16];
    for (int i = 0; i < 8; ++i) audio[i * 2] = audio[i * 2 + 1] = (float)(i + 1) / 16;
    CHECK(wav_write_f32(path, audio, 8, 2, 48000), "write fixture");
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.block_size = 64;
    config.output_queue_blocks = queue_blocks;
    Engine* engine = engine_create(&config);
    CHECK(engine && engine_add_clip_to_track(engine, 0, path, 0, NULL), "worker fixture clip");
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(engine_start(engine), "worker startup");
    audio_device_stop(&engine->device);
    CHECK(engine_transport_set_loop(engine, true, 2, 5) && engine_transport_seek(engine, 1) && engine_transport_play(engine), "loop setup");
    wait_transport(engine);
    size_t target = engine_output_target_frames(64, engine->device.spec.block_size, queue_blocks);
    wait_audio(engine, target);
    EngineDiagnostics queue_diagnostics;
    CHECK(engine_get_diagnostics(engine, &queue_diagnostics) && queue_diagnostics.queue_target_frames == target &&
          queue_diagnostics.queued_frames == target && queue_diagnostics.queue_high_water_frames <= target,
          "effective queue target not enforced");
    EngineClockSnapshot before = clock_read(engine);
    CHECK(before.rendered_frames >= 64 && before.consumed_frame == 1, "render and consumed cursors not independent");
    float output[128];
    consume(engine, output, 11);
    for (int i = 0; i < 11; ++i) {
        uint64_t frame = engine_clock_advance(1, (uint64_t)i, 2, 5);
        CHECK(fabsf(output[i * 2] - audio[frame * 2]) < 0.00001f, "worker loop samples disagree with clock");
    }
    CHECK(clock_read(engine).consumed_frame == engine_clock_advance(1, 11, 2, 5), "loop callback position");
    CHECK(engine_transport_pause(engine), "live pause");
    wait_transport(engine);
    uint64_t paused = clock_read(engine).consumed_frame;
    CHECK(paused == engine_clock_advance(1, 11, 2, 5) && clock_read(engine).queued_frames == 0, "pause skipped ahead");
    consume(engine, output, 11);
    for (int i = 0; i < 22; ++i) CHECK(output[i] == 0, "old audio escaped pause barrier");
    CHECK(engine_transport_play(engine), "resume");
    wait_transport(engine); wait_audio(engine, 64);
    consume(engine, output, 1);
    CHECK(fabsf(output[0] - audio[paused * 2]) < 0.00001f, "resume skipped unconsumed source audio");
    CHECK(engine_transport_set_loop(engine, false, 0, 0) && engine_transport_seek(engine, 6), "disable loop and seek");
    wait_transport(engine); wait_audio(engine, 64);
    consume(engine, output, 1);
    CHECK(fabsf(output[0] - audio[12]) < 0.00001f && clock_read(engine).consumed_frame == 7, "stale loop/seek audio");
    CHECK(engine_transport_stop(engine), "live stop");
    wait_transport(engine);
    consume(engine, output, 32);
    for (int i = 0; i < 64; ++i) CHECK(output[i] == 0, "old audio escaped stop barrier");
    CHECK(clock_read(engine).consumed_frame == 0, "live stop cursor");
    // Rapid accepted operations remain ordered even while callback delivery is active.
    CHECK(audio_device_start(&engine->device), "resume real dummy callback");
    for (int i = 0; i < 100; ++i) {
        CHECK(engine_transport_seek(engine, (uint64_t)(i % 8)), "stress seek");
        CHECK(engine_transport_play(engine), "stress play");
        CHECK(engine_transport_pause(engine), "stress pause");
        wait_transport(engine);
    }
    engine_stop(engine);
    CHECK(engine_get_diagnostics(engine, &queue_diagnostics) && queue_diagnostics.worker_cycles >= queue_diagnostics.worker_render_cycles &&
          queue_diagnostics.worker_render_cycles == queue_diagnostics.render_blocks &&
          queue_diagnostics.worker_max_ns >= queue_diagnostics.render_max_ns, "complete-cycle live coverage");
    CHECK(clock_read(engine).rendered_frames == 0 && clock_read(engine).consumed_frames == 0, "shutdown stale clock");
    engine_destroy(engine);
    CHECK(unlink(path) == 0, "remove fixture");
}

// Separates capture continuity from busy diagnostic clocks while invalidating every transport discontinuity.
static void test_capture_epoch_contract(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "capture context fixture");
    CHECK(!engine_capture_epoch_is_current(engine, 0), "zero epoch accepted");
    for (int operation = 0; operation < 4; ++operation) {
        CHECK(engine_transport_set_loop(engine, false, 0, 0) && engine_transport_play(engine), "linear capture setup");
        EngineClockSnapshot before = clock_read(engine), ignored;
        CHECK(engine_capture_epoch_is_current(engine, before.epoch), "active epoch unavailable");
        atomic_fetch_add(&engine->clock_render_seq, 1);
        atomic_fetch_add(&engine->clock_callback_seq, 1);
        CHECK(!engine_get_clock_snapshot(engine, &ignored), "busy full snapshot accepted");
        CHECK(engine_capture_epoch_is_current(engine, before.epoch), "ordinary publication interrupted capture");
        atomic_fetch_add(&engine->clock_callback_seq, 1);
        atomic_fetch_add(&engine->clock_render_seq, 1);
        if (operation == 0) CHECK(engine_transport_pause(engine), "pause");
        if (operation == 1) CHECK(engine_transport_stop(engine), "stop");
        if (operation == 2) CHECK(engine_transport_seek(engine, 1234), "seek");
        if (operation == 3) CHECK(engine_transport_set_loop(engine, true, 0, 48000), "loop");
        CHECK(!engine_capture_epoch_is_current(engine, before.epoch), "stale capture epoch survived discontinuity");
        EngineClockSnapshot after = clock_read(engine);
        CHECK(engine_capture_epoch_is_current(engine, after.epoch) == (after.playing && !after.loop_enabled),
              "capture epoch disagrees with completed transport state");
    }
    engine_destroy(engine);
}

// Runs deterministic software-clock and real worker/callback-boundary acceptance.
int main(void) {
    test_clock_math();
    test_capture_epoch_contract();
    CHECK(engine_output_target_frames(128, 128, 4) == 512, "four-block target");
    CHECK(engine_output_target_frames(128, 300, 2) == 640, "callback headroom rounding");
    CHECK(engine_output_target_frames(128, 128, 0) == 4096, "legacy config default");
    CHECK(engine_output_target_frames(128, 128, 33) == 4096, "invalid config default");
    CHECK(engine_output_target_frames(0, 128, 4) == 0, "invalid format");
    test_worker_transitions(4);
    test_worker_transitions(8);
    test_worker_transitions(32);
    puts("engine_transport_clock_test: success (partial callbacks, underruns, loop samples, pause/resume, stop/seek barriers, 300 live sequences, 4/8/32-block queue targets, complete worker budgets)");
    return 0;
}
