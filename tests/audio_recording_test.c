#include "app/audio_recording.h"

#include "audio/wav_writer.h"
#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "engine/engine_internal.h"
#include "engine/track_role.h"
#include "session.h"
#include "undo/undo_manager.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool reject_record_arm;
// Rejects recording isolation deterministically before the recording coordinator can start capture.
static bool recording_test_arm(Engine* engine, int track) {
    return !reject_record_arm && engine_set_record_armed_track(engine, track);
}
static atomic_bool stall_sync, entered_sync, fail_sync;
// Holds journal synchronization on its worker so UI polling and failure publication can be tested separately.
static bool recording_test_sync(DawTakeJournal* journal) {
    if (atomic_load(&stall_sync)) {
        atomic_store(&entered_sync, true);
        while (atomic_load(&stall_sync)) SDL_Delay(1);
    }
    return !atomic_load(&fail_sync) && daw_take_journal_sync(journal);
}
static bool reject_capture_clock;
// Forces the existing bounded clock-read rejection path without changing transport semantics.
static bool recording_test_clock(const Engine* engine, EngineClockSnapshot* clock) {
    return !reject_capture_clock && engine_get_clock_snapshot(engine, clock);
}
#define engine_get_clock_snapshot recording_test_clock
#define daw_take_journal_sync recording_test_sync
#define engine_set_record_armed_track recording_test_arm
#include "../src/app/audio_recording.c"
#undef engine_set_record_armed_track
#undef daw_take_journal_sync
#undef engine_get_clock_snapshot

static bool file_exists(const char* path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static float average_abs_bounce(const EngineBounceBuffer* bounce) {
    if (!bounce || !bounce->data || bounce->channels <= 0 || bounce->frame_count == 0) {
        return 0.0f;
    }
    double sum = 0.0;
    uint64_t samples = bounce->frame_count * (uint64_t)bounce->channels;
    for (uint64_t i = 0; i < samples; ++i) {
        sum += fabsf(bounce->data[i]);
    }
    return (float)(sum / (double)samples);
}

static void write_constant_wav_or_fail(const char* path, float value, int frames, int sample_rate) {
    float samples[512];
    assert(frames <= (int)(sizeof(samples) / sizeof(samples[0])));
    for (int i = 0; i < frames; ++i) {
        samples[i] = value;
    }
    assert(wav_write_pcm16_dithered(path, samples, (uint64_t)frames, 1, sample_rate, 0xA23u));
}

static void assert_status_contains(const DawAudioRecordingState* recording, const char* needle) {
    const char* status = daw_audio_recording_status_message(recording);
    if (!strstr(status, needle)) {
        fprintf(stderr, "expected status to contain '%s', got '%s'\n", needle, status);
        assert(false);
    }
}

static void prepare_state(AppState* state, char* temp_dir_template) {
    memset(state, 0, sizeof(*state));
    mkdir("tmp", 0755);
    char* root = mkdtemp(temp_dir_template);
    assert(root != NULL);

    config_set_defaults(&state->runtime_cfg);
    state->runtime_cfg.sample_rate = 48000;
    state->runtime_cfg.block_size = 128;
    snprintf(state->data_paths.input_root, sizeof(state->data_paths.input_root), "%s", root);
    snprintf(state->data_paths.output_root, sizeof(state->data_paths.output_root), "%s", root);
    snprintf(state->data_paths.library_copy_root, sizeof(state->data_paths.library_copy_root), "%s", root);
    state->engine = engine_create(&state->runtime_cfg);
    assert(state->engine != NULL);
    assert(engine_get_track_count(state->engine) >= 1);
    undo_manager_init(&state->undo);
    char registry_path[SESSION_PATH_MAX];
    snprintf(registry_path, sizeof(registry_path), "%s/library_index.json", root);
    media_registry_init(&state->media_registry, registry_path);
    daw_audio_recording_init(&state->audio_recording);
}

static void cleanup_state(AppState* state) {
    undo_manager_free(&state->undo);
    daw_audio_recording_free(&state->audio_recording);
    media_registry_shutdown(&state->media_registry);
    if (state->engine) {
        engine_destroy(state->engine);
        state->engine = NULL;
    }
}

static void test_next_path_creates_recordings_directory(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_path_XXXXXX";
    prepare_state(&state, temp_dir);

    char path[SESSION_PATH_MAX];
    assert(daw_audio_recording_next_path(&state, path, sizeof(path)));
    assert(strstr(path, "/recordings/recording.wav") != NULL);

    cleanup_state(&state);
}

static void test_next_path_rejects_unsafe_library_copy_root(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_safe_root_XXXXXX";
    prepare_state(&state, temp_dir);

    char safe_output[SESSION_PATH_MAX];
    snprintf(safe_output, sizeof(safe_output), "%s", state.data_paths.output_root);
    snprintf(state.data_paths.library_copy_root,
             sizeof(state.data_paths.library_copy_root),
             "/tmp/daw_recording.app/Contents/library_copy");

    char path[SESSION_PATH_MAX];
    assert(daw_audio_recording_next_path(&state, path, sizeof(path)));
    assert(strstr(path, ".app/Contents") == NULL);
    assert(strstr(path, safe_output) == path);
    assert(strstr(path, "/recordings/recording.wav") != NULL);

    cleanup_state(&state);
}

static void test_synthetic_take_writes_wav_and_inserts_audio_clip(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_take_XXXXXX";
    prepare_state(&state, temp_dir);

    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 128,
        .channels = 1
    };
    int record_track = engine_add_track(state.engine);
    assert(record_track == 1);
    state.selected_track_index = record_track;
    state.active_track_index = 0;
    state.timeline_drop_track_index = 0;
    assert(daw_audio_recording_begin_take(&state, state.selected_track_index, 2400, &spec));
    uint64_t recording_id = state.audio_recording.target_track_id;
    assert(engine_insert_track(state.engine, 0));
    ++record_track;
    assert(engine_get_tracks(state.engine)[record_track].runtime_id == recording_id);

    float samples[480];
    for (int i = 0; i < 480; ++i) {
        samples[i] = sinf((float)i * 0.02f) * 0.25f;
    }
    assert(!engine_transport_is_playing(state.engine));
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 120, 1) == 120);
    assert(daw_audio_recording_drain_if_transport_playing(&state) == 0);
    assert(state.audio_recording.take_frame_count == 0);

    assert(engine_transport_play(state.engine));
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 480, 1) == 480);
    assert(daw_audio_recording_drain_if_transport_playing(&state) == 480);
    AudioMediaClip take_view;
    assert(daw_audio_recording_take_clip_view(&state.audio_recording, &take_view));
    assert(take_view.samples == state.audio_recording.take_frames);
    assert(take_view.frame_count == 480);
    assert(take_view.sample_rate == 48000);
    assert(take_view.channels == 1);
    assert(fabsf(take_view.samples[10] - samples[10]) < 0.0001f);
    assert(engine_transport_stop(state.engine));
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 120, 1) == 120);
    assert(daw_audio_recording_drain_if_transport_playing(&state) == 0);
    assert(state.audio_recording.take_frame_count == 480);

    DawAudioRecordingResult result;
    assert(daw_audio_recording_finish(&state, &result));
    assert(result.inserted);
    assert(result.track_index == record_track);
    assert(result.clip_index >= 0);
    assert(result.frame_count == 480);
    assert(result.sample_rate == 48000);
    assert(result.channels == 1);
    assert(file_exists(result.wav_path));

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    assert(tracks != NULL);
    assert(tracks[record_track].clip_count == 1);
    const EngineClip* clip = &tracks[record_track].clips[result.clip_index];
    assert(clip->kind == ENGINE_CLIP_KIND_AUDIO);
    assert(clip->timeline_start_frames == 2400);
    assert(clip->duration_frames == 480);
    assert(engine_clip_get_media_path(clip) != NULL);
    assert(strcmp(engine_clip_get_media_path(clip), result.wav_path) == 0);
    assert(state.selection_count == 1);
    assert(state.selected_track_index == record_track);
    assert(state.selected_clip_index == result.clip_index);

    SessionDocument doc;
    session_document_init(&doc);
    assert(session_document_capture(&state, &doc));
    assert(doc.track_count >= 2);
    assert(doc.tracks[record_track].clip_count == 1);
    assert(doc.tracks[record_track].clips[0].kind == ENGINE_CLIP_KIND_AUDIO);
    assert(strcmp(doc.tracks[record_track].clips[0].media_path, result.wav_path) == 0);
    session_document_free(&doc);

    assert(undo_manager_can_undo(&state.undo));
    assert(undo_manager_undo(&state.undo, &state));
    tracks = engine_get_tracks(state.engine);
    assert(tracks != NULL);
    assert(tracks[record_track].clip_count == 0);
    assert(undo_manager_can_redo(&state.undo));
    assert(undo_manager_redo(&state.undo, &state));
    tracks = engine_get_tracks(state.engine);
    assert(tracks != NULL);
    assert(tracks[record_track].clip_count == 1);
    clip = &tracks[record_track].clips[0];
    assert(clip->kind == ENGINE_CLIP_KIND_AUDIO);
    assert(strcmp(engine_clip_get_media_path(clip), result.wav_path) == 0);

    cleanup_state(&state);
}

static void test_empty_take_finish_reports_error_and_cancel_resets(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_empty_take_XXXXXX";
    prepare_state(&state, temp_dir);

    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 128,
        .channels = 1
    };
    int record_track = engine_add_track(state.engine);
    assert(record_track == 1);
    state.selected_track_index = record_track;
    state.selected_clip_index = -1;
    state.selection_count = 0;
    assert(daw_audio_recording_begin_take(&state, record_track, 1200, &spec));
    assert(daw_audio_recording_is_active(&state.audio_recording));
    assert(state.audio_recording.target_track_index == record_track);
    assert(state.audio_recording.queue_ready);
    assert(state.audio_recording.take_frame_count == 0);

    DawAudioRecordingResult result;
    assert(!daw_audio_recording_finish(&state, &result));
    assert(!result.inserted);
    assert(result.track_index == -1);
    assert(result.clip_index == -1);
    assert(state.audio_recording.status == DAW_AUDIO_RECORDING_ERROR);
    assert(strstr(daw_audio_recording_status_message(&state.audio_recording), "no frames") != NULL);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    assert(tracks != NULL);
    assert(tracks[record_track].clip_count == 0);
    assert(state.selected_track_index == record_track);
    assert(state.selected_clip_index == -1);
    assert(state.selection_count == 0);

    daw_audio_recording_cancel(&state.audio_recording);
    assert(state.audio_recording.status == DAW_AUDIO_RECORDING_IDLE);
    assert(!state.audio_recording.queue_ready);
    assert(state.audio_recording.take_frames == NULL);
    assert(state.audio_recording.take_frame_count == 0);
    assert(state.audio_recording.target_track_index == -1);
    assert(!daw_audio_recording_is_active(&state.audio_recording));

    cleanup_state(&state);
}

static void test_begin_take_applies_target_role_policy(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_role_status_XXXXXX";
    prepare_state(&state, temp_dir);

    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 128,
        .channels = 1
    };

    int empty_track = engine_add_track(state.engine);
    assert(empty_track == 1);
    assert(daw_audio_recording_begin_take(&state, empty_track, 0, &spec));
    assert_status_contains(&state.audio_recording, "empty track 2");
    daw_audio_recording_cancel(&state.audio_recording);

    char audio_path[SESSION_PATH_MAX];
    snprintf(audio_path, sizeof(audio_path), "%s/audio_role.wav", state.data_paths.library_copy_root);
    write_constant_wav_or_fail(audio_path, 0.0f, 256, state.runtime_cfg.sample_rate);

    int audio_track = engine_add_track(state.engine);
    assert(audio_track == 2);
    int clip_index = -1;
    assert(engine_add_clip_to_track(state.engine, audio_track, audio_path, 0, &clip_index));
    EngineTrackRole role = ENGINE_TRACK_ROLE_EMPTY;
    assert(engine_track_role_resolve(state.engine, audio_track, &role));
    assert(role == ENGINE_TRACK_ROLE_AUDIO);
    assert(daw_audio_recording_begin_take(&state, audio_track, 0, &spec));
    assert_status_contains(&state.audio_recording, "audio track 3");
    daw_audio_recording_cancel(&state.audio_recording);

    int midi_track = engine_add_track(state.engine);
    assert(midi_track == 3);
    assert(engine_add_midi_clip_to_track(state.engine, midi_track, 0, 48000, &clip_index));
    assert(engine_track_role_resolve(state.engine, midi_track, &role));
    assert(role == ENGINE_TRACK_ROLE_MIDI);
    assert(!daw_audio_recording_begin_take(&state, midi_track, 0, &spec));
    assert(!daw_audio_recording_is_active(&state.audio_recording));
    assert(state.audio_recording.status == DAW_AUDIO_RECORDING_ERROR);
    assert(!state.audio_recording.queue_ready);
    assert(state.audio_recording.target_track_index == -1);
    assert_status_contains(&state.audio_recording, "track 4 is MIDI-only");

    int mixed_track = engine_add_track(state.engine);
    assert(mixed_track == 4);
    assert(engine_add_midi_clip_to_track(state.engine, mixed_track, 0, 48000, &clip_index));
    assert(engine_add_clip_to_track(state.engine, mixed_track, audio_path, 96000, &clip_index));
    assert(engine_track_role_resolve(state.engine, mixed_track, &role));
    assert(role == ENGINE_TRACK_ROLE_MIXED);
    assert(daw_audio_recording_begin_take(&state, mixed_track, 0, &spec));
    assert_status_contains(&state.audio_recording, "mixed track 5");
    daw_audio_recording_cancel(&state.audio_recording);

    cleanup_state(&state);
}

// Measures the real prepared live mixer rather than using offline export as a monitoring proxy.
static float average_live_backing(Engine* engine) {
    engine_source_plan_apply(engine);
    int channels = engine_graph_get_channels(engine->graph);
    assert(channels > 0 && channels <= 2);
    float output[256], scratch[256];
    double total = 0.0;
    for (int block = 0; block < 4; ++block) {
        engine_mix_tracks(engine, (uint64_t)block * 128, 128, output, scratch, channels);
        for (int i = 0; i < 128 * channels; ++i) total += fabsf(output[i]);
    }
    return (float)(total / (512 * channels));
}

// Keeps record-armed solo isolation in live monitoring while authored export ignores recording intent.
static void test_record_armed_solo_track_gates_backing_audio(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_solo_XXXXXX";
    prepare_state(&state, temp_dir);

    char backing_path[SESSION_PATH_MAX];
    snprintf(backing_path, sizeof(backing_path), "%s/backing.wav", state.data_paths.library_copy_root);
    write_constant_wav_or_fail(backing_path, 0.35f, 512, state.runtime_cfg.sample_rate);

    int backing_clip = -1;
    assert(engine_add_clip_to_track(state.engine, 0, backing_path, 0, &backing_clip));
    int record_track = engine_add_track(state.engine);
    assert(record_track == 1);
    assert(engine_track_set_solo(state.engine, record_track, true));

    EngineBounceBuffer before = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &before));
    assert(average_abs_bounce(&before) > 0.1f);
    assert(average_live_backing(state.engine) > 0.1f);
    engine_bounce_buffer_free(&before);

    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 128,
        .channels = 1
    };
    assert(daw_audio_recording_begin_take(&state, record_track, 0, &spec));

    EngineBounceBuffer armed = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &armed));
    assert(average_abs_bounce(&armed) > 0.1f);
    assert(average_live_backing(state.engine) < 0.001f);
    engine_bounce_buffer_free(&armed);

    assert(engine_track_set_solo(state.engine, 0, true));
    EngineBounceBuffer backing_soloed = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &backing_soloed));
    assert(average_abs_bounce(&backing_soloed) > 0.1f);
    assert(average_live_backing(state.engine) > 0.1f);
    engine_bounce_buffer_free(&backing_soloed);

    daw_audio_recording_cancel(&state.audio_recording);
    EngineBounceBuffer after = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &after));
    assert(average_abs_bounce(&after) > 0.1f);
    assert(average_live_backing(state.engine) > 0.1f);
    engine_bounce_buffer_free(&after);

    cleanup_state(&state);
}

// Exercises retry after a drain failure while a real dummy capture callback is still active.
static void test_capture_error_retry_quiesces_old_queue(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_retry_XXXXXX";
    prepare_state(&state, temp_dir);
    AudioDeviceSpec spec = {.sample_rate = 48000, .block_size = 128, .channels = 1};
    for (int retry = 0; retry < 4; ++retry) {
        assert(daw_audio_recording_begin_capture(&state, 0, 0, NULL, &spec));
        assert(state.audio_recording.capture_device_started);
        uint64_t deadline = SDL_GetTicks64() + 2000;
        uint64_t drained = 0;
        while (!drained && SDL_GetTicks64() < deadline) {
            drained = daw_audio_recording_drain(&state.audio_recording);
            if (!drained) SDL_Delay(2);
        }
        assert(drained > 0);
        state.audio_recording.status = DAW_AUDIO_RECORDING_ERROR;
        char recovery[1024];
        SDL_strlcpy(recovery, state.audio_recording.journal.path, sizeof(recovery));
        assert(!daw_audio_recording_begin_capture(&state, 0, 0, NULL, &spec));
        daw_audio_recording_cancel(&state.audio_recording);
        assert(file_exists(recovery));
    }
    // A synthetic take retry also has to stop a previously active device first.
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    assert(!state.audio_recording.capture_device_started && !state.audio_recording.capture_device.is_open);
    daw_audio_recording_cancel(&state.audio_recording);
    assert(!state.audio_recording.queue_ready);
    cleanup_state(&state);
}

// Verifies rejected isolation never advertises an active take and closes a newly opened endpoint.
static void test_record_arm_rejection_and_retry(void) {
    AppState state;
    char temp_dir[] = "tmp/audio_recording_arm_XXXXXX";
    prepare_state(&state, temp_dir);
    AudioDeviceSpec spec = {.sample_rate = 48000, .block_size = 128, .channels = 1};
    reject_record_arm = true;
    assert(!daw_audio_recording_begin_take(&state, 0, 0, &spec));
    assert(!daw_audio_recording_is_active(&state.audio_recording));
    assert(!state.audio_recording.queue_ready && !state.audio_recording.record_armed_engine);
    assert_status_contains(&state.audio_recording, "Could not prepare");
    assert(!daw_audio_recording_begin_capture(&state, 0, 0, NULL, &spec));
    assert(!state.audio_recording.capture_device.is_open && !state.audio_recording.capture_device_started);
    assert(!state.audio_recording.queue_ready);
    reject_record_arm = false;
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    assert(daw_audio_recording_is_active(&state.audio_recording));
    cleanup_state(&state);
}

// Rejects a deleted target even when another track occupies the same array index.
static void test_deleted_recording_target(void) {
    AppState state;
    char root[] = "tmp/recording-target-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .block_size = 128, .channels = 1};
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    float frames[8] = {0.25f};
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, frames, 8, 1) == 8);
    assert(daw_audio_recording_drain(&state.audio_recording) == 8);
    assert(engine_remove_track(state.engine, 0));
    assert(engine_add_track(state.engine) == 0);
    DawAudioRecordingResult result;
    assert(!daw_audio_recording_finish(&state, &result));
    assert(!result.inserted && result.track_index == -1);
    assert(engine_get_tracks(state.engine)[0].clip_count == 0);
    assert(state.audio_recording.take_frame_count == 8);
    assert_status_contains(&state.audio_recording, "track was removed");
    cleanup_state(&state);
}

// Preserves callback placement and dropped intervals, then retries failed publication.
static void test_timed_gaps_and_publication_retry(void) {
    AppState state;
    char root[] = "tmp/recording-timing-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .block_size = 128, .channels = 1};
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    DawAudioRecordingState* recording = &state.audio_recording;
    recording->timeline_aligned = true;
    ringbuf_free(&recording->capture_packets);
    assert(ringbuf_init(&recording->capture_packets, sizeof(DawCapturePacket) * 2));
    float samples[1536];
    for (int i = 0; i < 1536; ++i) samples[i] = 0.25f;
    EngineClockSnapshot clock = {.epoch = 42, .playing = false, .presentation_frame = 2536};
    assert(daw_audio_recording_enqueue_timed(recording, samples, 1536, 1, 100, &clock) == 0);
    clock.playing = true;
    size_t accepted = daw_audio_recording_enqueue_timed(recording, samples, 1536, 1, 200, &clock);
    assert(accepted > 0 && accepted < 1536);
    assert(daw_audio_recording_drain(recording) == accepted);
    clock.presentation_frame = 2792;
    assert(daw_audio_recording_enqueue_timed(recording, samples, 256, 1, 300, &clock) == 256);
    daw_audio_recording_drain(recording);
    assert(recording->start_frame == 1000 && recording->take_frame_count == 1792);
    assert(recording->gap_frames == 1536 - accepted && recording->alignment_error_frames == 0);
    for (size_t i = 0; i < 1792; ++i)
        assert(recording->take_frames[i] == ((i >= accepted && i < 1536) ? 0.0f : 0.25f));
    clock.epoch++;
    assert(daw_audio_recording_enqueue_timed(recording, samples, 256, 1, 400, &clock) == 0);
    assert(atomic_load(&recording->capture_halted));
    char journal[1024], destination[1100];
    snprintf(journal, sizeof(journal), "%s", recording->journal.path);
    snprintf(destination, sizeof(destination), "%s.wav", journal);
    assert(mkdir(destination, 0700) == 0);
    DawAudioRecordingResult result;
    assert(!daw_audio_recording_finish(&state, &result));
    assert(recording->take_frame_count == 1792 && file_exists(journal));
    assert(engine_get_tracks(state.engine)[0].clip_count == 0);
    assert(rmdir(destination) == 0);
    assert(daw_audio_recording_finish(&state, &result));
    assert(result.frame_count == 1792 && result.inserted);
    assert(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 1000);
    snprintf(destination, sizeof(destination), "%s.recovered.wav", journal);
    DawTakeRecoveryInfo recovery;
    assert(daw_take_journal_recover(journal, destination, &recovery));
    assert(recovery.frames == 1792 && recovery.start_frame == 1000 && !recovery.incomplete_tail);
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    recording = &state.audio_recording;
    assert(daw_audio_recording_enqueue_frames(recording, samples, 256, 1) == 256);
    assert(daw_audio_recording_drain(recording) == 256);
    snprintf(journal, sizeof(journal), "%s", recording->journal.path);
    samples[0] = NAN;
    assert(daw_audio_recording_enqueue_frames(recording, samples, 256, 1) == 256);
    assert(daw_audio_recording_drain(recording) == 0 && recording->drain_failed);
    assert(!daw_audio_recording_finish(&state, &result));
    daw_audio_recording_cancel(recording);
    snprintf(destination, sizeof(destination), "%s.recovered.wav", journal);
    assert(daw_take_journal_recover(journal, destination, &recovery));
    assert(recovery.frames == 256);
    spec.sample_rate = 44100;
    assert(!daw_audio_recording_begin_take(&state, 0, 0, &spec));
    state.loop_enabled = true;
    assert(!daw_audio_recording_begin_timeline_capture(&state));
    cleanup_state(&state);
}

// Exercises capture-clock reads concurrently with a real worker and SDL dummy callbacks.
static void test_live_aligned_capture(void) {
    AppState state;
    char root[] = "tmp/recording-live-aligned-XXXXXX";
    prepare_state(&state, root);
    assert(engine_start(state.engine));
    assert(engine_transport_play(state.engine));
    assert(daw_audio_recording_begin_timeline_capture(&state));
    for (int i = 0; i < 1000 && !state.audio_recording.take_frame_count; ++i) {
        daw_audio_recording_drain_if_transport_playing(&state);
        SDL_Delay(1);
    }
    assert(state.audio_recording.take_frame_count > 0);
    assert(atomic_load(&state.audio_recording.first_capture_ns) > 0);
    DawAudioRecordingResult result;
    assert(daw_audio_recording_finish_timeline_capture(&state, &result));
    assert(result.inserted && result.frame_count > 0);
    engine_stop(state.engine);
    cleanup_state(&state);
}

// Verifies preview rollover never truncates journal audio or shifts the finalized take.
static void test_bounded_long_take(void) {
    AppState state;
    char root[] = "tmp/recording-bounded-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .channels = 1, .block_size = 128};
    assert(daw_audio_recording_begin_take(&state, 0, 100, &spec));
    assert(engine_transport_play(state.engine));
    float samples[4096];
    uint64_t total = DAW_RECORDING_PREVIEW_FRAMES * 3 + 17;
    for (uint64_t first = 0; first < total;) {
        uint32_t count = total - first < 4096 ? (uint32_t)(total - first) : 4096;
        for (uint32_t i = 0; i < count; ++i) samples[i] = (float)((first + i) % 1000) / 2000;
        assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, count, 1) == count);
        assert(daw_audio_recording_drain(&state.audio_recording) == count);
        assert(state.audio_recording.take_frame_capacity == DAW_RECORDING_PREVIEW_FRAMES);
        first += count;
    }
    AudioMediaClip view = {0};
    assert(daw_audio_recording_take_clip_view(&state.audio_recording, &view));
    assert(view.frame_count == DAW_RECORDING_PREVIEW_FRAMES);
    for (uint64_t i = 0; i < view.frame_count; ++i)
        assert(view.samples[i] == (float)((total - view.frame_count + i) % 1000) / 2000);
    DawAudioRecordingResult result;
    assert(daw_audio_recording_finish(&state, &result) && result.frame_count == total);
    AudioMediaClip decoded = {0};
    assert(audio_media_clip_load_wav(result.wav_path, 48000, &decoded));
    assert(decoded.frame_count == total);
    for (uint64_t i = 0; i < total; ++i)
        assert(fabsf(decoded.samples[i] - (float)(i % 1000) / 2000) < .0001f);
    audio_media_clip_free(&decoded);
    cleanup_state(&state);
}

// Checks that capture checkpoints advance without UI draining and cancellation joins before freeing storage.
static void test_worker_without_ui_drain(void) {
    AppState state;
    char root[] = "tmp/recording-worker-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .channels = 1, .block_size = 128};
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    state.audio_recording.timeline_aligned = true;
    assert(daw_recording_worker_start(&state.audio_recording));
    float samples[4096];
    for (int i = 0; i < 4096; ++i) samples[i] = .25f;
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 4096, 1) == 4096);
    DawRecordingWorker* worker = state.audio_recording.worker;
    bool checkpointed = false;
    for (int i = 0; i < 1000 && !checkpointed; ++i) {
        SDL_Delay(1);
        SDL_LockMutex(worker->mutex);
        checkpointed = worker->snapshot.checkpoint_frames == 4096;
        SDL_UnlockMutex(worker->mutex);
    }
    assert(checkpointed && state.audio_recording.take_frame_count == 0);
    assert(daw_audio_recording_drain(&state.audio_recording) == 4096);
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 4096, 1) == 4096);
    char journal[1024], recovered[1100];
    snprintf(journal, sizeof(journal), "%s", state.audio_recording.journal.path);
    snprintf(recovered, sizeof(recovered), "%s.recovered.wav", journal);
    daw_audio_recording_cancel(&state.audio_recording);
    assert(!state.audio_recording.worker && !state.audio_recording.take_frames);
    DawTakeRecoveryInfo info;
    assert(daw_take_journal_recover(journal, recovered, &info) && info.frames == 8192);
    unlink(recovered);
    cleanup_state(&state);
}

// Proves slow storage never holds the UI publication mutex and failures stop capture with a retained journal.
static void test_worker_storage_failure(void) {
    AppState state;
    char root[] = "tmp/recording-worker-fault-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .channels = 1, .block_size = 128};
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    atomic_store(&entered_sync, false);
    atomic_store(&stall_sync, true);
    state.audio_recording.timeline_aligned = true;
    assert(daw_recording_worker_start(&state.audio_recording));
    float samples[256] = {0};
    assert(daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 256, 1) == 256);
    for (int i = 0; i < 2000 && !atomic_load(&entered_sync); ++i) SDL_Delay(1);
    assert(atomic_load(&entered_sync));
    Uint64 began = SDL_GetPerformanceCounter();
    daw_audio_recording_drain(&state.audio_recording);
    assert((SDL_GetPerformanceCounter() - began) * 1000 / SDL_GetPerformanceFrequency() < 100);
    atomic_store(&fail_sync, true);
    atomic_store(&stall_sync, false);
    for (int i = 0; i < 2000 && !state.audio_recording.drain_failed; ++i) {
        daw_audio_recording_drain(&state.audio_recording);
        SDL_Delay(1);
    }
    assert(state.audio_recording.drain_failed && atomic_load(&state.audio_recording.capture_halted));
    assert(!daw_audio_recording_enqueue_frames(&state.audio_recording, samples, 256, 1));
    assert(state.audio_recording.checkpoint_frames == 0);
    atomic_store(&fail_sync, false);
    daw_audio_recording_cancel(&state.audio_recording);
    cleanup_state(&state);
}

// Proves busy diagnostics retain exact samples through an independently current epoch, then halt on invalidation.
static void test_clock_continuity(void) {
    AppState state;
    char root[] = "tmp/recording-clock-gap-XXXXXX";
    prepare_state(&state, root);
    AudioDeviceSpec spec = {.sample_rate = 48000, .channels = 1, .block_size = 128};
    assert(daw_audio_recording_begin_take(&state, 0, 0, &spec));
    DawAudioRecordingState* recording = &state.audio_recording;
    recording->timeline_aligned = true;
    assert(daw_recording_worker_start(recording));
    assert(engine_transport_play(state.engine));
    EngineClockSnapshot clock;
    assert(engine_get_clock_snapshot(state.engine, &clock));
    clock.presentation_frame = 128;
    float samples[128];
    for (int i = 0; i < 128; ++i) samples[i] = .5f;
    assert(daw_audio_recording_enqueue_timed(recording, samples, 128, 1, 1, &clock) == 128);
    reject_capture_clock = true;
    daw_audio_recording_capture_callback(samples, 128, 1, recording);
    reject_capture_clock = false;
    clock.presentation_frame = 384;
    assert(daw_audio_recording_enqueue_timed(recording, samples, 128, 1, 3, &clock) == 128);
    assert(atomic_load(&recording->dropped_frames) == 0 && atomic_load(&recording->clock_missing_frames) == 0 &&
           atomic_load(&recording->clock_continuity_frames) == 128 &&
           atomic_load(&recording->queue_dropped_frames) == 0);
    atomic_store(&state.engine->clock_capture_epoch, 0);
    reject_capture_clock = true;
    daw_audio_recording_capture_callback(samples, 128, 1, recording);
    reject_capture_clock = false;
    assert(atomic_load(&recording->capture_halted) && atomic_load(&recording->captured_frames) == 384);
    DawAudioRecordingResult result;
    assert(daw_audio_recording_finish(&state, &result) && result.frame_count == 384);
    AudioMediaClip decoded = {0};
    assert(audio_media_clip_load_wav(result.wav_path, 48000, &decoded) && decoded.frame_count == 384);
    for (int i = 0; i < 384; ++i) assert(fabsf(decoded.samples[i] - .5f) < .0001f);
    audio_media_clip_free(&decoded);
    cleanup_state(&state);
}

int main(void) {
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    test_clock_continuity();
    test_worker_storage_failure();
    test_bounded_long_take();
    test_worker_without_ui_drain();
    test_live_aligned_capture();
    test_timed_gaps_and_publication_retry();
    test_record_arm_rejection_and_retry();
    test_deleted_recording_target();
    test_next_path_creates_recordings_directory();
    test_next_path_rejects_unsafe_library_copy_root();
    test_synthetic_take_writes_wav_and_inserts_audio_clip();
    test_empty_take_finish_reports_error_and_cancel_resets();
    test_begin_take_applies_target_role_policy();
    test_record_armed_solo_track_gates_backing_audio();
    test_capture_error_retry_quiesces_old_queue();
    SDL_Quit();
    puts("audio_recording_test: success");
    return 0;
}
