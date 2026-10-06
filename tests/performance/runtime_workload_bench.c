#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "app/audio_recording.h"
#include "audio/wav_writer.h"
#include "engine/engine_internal.h"
#include "engine/analysis_math.h"
#include "core_time.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

// Carries one explicitly parameterized workload, with no changes to production render behavior.
typedef struct Workload {
    const char* mode;
    int tracks, clips, notes, effects, block, rate, iterations;
    double seconds;
} Workload;

// Reads monotonic elapsed time in milliseconds without wall-clock corrections.
static double now_ms(void) { return (double)core_time_now_ns() / 1000000.0; }

// Reports aggregate user plus system CPU time across this process's threads.
static double cpu_ms(void) {
    struct rusage r;
    assert(getrusage(RUSAGE_SELF, &r) == 0);
    return 1000.0 * (r.ru_utime.tv_sec + r.ru_stime.tv_sec) + .001 * (r.ru_utime.tv_usec + r.ru_stime.tv_usec);
}

// Reports current resident bytes on macOS; unsupported platforms explicitly report zero.
static uint64_t resident_bytes(void) {
#ifdef __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
        return info.resident_size;
#endif
    return 0;
}

// Orders measured durations for empirical nearest-rank percentile reporting.
static int compare_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

// Prints sample counts, processing percentiles, and deadline fractions without a pass/fail speed threshold.
static void timings(double* samples, int count, double budget) {
    double sum = 0;
    int late = 0;
    for (int i = 0; i < count; ++i) { sum += samples[i]; late += samples[i] > budget; }
    qsort(samples, (size_t)count, sizeof(*samples), compare_double);
    printf(",\"samples\":%d,\"mean_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,\"budget_ms\":%.6f,\"over_budget\":%d",
           count, sum / count, samples[(int)ceil(count * .50) - 1], samples[(int)ceil(count * .95) - 1],
           samples[(int)ceil(count * .99) - 1], samples[count - 1], budget, late);
}

// Generates finite deterministic stereo samples outside the measured import/render interval.
static void fixture(const char* path, int rate, int seconds) {
    uint64_t frames = (uint64_t)rate * seconds;
    float* data = malloc((size_t)frames * 2 * sizeof(float));
    assert(data);
    for (uint64_t n = 0; n < frames; ++n) {
        float x = .01f * (float)sin(6.283185307179586 * 440 * n / rate);
        data[n * 2] = x; data[n * 2 + 1] = x * .8f;
    }
    assert(wav_write_f32(path, data, frames, 2, rate));
    free(data);
}

// Constructs projects through public edit APIs so captured plans match ordinary authored content.
static Engine* project(const Workload* w, const char* path, FxInstId* scope) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = w->rate; cfg.block_size = w->block;
    const char* queue_blocks = getenv("DAW_BENCH_QUEUE_BLOCKS");
    if (queue_blocks) cfg.output_queue_blocks = atoi(queue_blocks);
    Engine* e = engine_create(&cfg);
    assert(e);
    for (int t = 0; t < w->tracks; ++t) {
        if (t) assert(engine_add_track(e) == t);
        assert(engine_track_set_gain(e, t, 1.0f / w->tracks));
        for (int c = 0; c < w->clips; ++c) {
            int index = -1;
            uint64_t start = strstr(w->mode, "overlap") ? 0 : (uint64_t)c * w->rate * 16;
            if (w->notes) {
                assert(engine_add_midi_clip_to_track(e, t, start, (uint64_t)w->rate * 3600, &index));
                EngineInstrumentParams params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
                params.level = .1f;
                assert(engine_clip_midi_set_instrument_params(e, t, index, params));
                for (int n = 0; n < w->notes; ++n) {
                    bool sparse = strstr(w->mode, "sparse") != NULL;
                    uint64_t note_start = sparse && n ? (uint64_t)(n + 8) * w->rate : 0;
                    EngineMidiNote note = {note_start, (uint64_t)w->rate * 8, (uint8_t)(48 + n % 24), .5f};
                    assert(engine_clip_midi_add_note(e, t, index, note, NULL));
                }
            } else assert(engine_add_clip_to_track(e, t, path, start, &index));
        }
        if (w->effects) {
            assert(engine_fx_track_add(e, t, 1)); // Gain.
            assert(engine_fx_track_add(e, t, 20)); // Compressor.
            assert(engine_fx_track_add(e, t, 50)); // Delay.
            assert(engine_fx_track_add(e, t, 21)); // Limiter.
        }
    }
    if (strstr(w->mode, "analysis")) {
        *scope = engine_fx_track_add(e, 0, 105);
        assert(*scope);
        engine_set_spectrum_target(e, ENGINE_SPECTRUM_VIEW_MASTER, 0, true);
        engine_set_fx_spectrogram_target(e, 0, *scope, true);
    }
    return e;
}

// Measures the actual live mixer without device scheduling, separately from fixture preparation.
static void render_bench(Engine* e, const Workload* w) {
    float* out = calloc((size_t)w->block * 2, sizeof(float));
    float* scratch = calloc((size_t)w->block * 2, sizeof(float));
    double* times = calloc((size_t)w->iterations, sizeof(double));
    assert(out && scratch && times);
    double checksum = 0;
    for (int i = -64; i < w->iterations; ++i) {
        uint64_t frame = (uint64_t)(i + 64) * w->block % ((uint64_t)w->rate * 4);
        double began = now_ms();
        engine_mix_tracks(e, frame, w->block, out, scratch, 2);
        double elapsed = now_ms() - began;
        if (i >= 0) times[i] = elapsed;
        for (int n = 0; n < w->block * 2; ++n) { assert(isfinite(out[n])); checksum += fabs(out[n]); }
    }
    assert(checksum > 0);
    timings(times, w->iterations, 1000.0 * w->block / w->rate);
    // Public scalar edits currently capture whole plans; measure that cost separately.
    double edits[20];
    for (int i = 0; i < 20; ++i) {
        double began = now_ms();
        assert(engine_track_set_gain(e, 0, (.8f + .01f * i) / w->tracks));
        edits[i] = now_ms() - began;
    }
    qsort(edits, 20, sizeof(double), compare_double);
    printf(",\"edit_p50_ms\":%.6f,\"edit_max_ms\":%.6f,\"checksum\":%.6f", edits[9], edits[19], checksum);
    free(times); free(scratch); free(out);
}

// Records sampled process residency during streaming without allocating per audio chunk.
typedef struct StreamMemoryProbe {
    uint64_t peak, next;
} StreamMemoryProbe;

// Samples once per 48000 work frames while preserving cancellation-free benchmark execution.
static bool stream_memory_progress(uint64_t done, uint64_t total, void* user) {
    StreamMemoryProbe* probe = user;
    if (done >= probe->next || done == total) {
        uint64_t rss = resident_bytes();
        if (rss > probe->peak) probe->peak = rss;
        probe->next = done + 48000;
    }
    return true;
}

// Measures synchronous render and publication memory/cost independently of fixture disk writes.
static void export_bench(Engine* e, const Workload* w, const char* path) {
    uint64_t before = resident_bytes();
    if (strstr(w->mode, "stream")) {
        StreamMemoryProbe probe = {.peak = before};
        EngineBounceStreamCallbacks callbacks = {.progress = stream_memory_progress, .user = &probe};
        double began = now_ms();
        assert(engine_bounce_range_to_wav(e, 0, (uint64_t)(w->seconds * w->rate), NULL, path,
                                          ENGINE_BOUNCE_WAV_PCM16, &callbacks) == DAW_SAVE_SYNCED);
        double elapsed = now_ms() - began;
        printf(",\"export_ms\":%.6f,\"realtime_factor\":%.6f,\"render_buffer_bytes\":%llu,\"output_bytes\":%llu,\"rss_before_export\":%llu,\"rss_sampled_peak_export\":%llu",
               elapsed, w->seconds * 1000 / elapsed, (unsigned long long)(2 * w->block * 2 * sizeof(float)),
               (unsigned long long)(w->seconds * w->rate * 2 * 2 + 44),
               (unsigned long long)before, (unsigned long long)probe.peak);
        return;
    }
    EngineBounceBuffer b = {0};
    double began = now_ms();
    assert(engine_bounce_range_to_buffer(e, 0, (uint64_t)(w->seconds * w->rate), NULL, NULL, &b));
    double render = now_ms() - began;
    uint64_t held = resident_bytes();
    began = now_ms();
    assert(engine_bounce_write_wav(&b, path, ENGINE_BOUNCE_WAV_PCM16));
    printf(",\"export_ms\":%.6f,\"write_ms\":%.6f,\"realtime_factor\":%.6f,\"output_bytes\":%llu,\"rss_before_export\":%llu,\"rss_holding_export\":%llu",
           render, now_ms() - began, w->seconds * 1000 / render,
           (unsigned long long)(b.frame_count * b.channels * sizeof(float)), (unsigned long long)before, (unsigned long long)held);
    engine_bounce_buffer_free(&b);
}

// Samples real worker/dummy-callback deltas after startup, optionally adding analysis, edits, or export.
static void live_bench(Engine* e, const Workload* w, const char* path) {
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    assert(engine_transport_set_loop(e, true, 0, (uint64_t)w->rate * 4));
    assert(engine_start(e));
    assert(engine_transport_play(e));
    SDL_Delay(600);
    EngineDiagnostics before, after;
    assert(engine_get_diagnostics(e, &before));
    uint64_t sq = atomic_load(&e->spectrum_stream.queued), sp = atomic_load(&e->spectrum_stream.published);
    uint64_t sd = atomic_load(&e->spectrum_stream.dropped), gp = atomic_load(&e->spectrogram_stream.published);
    uint64_t gd = atomic_load(&e->spectrogram_stream.dropped);
    double cpu = cpu_ms(), begin = now_ms(), edit_max = 0;
    if (strstr(w->mode, "export")) export_bench(e, w, path);
    int edits = 0;
    while (now_ms() - begin < 4000) {
        if (strstr(w->mode, "edits")) {
            double t = now_ms();
            bool mixed = strstr(w->mode, "mixed") != NULL;
            assert(engine_track_set_gain(e, 0, (mixed && edits % 2 ? .7f : .9f) / w->tracks));
            if (mixed) {
                assert(engine_track_set_pan(e, 0, edits % 2 ? -.2f : .2f));
                if (edits % 20 == 0) assert(engine_insert_track(e, engine_get_track_count(e)));
                if (edits % 20 == 10) assert(engine_remove_track(e, engine_get_track_count(e) - 1));
            }
            ++edits;
            if (now_ms() - t > edit_max) edit_max = now_ms() - t;
            engine_source_plan_collect(e);
        }
        SDL_Delay(20);
    }
    double elapsed = now_ms() - begin;
    assert(engine_get_diagnostics(e, &after));
    printf(",\"live_wall_ms\":%.6f,\"live_cpu_ms\":%.6f,\"callback_delta\":%llu,\"underrun_callback_delta\":%llu,\"underrun_frame_delta\":%llu,\"render_block_delta\":%llu,\"render_over_budget_delta\":%llu,\"lifetime_render_max_ms\":%.6f,\"lifetime_command_max_ms\":%.6f,\"queue_high_water_frames\":%llu,\"callback_frames\":%d,\"spectrum_queued_delta\":%llu,\"spectrum_published_delta\":%llu,\"spectrum_dropped_delta\":%llu,\"spectrogram_published_delta\":%llu,\"spectrogram_dropped_delta\":%llu,\"live_edit_max_ms\":%.6f",
        elapsed, cpu_ms() - cpu, (unsigned long long)(after.callback_count - before.callback_count),
        (unsigned long long)(after.underrun_callbacks - before.underrun_callbacks),
        (unsigned long long)(after.underrun_frames - before.underrun_frames),
        (unsigned long long)(after.render_blocks - before.render_blocks),
        (unsigned long long)(after.render_over_budget - before.render_over_budget), after.render_max_ns / 1e6,
        after.command_max_age_ns / 1e6, (unsigned long long)after.queue_high_water_frames, e->device.spec.block_size,
        (unsigned long long)(atomic_load(&e->spectrum_stream.queued) - sq),
        (unsigned long long)(atomic_load(&e->spectrum_stream.published) - sp),
        (unsigned long long)(atomic_load(&e->spectrum_stream.dropped) - sd),
        (unsigned long long)(atomic_load(&e->spectrogram_stream.published) - gp),
        (unsigned long long)(atomic_load(&e->spectrogram_stream.dropped) - gd), edit_max);
    printf(",\"worker_cycle_delta\":%llu,\"worker_render_cycle_delta\":%llu,\"worker_over_budget_delta\":%llu,\"lifetime_worker_max_ms\":%.6f,\"lifetime_service_max_ms\":%.6f,\"queue_target_frames\":%llu",
        (unsigned long long)(after.worker_cycles - before.worker_cycles),
        (unsigned long long)(after.worker_render_cycles - before.worker_render_cycles),
        (unsigned long long)(after.worker_over_budget - before.worker_over_budget),
        after.worker_max_ns / 1e6, after.service_max_ns / 1e6, (unsigned long long)after.queue_target_frames);
    for (int spectrogram = 0; spectrogram < 2; ++spectrogram) {
        EngineAnalysisDiagnostics diagnostics;
        assert(engine_get_analysis_diagnostics(e, spectrogram, &diagnostics));
        const char* name = spectrogram ? "spectrogram" : "spectrum";
        printf(",\"%s_pending\":%llu,\"%s_backlog_high_water\":%llu,\"%s_stale\":%llu,"
               "\"%s_computed\":%llu,\"%s_compute_total_ms\":%.6f,\"%s_compute_max_ms\":%.6f",
               name, (unsigned long long)diagnostics.pending, name, (unsigned long long)diagnostics.backlog_high_water,
               name, (unsigned long long)diagnostics.stale, name, (unsigned long long)diagnostics.computed,
               name, diagnostics.compute_total_ms, name, diagnostics.compute_max_ms);
    }
    engine_stop(e);
}

// Measures production capture queue/drain/journal paths with synthetic input and explicit drain cadence.
static void recording_bench(Engine* e, const Workload* w, const char* root) {
    AppState* app = calloc(1, sizeof(*app));
    assert(app); app->engine = e; app->runtime_cfg = *engine_get_config(e);
    snprintf(app->data_paths.library_copy_root, sizeof(app->data_paths.library_copy_root), "%s", root);
    daw_audio_recording_init(&app->audio_recording);
    AudioDeviceSpec spec = {.sample_rate = w->rate, .channels = 2, .block_size = w->block};
    assert(daw_audio_recording_begin_take(app, 0, 0, &spec));
    const int frames = w->rate / 10, count = (int)(w->seconds * 10);
    float* samples = calloc((size_t)frames * 2, sizeof(float));
    double* times = calloc((size_t)count, sizeof(double));
    assert(samples && times);
    for (int i = 0; i < frames * 2; ++i) samples[i] = .01f;
    bool live = !strcmp(w->mode, "live_record");
    EngineDiagnostics d0 = {0}, d1 = {0};
    if (live) {
        assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
        assert(engine_transport_set_loop(e, true, 0, (uint64_t)w->rate * 4));
        assert(engine_start(e)); assert(engine_transport_play(e)); SDL_Delay(600);
        assert(engine_get_diagnostics(e, &d0));
    }
    uint64_t before = resident_bytes();
    double begin = now_ms();
    for (int i = 0; i < count; ++i) {
        if (live) SDL_Delay(100);
        assert(daw_audio_recording_enqueue_frames(&app->audio_recording, samples, frames, 2) == (size_t)frames);
        double t = now_ms();
        assert(daw_audio_recording_drain(&app->audio_recording) == (uint64_t)frames);
        times[i] = now_ms() - t;
    }
    DawAudioRecordingState* r = &app->audio_recording;
    assert(!r->drain_failed && !atomic_load(&r->dropped_frames));
    timings(times, count, 100);
    printf(",\"recording_wall_ms\":%.6f,\"recorded_frames\":%llu,\"take_capacity_bytes\":%llu,\"take_used_bytes\":%llu,\"rss_before_recording\":%llu,\"rss_holding_recording\":%llu,\"checkpoint_frames\":%llu",
        now_ms() - begin, (unsigned long long)r->take_frame_count,
        (unsigned long long)(r->take_frame_capacity * r->channels * sizeof(float)),
        (unsigned long long)((r->take_frame_count < r->take_frame_capacity ? r->take_frame_count : r->take_frame_capacity) * r->channels * sizeof(float)),
        (unsigned long long)before, (unsigned long long)resident_bytes(), (unsigned long long)r->checkpoint_frames);
    if (live) {
        assert(engine_get_diagnostics(e, &d1));
        printf(",\"callback_delta\":%llu,\"underrun_frame_delta\":%llu,\"render_over_budget_delta\":%llu",
            (unsigned long long)(d1.callback_count - d0.callback_count),
            (unsigned long long)(d1.underrun_frames - d0.underrun_frames),
            (unsigned long long)(d1.render_over_budget - d0.render_over_budget));
        printf(",\"worker_over_budget_delta\":%llu,\"lifetime_worker_max_ms\":%.6f,\"lifetime_service_max_ms\":%.6f,\"queue_target_frames\":%llu,\"queue_high_water_frames\":%llu",
            (unsigned long long)(d1.worker_over_budget - d0.worker_over_budget), d1.worker_max_ns / 1e6,
            d1.service_max_ns / 1e6, (unsigned long long)d1.queue_target_frames, (unsigned long long)d1.queue_high_water_frames);
        engine_stop(e);
    }
    daw_audio_recording_free(r); free(samples); free(times); free(app);
}

// Exercises the real timeline capture worker while UI polling pauses for one second at a time.
static void recording_worker_bench(Engine* engine, const Workload* workload, const char* root) {
    AppState* app = calloc(1, sizeof(*app));
    assert(app);
    app->engine = engine;
    app->runtime_cfg = *engine_get_config(engine);
    snprintf(app->data_paths.library_copy_root, sizeof(app->data_paths.library_copy_root), "%s", root);
    daw_audio_recording_init(&app->audio_recording);
    undo_manager_init(&app->undo);
    char registry_path[1024];
    snprintf(registry_path, sizeof(registry_path), "%s/registry.json", root);
    media_registry_init(&app->media_registry, registry_path);
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    assert(engine_start(engine) && engine_transport_play(engine));
    SDL_Delay(600);
    EngineDiagnostics before, after;
    assert(engine_get_diagnostics(engine, &before));
    assert(daw_audio_recording_begin_timeline_capture(app));
    uint64_t rss = resident_bytes();
    double max_poll = 0;
    for (int i = 0; i < (int)workload->seconds; ++i) {
        SDL_Delay(1000);
        double began = now_ms();
        daw_audio_recording_drain_if_transport_playing(app);
        double elapsed = now_ms() - began;
        if (elapsed > max_poll) max_poll = elapsed;
    }
    DawAudioRecordingState* recording = &app->audio_recording;
    assert(recording->worker && !recording->drain_failed && recording->checkpoint_frames > 0);
    audio_capture_device_stop(&recording->capture_device);
    recording->capture_device_started = false;
    uint64_t missing = atomic_load(&recording->dropped_frames);
    uint64_t queue_missing = atomic_load(&recording->queue_dropped_frames);
    uint64_t clock_missing = atomic_load(&recording->clock_missing_frames);
    uint64_t captured = atomic_load(&recording->captured_frames);
    assert(missing == 0 && queue_missing == 0 && clock_missing == 0);
    uint64_t continuity = atomic_load(&recording->clock_continuity_frames);
    assert(engine_get_diagnostics(engine, &after));
    printf(",\"recorded_frames\":%llu,\"checkpoint_frames\":%llu,\"ui_poll_max_ms\":%.6f,\"preview_buffers_bytes\":%llu,\"rss_before_recording\":%llu,\"rss_holding_recording\":%llu,\"underrun_frames_delta\":%llu,\"worker_over_budget_delta\":%llu",
           (unsigned long long)recording->take_frame_count, (unsigned long long)recording->checkpoint_frames,
           max_poll, (unsigned long long)(3 * recording->take_frame_capacity * recording->channels * sizeof(float)),
           (unsigned long long)rss, (unsigned long long)resident_bytes(),
           (unsigned long long)(after.underrun_frames - before.underrun_frames),
           (unsigned long long)(after.worker_over_budget - before.worker_over_budget));
    DawAudioRecordingResult result;
    assert(daw_audio_recording_finish(app, &result));
    assert(result.frame_count == captured);
    printf(",\"capture_missing_frames\":%llu,\"capture_queue_dropped_frames\":%llu,\"capture_clock_missing_frames\":%llu,\"finalized_frames\":%llu,\"captured_frames\":%llu",
           (unsigned long long)missing, (unsigned long long)queue_missing, (unsigned long long)clock_missing,
           (unsigned long long)result.frame_count, (unsigned long long)captured);
    printf(",\"capture_clock_continuity_frames\":%llu", (unsigned long long)continuity);
    undo_manager_free(&app->undo);
    media_registry_shutdown(&app->media_registry);
    engine_stop(engine);
    free(app);
}

// Measures the existing calibrated analyzer kernel at its real window/grid sizes.
static void analysis_bench(const Workload* w) {
    float samples[2048], bins[256];
    EngineAnalysisPlan plan;
    assert(engine_analysis_prepare(&plan, 2048, 256, w->rate, 20, 20000));
    for (int i = 0; i < 2048; ++i) samples[i] = .25f * (float)sin(6.283185307179586 * 440 * i / w->rate);
    double times[20], checksum = 0;
    for (int k = -2; k < 20; ++k) {
        double began = now_ms();
        engine_analysis_compute(&plan, samples, bins);
        double elapsed = now_ms() - began;
        for (int b = 0; b < 256; ++b) checksum += bins[b];
        if (k >= 0) times[k] = elapsed;
    }
    timings(times, 20, 2048.0 * 1000 / w->rate);
    printf(",\"checksum\":%.6f", checksum);
}

// Runs one bounded workload per process so RSS and timing evidence do not mix different fixtures.
int main(int argc, char** argv) {
    if (argc != 10) { fprintf(stderr, "mode tracks clips notes effects block rate seconds iterations\n"); return 2; }
    Workload w = {argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), atoi(argv[9]), atof(argv[8])};
    assert(w.tracks >= 1 && w.tracks <= 64 && w.clips >= 0 && w.clips <= 64 && w.notes >= 0 && w.notes <= 2048);
    assert(w.block >= 64 && w.block <= 1024 && w.rate >= 44100 && w.rate <= 96000 && w.iterations > 0 && w.iterations <= 10000 && w.seconds >= 1 && w.seconds <= 120);
    char root[] = "/tmp/daw-s4-workload-XXXXXX";
    assert(mkdtemp(root));
    char path[512], output[512];
    snprintf(path, sizeof(path), "%s/input.wav", root);
    snprintf(output, sizeof(output), "%s/output.wav", root);
    bool importing = !strcmp(w.mode, "import");
    fixture(path, importing ? w.block == 64 ? 44100 : w.rate : w.rate, importing ? (int)w.seconds : 8);
    printf("{\"mode\":\"%s\",\"tracks\":%d,\"clips_per_track\":%d,\"notes_per_clip\":%d,\"fx_chain\":%d,\"block\":%d,\"rate\":%d,\"seconds\":%.1f", w.mode, w.tracks, w.clips, w.notes, w.effects, w.block, w.rate, w.seconds);
    double began = now_ms(), cpu = cpu_ms();
    if (importing) {
        AudioMediaClip clip = {0};
        uint64_t before = resident_bytes();
        assert(audio_media_clip_load(path, w.rate, &clip));
        printf(",\"import_ms\":%.6f,\"decoded_bytes\":%llu,\"rss_before_import\":%llu,\"rss_holding_import\":%llu,\"source_rate\":%d",
            now_ms() - began, (unsigned long long)(clip.frame_count * clip.channels * sizeof(float)),
            (unsigned long long)before, (unsigned long long)resident_bytes(), w.block == 64 ? 44100 : w.rate);
        audio_media_clip_free(&clip);
    } else if (!strcmp(w.mode, "analysis")) analysis_bench(&w);
    else {
        FxInstId scope = 0;
        Engine* e = project(&w, path, &scope);
        printf(",\"setup_ms\":%.6f,\"rss_after_setup\":%llu", now_ms() - began, (unsigned long long)resident_bytes());
        if (!strcmp(w.mode, "live_record_worker")) recording_worker_bench(e, &w, root);
        else if (!strcmp(w.mode, "live_record")) recording_bench(e, &w, root);
        else if (!strncmp(w.mode, "live", 4)) live_bench(e, &w, output);
        else if (!strncmp(w.mode, "export", 6)) export_bench(e, &w, output);
        else if (!strcmp(w.mode, "record")) recording_bench(e, &w, root);
        else render_bench(e, &w);
        engine_destroy(e);
    }
    struct rusage r;
    assert(getrusage(RUSAGE_SELF, &r) == 0);
    printf(",\"process_wall_ms_excluding_fixture\":%.6f,\"process_cpu_ms_excluding_fixture\":%.6f,\"peak_rss_platform_units\":%ld}\n", now_ms() - began, cpu_ms() - cpu, r.ru_maxrss);
    unlink(path); unlink(output);
    // Recording journals stay in this unique directory for the runner's scoped cleanup.
    fprintf(stderr, "workload_temp_root=%s\n", root);
    rmdir(root);
    return 0;
}
