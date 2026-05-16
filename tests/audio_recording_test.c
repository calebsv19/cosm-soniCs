#include "app/audio_recording.h"

#include "audio/wav_writer.h"
#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "session.h"
#include "undo/undo_manager.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    engine_bounce_buffer_free(&before);

    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 128,
        .channels = 1
    };
    assert(daw_audio_recording_begin_take(&state, record_track, 0, &spec));

    EngineBounceBuffer armed = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &armed));
    assert(average_abs_bounce(&armed) < 0.001f);
    engine_bounce_buffer_free(&armed);

    assert(engine_track_set_solo(state.engine, 0, true));
    EngineBounceBuffer backing_soloed = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &backing_soloed));
    assert(average_abs_bounce(&backing_soloed) > 0.1f);
    engine_bounce_buffer_free(&backing_soloed);

    daw_audio_recording_cancel(&state.audio_recording);
    EngineBounceBuffer after = {0};
    assert(engine_bounce_range_to_buffer(state.engine, 0, 512, NULL, NULL, &after));
    assert(average_abs_bounce(&after) > 0.1f);
    engine_bounce_buffer_free(&after);

    cleanup_state(&state);
}

int main(void) {
    test_next_path_creates_recordings_directory();
    test_synthetic_take_writes_wav_and_inserts_audio_clip();
    test_record_armed_solo_track_gates_backing_audio();
    puts("audio_recording_test: success");
    return 0;
}
