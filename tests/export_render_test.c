#define _POSIX_C_SOURCE 200809L
#include "audio/wav_writer.h"
#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Thread_local int fail_after = -1;
// Injects allocation failure only into the calling control thread's prepared export path.
static bool fail_allocation(void) {
    if (fail_after < 0)
        return false;
    if (!fail_after)
        return true;
    --fail_after;
    return false;
}
// Wraps allocations in instrumented production export/source/DSP objects.
void* daw_test_malloc(size_t n) { return fail_allocation() ? NULL : malloc(n); }
// Wraps zeroed prepared storage for complete failure-boundary coverage.
void* daw_test_calloc(size_t n, size_t size) { return fail_allocation() ? NULL : calloc(n, size); }
// Wraps capacity growth without disturbing original storage on failure.
void* daw_test_realloc(void* p, size_t n) { return fail_allocation() ? NULL : realloc(p, n); }
// Preserves real release behavior so sanitizers can detect incomplete rollback.
void daw_test_free(void* p) { free(p); }

static _Thread_local int io_fault;
// Rejects spool creation before any output candidate exists.
FILE* daw_test_tmpfile(void) { return io_fault == 1 ? NULL : tmpfile(); }
// Models a short spool write while allowing normal fixture and WAV writes.
size_t daw_test_fwrite(const void* data, size_t size, size_t count, FILE* file) {
    return fwrite(data, size, io_fault == 2 ? count / 2 : count, file);
}
// Models deferred spool flush failure before the second pass.
int daw_test_fflush(FILE* file) { return io_fault == 3 ? EOF : fflush(file); }
// Models inability to rewind the completed spool.
int daw_test_fseek(FILE* file, long offset, int origin) { return io_fault == 4 ? -1 : fseek(file, offset, origin); }
// Models a short read after the WAV candidate has been opened.
size_t daw_test_fread(void* data, size_t size, size_t count, FILE* file) {
    return fread(data, size, io_fault == 5 ? count / 2 : count, file);
}
// Closes the actual spool while reporting a delayed error before WAV publication.
int daw_test_fclose(FILE* file) { int result = fclose(file); return io_fault == 6 ? EOF : result; }

// Creates a small stereo engine without opening an output device.
static Engine* create_engine(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 64;
    Engine* engine = engine_create(&config);
    assert(engine);
    return engine;
}
// Installs the production one-millisecond fully wet delay with no feedback.
static void add_delay(Engine* engine) {
    FxInstId delay = engine_fx_track_add(engine, 0, 50);
    assert(delay);
    assert(engine_fx_track_set_param(engine, 0, delay, 0, 1));
    assert(engine_fx_track_set_param(engine, 0, delay, 1, 0));
    assert(engine_fx_track_set_param(engine, 0, delay, 2, 1));
}
// Captures one owned output with an explicit gain-preserving export policy.
static EngineBounceBuffer bounce(Engine* engine, uint64_t first, uint64_t last, uint64_t tail,
                                 uint64_t preroll) {
    EngineBounceOptions options = {.tail_frames = tail, .preroll_frames = preroll};
    EngineBounceBuffer output = {0};
    assert(engine_bounce_range_to_buffer_with_options(engine, first, last, &options, NULL, NULL, &output));
    assert(output.frame_count == last - first + tail && output.channels == 2 && output.sample_rate == 48000);
    return output;
}
// Verifies cold starts, preceding context, bounded effect tails, exact endpoints, and delay trimming.
static void range_and_tail(const char* path) {
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    add_delay(engine);
    EngineSourcePlan* active = engine->active_source_plan;
    EngineBounceBuffer full = bounce(engine, 0, 200, 0, 0), repeat = bounce(engine, 0, 200, 0, 0);
    assert(!memcmp(full.data, repeat.data, 400 * sizeof(float)));
    EngineBounceBuffer cold = bounce(engine, 64, 200, 0, 0), context = bounce(engine, 64, 200, 0, 64);
    assert(!memcmp(context.data, full.data + 128, 272 * sizeof(float)));
    assert(fabsf(context.data[15 * 2] - .125f) < 1e-5f && cold.data[15 * 2] == 0);
    EngineBounceBuffer exact = bounce(engine, 0, 32, 0, 0), tail = bounce(engine, 0, 32, 128, 0);
    for (int i = 0; i < 64; ++i)
        assert(exact.data[i] == 0);
    for (int n = 0; n < 160; ++n)
        for (int ch = 0; ch < 2; ++ch) {
            float expected = (n == 48 ? .25f : n == 79 ? .125f : 0) * (ch ? -.5f : 1);
            assert(fabsf(tail.data[n * 2 + ch] - expected) < 1e-5f);
        }
    assert(engine->active_source_plan == active);
    FxInstId limiter = engine_fx_track_add(engine, 0, 21);
    assert(limiter);
    assert(engine_fx_track_set_param(engine, 0, limiter, 1, 1));
    FxInstId master = engine_fx_master_add(engine, 21);
    assert(master);
    assert(engine_fx_master_set_param(engine, master, 1, 2));
    EngineBounceBuffer delayed = bounce(engine, 0, 32, 128, 0);
    for (int i = 0; i < 320; ++i)
        assert(fabsf(delayed.data[i] - tail.data[i]) < 2e-6f);
    engine_bounce_buffer_free(&full);
    engine_bounce_buffer_free(&repeat);
    engine_bounce_buffer_free(&cold);
    engine_bounce_buffer_free(&context);
    engine_bounce_buffer_free(&exact);
    engine_bounce_buffer_free(&tail);
    engine_bounce_buffer_free(&delayed);
    engine_destroy(engine);
}
// Checks MIDI end gating, bounded release, unchanged pre-cut automation, and exclusion of later notes.
static void midi_tail(void) {
    Engine* engine = create_engine();
    int clip = -1;
    assert(engine_add_midi_clip_to_track(engine, 0, 0, 4096, &clip));
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    p.attack_ms = 0;
    p.decay_ms = 0;
    p.sustain = 1;
    p.release_ms = 10;
    p.level = 1;
    assert(engine_clip_midi_set_instrument_params(engine, 0, clip, p));
    assert(engine_clip_midi_add_note(engine, 0, clip, (EngineMidiNote){0, 3000, 69, 1}, NULL));
    assert(engine_clip_midi_add_note(engine, 0, clip, (EngineMidiNote){1020, 2000, 72, 1}, NULL));
    EngineAutomationPoint points[] = {{0, 0}, {2000, 1}};
    assert(
        engine_clip_set_automation_lane_points(engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, points, 2));
    EngineBounceBuffer exact = bounce(engine, 0, 1000, 0, 0), tail = bounce(engine, 0, 1000, 960, 0);
    assert(!memcmp(exact.data, tail.data, 2000 * sizeof(float)));
    for (int n = 1000; n < 1960; ++n) {
        float env = n < 1480 ? 1 - (n - 1000) / 480.0f : 0;
        float expected =
            (float)sin(2 * 3.14159265358979323846 * 440 * n / 48000) * (.16f + p.tone * .08f) * env * 1.4995f;
        assert(fabsf(tail.data[n * 2] - expected) < 3e-6f);
    }
    assert(engine_get_tracks(engine)[0].clips[clip].midi_notes.notes[0].duration_frames == 3000);
    engine_bounce_buffer_free(&exact);
    engine_bounce_buffer_free(&tail);
    engine_destroy(engine);
}
// Keeps transient record arming of an empty solo track out of authored export selection.
static void recording_intent_isolation(const char* path) {
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    assert(engine_add_track(engine) == 1 && engine_track_set_solo(engine, 1, true));
    EngineBounceBuffer before = bounce(engine, 0, 128, 0, 0);
    atomic_store(&engine->record_armed_track_index, 1);
    EngineBounceBuffer after = bounce(engine, 0, 128, 0, 0);
    assert(before.data[0] == .25f && !memcmp(before.data, after.data, 256 * sizeof(float)));
    engine_bounce_buffer_free(&before);
    engine_bounce_buffer_free(&after);
    engine_destroy(engine);
}

// Carries callback observations and an optional edit that deliberately changes the live authored project.
typedef struct Progress {
    Engine* engine;
    uint64_t last, total;
    bool edit, edited, wait;
    unsigned calls;
} Progress;
// Checks progress bounds and edits only after capture to prove snapshot/media ownership.
static void progress(uint64_t done, uint64_t total, void* user) {
    Progress* state = user;
    assert(done >= state->last && done <= total && (!state->total || state->total == total));
    state->last = done;
    state->total = total;
    ++state->calls;
    if (state->edit && !state->edited) {
        assert(engine_track_set_gain(state->engine, 0, 0));
        assert(engine_remove_clip(state->engine, 0, 0));
        state->edited = true;
    }
    if (state->wait)
        SDL_Delay(1);
}
// Verifies export leaves idle live history/meters untouched and holds media through callback edits.
static void isolation(const char* path) {
    Engine* a = create_engine();
    Engine* b = create_engine();
    assert(engine_add_clip_to_track(a, 0, path, 0, NULL) && engine_add_clip_to_track(b, 0, path, 0, NULL));
    add_delay(a);
    add_delay(b);
    float x[128], y[128], scratch[128];
    engine_mix_tracks(a, 0, 64, x, scratch, 2);
    engine_mix_tracks(b, 0, 64, y, scratch, 2);
    EngineSourcePlan* active = a->active_source_plan;
    EngineMeterState meter = engine_render_mix_state(a)->master_meter;
    uint64_t epoch = atomic_load(&a->clock_epoch), blocks = atomic_load(&a->diag_render_blocks);
    EngineBounceBuffer expected = bounce(a, 0, 200, 0, 0);
    assert(a->active_source_plan == active && atomic_load(&a->clock_epoch) == epoch);
    assert(atomic_load(&a->diag_render_blocks) == blocks);
    assert(!memcmp(&meter, &engine_render_mix_state(a)->master_meter, sizeof(meter)));
    engine_mix_tracks(a, 64, 64, x, scratch, 2);
    engine_mix_tracks(b, 64, 64, y, scratch, 2);
    assert(!memcmp(x, y, sizeof(x)));
    Progress state = {.engine = a, .edit = true};
    EngineBounceBuffer edited = {0};
    assert(engine_bounce_range_to_buffer(a, 0, 200, progress, &state, &edited));
    assert(state.edited && state.calls > 1 && state.last == state.total);
    assert(!memcmp(expected.data, edited.data, 400 * sizeof(float)));
    assert(engine_get_tracks(a)[0].clip_count == 0 && a->media_cache.count == 0);
    engine_bounce_buffer_free(&expected);
    engine_bounce_buffer_free(&edited);
    engine_destroy(a);
    engine_destroy(b);
}
// Proves an actual live callback/worker remains running in the same clock epoch throughout export.
static void live_isolation(const char* path) {
    assert(!SDL_setenv("SDL_AUDIODRIVER", "dummy", 1));
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    EngineBounceBuffer expected = bounce(engine, 0, 8192, 0, 0);
    assert(engine_start(engine) && engine_transport_play(engine));
    for (int i = 0; i < 5000 && !engine_transport_is_playing(engine); ++i)
        SDL_Delay(1);
    assert(engine_transport_is_playing(engine));
    SDL_Thread* worker = engine->worker_thread;
    uint64_t epoch = atomic_load(&engine->clock_epoch);
    uint64_t callbacks = atomic_load(&engine->diag_callback_count);
    Progress state = {.engine = engine, .wait = true};
    EngineBounceBuffer actual = {0};
    assert(engine_bounce_range_to_buffer(engine, 0, 8192, progress, &state, &actual));
    assert(engine->worker_thread == worker && engine_is_running(engine) &&
           engine_transport_is_playing(engine));
    assert(atomic_load(&engine->clock_epoch) == epoch &&
           atomic_load(&engine->diag_callback_count) > callbacks);
    assert(!memcmp(expected.data, actual.data, 8192 * 2 * sizeof(float)));
    char streamed_path[] = "/tmp/daw-live-stream-XXXXXX";
    int fd = mkstemp(streamed_path);
    assert(fd >= 0); close(fd);
    state = (Progress){.engine = engine, .wait = true};
    callbacks = atomic_load(&engine->diag_callback_count);
    assert(engine_bounce_range(engine, 0, 8192, streamed_path, progress, &state));
    assert(engine->worker_thread == worker && engine_transport_is_playing(engine) &&
           atomic_load(&engine->clock_epoch) == epoch && atomic_load(&engine->diag_callback_count) > callbacks);
    AudioMediaClip decoded = {0};
    assert(audio_media_clip_load_wav(streamed_path, 48000, &decoded) && decoded.frame_count == 8192);
    for (size_t i = 0; i < 8192 * 2; ++i) assert(fabsf(decoded.samples[i] - expected.data[i]) < .0001f);
    audio_media_clip_free(&decoded);
    unlink(streamed_path);
    engine_stop(engine);
    engine_bounce_buffer_free(&expected);
    engine_bounce_buffer_free(&actual);
    engine_destroy(engine);
}
// Exercises every instrumented preparation allocation and verifies unchanged caller/live/media ownership.
static void failures(const char* path) {
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    add_delay(engine);
    int clip = -1;
    assert(engine_add_midi_clip_to_track(engine, 0, 512, 512, &clip));
    assert(engine_clip_midi_add_note(engine, 0, clip, (EngineMidiNote){0, 100, 69, 1}, NULL));
    EngineSourcePlan* active = engine->active_source_plan;
    int refs = engine->media_cache.refcounts[0];
    int boundary;
    for (boundary = 0; boundary < 512; ++boundary) {
        EngineBounceBuffer output = {.frame_count = 777};
        fail_after = boundary;
        bool ok = engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &output);
        fail_after = -1;
        assert(engine->active_source_plan == active && engine->media_cache.refcounts[0] == refs);
        if (ok) {
            engine_bounce_buffer_free(&output);
            break;
        }
        assert(!output.data && output.frame_count == 777);
    }
    assert(boundary > 20 && boundary < 512);
    EngineBounceBuffer invalid = {.frame_count = 777};
    EngineBounceOptions options = {.tail_frames = UINT64_MAX};
    assert(!engine_bounce_range_to_buffer_with_options(engine, 0, 32, &options, NULL, NULL, &invalid));
    assert(!engine_bounce_range_to_buffer(engine, 32, 32, NULL, NULL, &invalid));
    assert(!engine_bounce_range_to_buffer(engine, 0, UINT64_MAX, NULL, NULL, &invalid));
    assert(!invalid.data && invalid.frame_count == 777);
    printf("export_render_test: %d instrumented allocation failure boundaries passed\n", boundary);
    engine_destroy(engine);
}
// Reads a complete fixture file for byte-level reproducibility and atomic failure comparisons.
static unsigned char* file_bytes(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    assert(f && !fseek(f, 0, SEEK_END));
    long count = ftell(f);
    assert(count > 0);
    rewind(f);
    unsigned char* bytes = malloc((size_t)count);
    assert(bytes && fread(bytes, 1, (size_t)count, f) == (size_t)count);
    fclose(f);
    *size = (size_t)count;
    return bytes;
}
// Checks explicit normalization, deterministic PCM bytes, float preservation, and failed-write preservation.
static void files_and_gain(const char* path) {
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    assert(engine_track_set_gain(engine, 0, 8));
    EngineBounceBuffer raw = bounce(engine, 0, 128, 0, 0), normalized = {0};
    assert(raw.data[0] == 2);
    assert(engine_bounce_range_to_buffer(engine, 0, 128, NULL, NULL, &normalized));
    assert(fabsf(normalized.data[0] - 1.0f / 3) < 1e-6f); // Peak .75 * 8 = 6.
    char dest[] = "/tmp/daw-export-output-XXXXXX";
    int fd = mkstemp(dest);
    assert(fd >= 0);
    close(fd);
    assert(engine_bounce_write_wav(&normalized, dest, ENGINE_BOUNCE_WAV_PCM16));
    size_t before_size, after_size;
    unsigned char* before = file_bytes(dest, &before_size);
    assert(engine_bounce_write_wav(&normalized, dest, ENGINE_BOUNCE_WAV_PCM16));
    unsigned char* after = file_bytes(dest, &after_size);
    assert(before_size == after_size && !memcmp(before, after, before_size));
    free(after);
    assert(engine_bounce_range(engine, 0, 128, dest, NULL, NULL));
    after = file_bytes(dest, &after_size);
    assert(before_size == after_size && !memcmp(before, after, before_size));
    free(after);
    float saved = normalized.data[10];
    normalized.data[10] = NAN;
    assert(!engine_bounce_write_wav(&normalized, dest, ENGINE_BOUNCE_WAV_PCM16));
    normalized.data[10] = saved;
    after = file_bytes(dest, &after_size);
    assert(before_size == after_size && !memcmp(before, after, before_size));
    free(after);
    free(before);
    assert(!engine_bounce_write_wav(&normalized, dest, (EngineBounceWavFormat)999));
    assert(engine_bounce_write_wav(&raw, dest, ENGINE_BOUNCE_WAV_FLOAT32));
    AudioMediaClip loaded = {0};
    assert(audio_media_clip_load_wav(dest, 48000, &loaded));
    assert(loaded.frame_count == raw.frame_count && !memcmp(loaded.samples, raw.data, 256 * sizeof(float)));
    audio_media_clip_free(&loaded);
    EngineBounceOptions no_normalize = {.normalize_if_clipping = false};
    assert(engine_bounce_range_to_wav(engine, 0, 128, &no_normalize, dest, ENGINE_BOUNCE_WAV_FLOAT32, NULL) == DAW_SAVE_SYNCED);
    assert(audio_media_clip_load_wav(dest, 48000, &loaded));
    assert(loaded.frame_count == raw.frame_count && !memcmp(loaded.samples, raw.data, 256 * sizeof(float)));
    audio_media_clip_free(&loaded);
    assert(engine_bounce_range(engine, 0, 128, dest, NULL, NULL));
    char sidecar[256];
    snprintf(sidecar, sizeof(sidecar), "%s.f32.wav", dest);
    assert(access(sidecar, F_OK) != 0);
    assert(engine_track_set_gain(engine, 0, 1000));
    EngineBounceBuffer invalid = {.frame_count = 777};
    assert(!engine_bounce_range_to_buffer(engine, 0, 128, NULL, NULL, &invalid));
    assert(!invalid.data && invalid.frame_count == 777);
    unlink(dest);
    engine_bounce_buffer_free(&raw);
    engine_bounce_buffer_free(&normalized);
    engine_destroy(engine);
}
// Tracks both export passes and cancels at a selected boundary without touching the destination.
typedef struct StreamProbe {
    Progress progress;
    unsigned cancel_stage;
    uint64_t observed;
} StreamProbe;

// Checks monotonic progress and optionally cancels before render, during render, during write, or before commit.
static bool stream_progress(uint64_t done, uint64_t total, void* user) {
    StreamProbe* probe = user;
    progress(done, total, &probe->progress);
    switch (probe->cancel_stage) {
        case 1: return done != 0;
        case 2: return done < total / 4;
        case 3: return done < total * 3 / 4;
        case 4: return done != total;
        default: return true;
    }
}

// Confirms normalized chunk observers receive one contiguous complete output sequence.
static void stream_samples(const float* samples, uint64_t first, uint32_t frames, int channels, void* user) {
    StreamProbe* probe = user;
    assert(first == probe->observed && channels == 2);
    for (size_t i = 0; i < (size_t)frames * channels; ++i) assert(isfinite(samples[i]));
    probe->observed += frames;
}

// Proves streamed PCM/float parity, normalization, PDC/context/tails, cancellation and captured media lifetime.
static void streaming(const char* source) {
    Engine* engine = create_engine();
    assert(engine_add_clip_to_track(engine, 0, source, 0, NULL));
    assert(engine_track_set_gain(engine, 0, 8));
    add_delay(engine);
    FxInstId limiter = engine_fx_master_add(engine, 21);
    assert(limiter && engine_fx_master_set_param(engine, limiter, 1, 2));
    char path[] = "/tmp/daw-stream-parity-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0); close(fd);
    EngineBounceOptions options = {.preroll_frames = 32, .tail_frames = 97, .normalize_if_clipping = true};
    EngineBounceBuffer reference = {0};
    assert(engine_bounce_range_to_buffer_with_options(engine, 32, 1001, &options, NULL, NULL, &reference));
    for (int format = 0; format <= 1; ++format) {
        assert(engine_bounce_write_wav(&reference, path, format));
        size_t expected_size, actual_size;
        unsigned char* expected = file_bytes(path, &expected_size);
        StreamProbe probe = {0};
        EngineBounceStreamCallbacks callbacks = {stream_progress, stream_samples, &probe};
        assert(engine_bounce_range_to_wav(engine, 32, 1001, &options, path, format, &callbacks) == DAW_SAVE_SYNCED);
        assert(probe.observed == reference.frame_count && probe.progress.last == probe.progress.total);
        unsigned char* actual = file_bytes(path, &actual_size);
        assert(actual_size == expected_size && !memcmp(expected, actual, actual_size));
        free(actual);
        for (unsigned stage = 1; stage <= 4; ++stage) {
            probe = (StreamProbe){.cancel_stage = stage};
            assert(engine_bounce_range_to_wav(engine, 32, 1001, &options, path, format, &callbacks) == DAW_SAVE_FAILED);
            actual = file_bytes(path, &actual_size);
            assert(actual_size == expected_size && !memcmp(expected, actual, actual_size));
            free(actual);
        }
        free(expected);
    }
    size_t preserved_size, failed_size;
    unsigned char* preserved = file_bytes(path, &preserved_size);
    for (io_fault = 1; io_fault <= 6; ++io_fault) {
        assert(engine_bounce_range_to_wav(engine, 32, 1001, &options, path, ENGINE_BOUNCE_WAV_FLOAT32, NULL) == DAW_SAVE_FAILED);
        unsigned char* failed = file_bytes(path, &failed_size);
        assert(failed_size == preserved_size && !memcmp(failed, preserved, failed_size));
        free(failed);
    }
    io_fault = 0;
    free(preserved);
    int refs = engine->media_cache.refcounts[0], boundary;
    for (boundary = 0; boundary < 512; ++boundary) {
        fail_after = boundary;
        DawSaveResult result = engine_bounce_range_to_wav(engine, 32, 1001, &options, path, ENGINE_BOUNCE_WAV_FLOAT32, NULL);
        fail_after = -1;
        assert(engine->media_cache.refcounts[0] == refs);
        if (result == DAW_SAVE_SYNCED) break;
        assert(result == DAW_SAVE_FAILED);
    }
    assert(boundary > 20 && boundary < 512);
    printf("export_render_test: %d streaming allocation failure boundaries passed\n", boundary);
    StreamProbe edited = {.progress = {.engine = engine, .edit = true}};
    EngineBounceStreamCallbacks callbacks = {stream_progress, stream_samples, &edited};
    assert(engine_bounce_range_to_wav(engine, 32, 1001, &options, path, ENGINE_BOUNCE_WAV_FLOAT32, &callbacks) == DAW_SAVE_SYNCED);
    AudioMediaClip loaded = {0};
    assert(audio_media_clip_load_wav(path, 48000, &loaded));
    assert(loaded.frame_count == reference.frame_count &&
           !memcmp(loaded.samples, reference.data, reference.frame_count * 2 * sizeof(float)));
    audio_media_clip_free(&loaded);
    engine_bounce_buffer_free(&reference);
    unlink(path);
    engine_destroy(engine);
}

// Runs export contracts against production sources, DSP, ownership, live playback, and WAV publication.
int main(void) {
    char path[] = "/tmp/daw-export-input-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    float samples[512] = {0};
    samples[0] = .25f;
    samples[31 * 2] = .125f;
    samples[32 * 2] = samples[64 * 2] = .75f;
    for (int n = 0; n < 256; ++n)
        samples[n * 2 + 1] = samples[n * 2] * -.5f;
    assert(wav_write_f32(path, samples, 256, 2, 48000));
    range_and_tail(path);
    midi_tail();
    recording_intent_isolation(path);
    isolation(path);
    failures(path);
    streaming(path);
    files_and_gain(path);
    live_isolation(path);
    assert(!unlink(path));
    puts("export_render_test: success (repeatability, ranges, preroll, release/FX tails, PDC, snapshot/live "
         "isolation, failures, normalization, deterministic WAV)");
    return 0;
}
