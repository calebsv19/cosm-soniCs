#include "app/audio_recording.h"

#include "app_state.h"
#include "audio/media_registry.h"
#include "audio/wav_writer.h"
#include "daw/data_paths.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#define DAW_AUDIO_RECORDING_QUEUE_SECONDS 10u
#define DAW_AUDIO_RECORDING_DRAIN_FRAMES 4096u

static void daw_audio_recording_set_status(DawAudioRecordingState* recording, const char* message) {
    if (!recording) {
        return;
    }
    if (!message) {
        recording->status_message[0] = '\0';
        return;
    }
    SDL_strlcpy(recording->status_message, message, sizeof(recording->status_message));
}

static bool recording_path_exists(const char* path) {
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0;
}

static bool recording_path_is_directory(const char* path) {
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool recording_ensure_directory_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char temp[SESSION_PATH_MAX];
    SDL_strlcpy(temp, path, sizeof(temp));
    for (char* p = temp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char hold = *p;
            *p = '\0';
#if defined(_WIN32)
            _mkdir(temp);
#else
            mkdir(temp, 0755);
#endif
            *p = hold;
        }
    }
#if defined(_WIN32)
    if (_mkdir(temp) == 0 || errno == EEXIST) {
        return true;
    }
#else
    if (mkdir(temp, 0755) == 0 || errno == EEXIST) {
        return true;
    }
#endif
    return recording_path_is_directory(path);
}

static const char* daw_audio_recording_root(const AppState* state) {
    if (!state) {
        return DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT;
    }
    if (state->data_paths.library_copy_root[0] != '\0') {
        return state->data_paths.library_copy_root;
    }
    if (state->data_paths.output_root[0] != '\0') {
        return state->data_paths.output_root;
    }
    return DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT;
}

static void daw_audio_recording_clear_take(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    free(recording->take_frames);
    recording->take_frames = NULL;
    recording->take_frame_count = 0;
    recording->take_frame_capacity = 0;
}

static bool daw_audio_recording_prepare_queue(DawAudioRecordingState* recording,
                                              int channels,
                                              uint64_t capacity_frames) {
    if (!recording || channels <= 0 || capacity_frames == 0) {
        return false;
    }
    if (recording->queue_ready) {
        audio_queue_free(&recording->capture_queue);
        recording->queue_ready = false;
    }
    if (!audio_queue_init(&recording->capture_queue, channels, (size_t)capacity_frames)) {
        daw_audio_recording_set_status(recording, "Failed to allocate audio recording queue.");
        return false;
    }
    recording->queue_ready = true;
    return true;
}

static bool daw_audio_recording_grow_take(DawAudioRecordingState* recording, uint64_t frame_count_needed) {
    if (!recording || frame_count_needed <= recording->take_frame_capacity) {
        return true;
    }
    uint64_t next = recording->take_frame_capacity > 0 ? recording->take_frame_capacity : 4096u;
    while (next < frame_count_needed) {
        if (next > UINT64_MAX / 2u) {
            return false;
        }
        next *= 2u;
    }
    if (recording->channels <= 0 || next > SIZE_MAX / (uint64_t)recording->channels / sizeof(float)) {
        return false;
    }
    float* resized = (float*)realloc(recording->take_frames,
                                     (size_t)next * (size_t)recording->channels * sizeof(float));
    if (!resized) {
        return false;
    }
    recording->take_frames = resized;
    recording->take_frame_capacity = next;
    return true;
}

static bool daw_audio_recording_session_clip_from_engine(const EngineClip* clip, SessionClip* out_clip) {
    if (!clip || !out_clip) {
        return false;
    }
    memset(out_clip, 0, sizeof(*out_clip));
    out_clip->kind = engine_clip_get_kind(clip);
    const char* media_id = engine_clip_get_media_id(clip);
    const char* media_path = engine_clip_get_media_path(clip);
    SDL_strlcpy(out_clip->media_id, media_id ? media_id : "", sizeof(out_clip->media_id));
    SDL_strlcpy(out_clip->media_path, media_path ? media_path : "", sizeof(out_clip->media_path));
    SDL_strlcpy(out_clip->name, clip->name, sizeof(out_clip->name));
    out_clip->start_frame = clip->timeline_start_frames;
    out_clip->duration_frames = clip->duration_frames;
    if (out_clip->duration_frames == 0 && clip->sampler) {
        out_clip->duration_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    out_clip->offset_frames = clip->offset_frames;
    out_clip->fade_in_frames = clip->fade_in_frames;
    out_clip->fade_out_frames = clip->fade_out_frames;
    out_clip->fade_in_curve = clip->fade_in_curve;
    out_clip->fade_out_curve = clip->fade_out_curve;
    out_clip->gain = clip->gain;
    out_clip->selected = false;
    out_clip->instrument_preset = engine_clip_midi_instrument_preset(clip);
    out_clip->instrument_params = engine_clip_midi_instrument_params(clip);
    out_clip->instrument_inherits_track = engine_clip_midi_inherits_track_instrument(clip);
    return true;
}

static void daw_audio_recording_push_insert_undo(AppState* state, int track_index, int clip_index) {
    if (!state || !state->engine || track_index < 0 || clip_index < 0) {
        return;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index >= track_count) {
        return;
    }
    const EngineTrack* track = &tracks[track_index];
    if (clip_index >= track->clip_count) {
        return;
    }
    const EngineClip* clip = &track->clips[clip_index];
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
    cmd.data.clip_add_remove.added = true;
    cmd.data.clip_add_remove.track_index = track_index;
    cmd.data.clip_add_remove.sampler = clip->sampler;
    if (!daw_audio_recording_session_clip_from_engine(clip, &cmd.data.clip_add_remove.clip)) {
        return;
    }
    (void)undo_manager_push(&state->undo, &cmd);
}

static int daw_audio_recording_resolve_timeline_track(const AppState* state) {
    if (!state || !state->engine) {
        return -1;
    }
    int track_count = engine_get_track_count(state->engine);
    if (track_count <= 0) {
        return -1;
    }
    if (state->selected_track_index >= 0 && state->selected_track_index < track_count) {
        return state->selected_track_index;
    }
    if (state->active_track_index >= 0 && state->active_track_index < track_count) {
        return state->active_track_index;
    }
    if (state->timeline_drop_track_index >= 0 && state->timeline_drop_track_index < track_count) {
        return state->timeline_drop_track_index;
    }
    return 0;
}

static void daw_audio_recording_capture_callback(const float* input,
                                                 int frames,
                                                 int channels,
                                                 void* userdata) {
    DawAudioRecordingState* recording = (DawAudioRecordingState*)userdata;
    if (!recording || !input || frames <= 0 || channels <= 0) {
        return;
    }
    size_t written = daw_audio_recording_enqueue_frames(recording, input, (size_t)frames, channels);
    if (written < (size_t)frames) {
        atomic_fetch_add_explicit(&recording->dropped_frames,
                                  (uint_fast64_t)((size_t)frames - written),
                                  memory_order_relaxed);
    }
}

void daw_audio_recording_init(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    memset(recording, 0, sizeof(*recording));
    recording->status = DAW_AUDIO_RECORDING_IDLE;
    recording->target_track_index = -1;
    atomic_init(&recording->dropped_frames, 0);
    audio_capture_device_init(&recording->capture_device);
}

void daw_audio_recording_free(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    daw_audio_recording_cancel(recording);
    daw_audio_recording_clear_take(recording);
}

bool daw_audio_recording_is_active(const DawAudioRecordingState* recording) {
    return recording && recording->status == DAW_AUDIO_RECORDING_ACTIVE;
}

const char* daw_audio_recording_status_message(const DawAudioRecordingState* recording) {
    if (!recording || recording->status_message[0] == '\0') {
        return "";
    }
    return recording->status_message;
}

bool daw_audio_recording_next_path(const AppState* state, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    const char* root = daw_audio_recording_root(state);
    char dir[SESSION_PATH_MAX];
    if (snprintf(dir, sizeof(dir), "%s/recordings", root) >= (int)sizeof(dir)) {
        return false;
    }
    if (!recording_ensure_directory_recursive(dir)) {
        return false;
    }
    for (int i = 0; i < 10000; ++i) {
        char name[64];
        if (i == 0) {
            snprintf(name, sizeof(name), "recording.wav");
        } else {
            snprintf(name, sizeof(name), "recording%d.wav", i);
        }
        if (snprintf(out, len, "%s/%s", dir, name) >= (int)len) {
            return false;
        }
        if (!recording_path_exists(out)) {
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

bool daw_audio_recording_begin_take(AppState* state,
                                    int track_index,
                                    uint64_t start_frame,
                                    const AudioDeviceSpec* desired) {
    if (!state || !state->engine || track_index < 0 || track_index >= engine_get_track_count(state->engine)) {
        return false;
    }
    DawAudioRecordingState* recording = &state->audio_recording;
    if (daw_audio_recording_is_active(recording)) {
        daw_audio_recording_set_status(recording, "Audio recording is already active.");
        return false;
    }

    AudioDeviceSpec spec = desired ? *desired : audio_capture_device_default_spec();
    if (state->runtime_cfg.sample_rate > 0 && (!desired || desired->sample_rate <= 0)) {
        spec.sample_rate = state->runtime_cfg.sample_rate;
    }
    if (spec.sample_rate <= 0) {
        spec.sample_rate = 48000;
    }
    if (spec.block_size <= 0) {
        spec.block_size = 512;
    }
    if (spec.channels <= 0) {
        spec.channels = 1;
    }

    uint64_t queue_frames = (uint64_t)spec.sample_rate * DAW_AUDIO_RECORDING_QUEUE_SECONDS;
    if (!daw_audio_recording_prepare_queue(recording, spec.channels, queue_frames)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        return false;
    }

    daw_audio_recording_clear_take(recording);
    recording->sample_rate = spec.sample_rate;
    recording->channels = spec.channels;
    recording->target_track_index = track_index;
    recording->start_frame = start_frame;
    recording->transport_started_by_recording = false;
    recording->record_armed_engine = state->engine;
    recording->pending_path[0] = '\0';
    atomic_store_explicit(&recording->dropped_frames, 0, memory_order_relaxed);
    recording->status = DAW_AUDIO_RECORDING_ACTIVE;
    (void)engine_set_record_armed_track(state->engine, track_index);
    daw_audio_recording_set_status(recording, "Audio recording active.");
    return true;
}

bool daw_audio_recording_begin_capture(AppState* state,
                                       int track_index,
                                       uint64_t start_frame,
                                       const char* device_name,
                                       const AudioDeviceSpec* desired) {
    if (!state || !state->engine) {
        return false;
    }
    DawAudioRecordingState* recording = &state->audio_recording;
    if (daw_audio_recording_is_active(recording)) {
        daw_audio_recording_set_status(recording, "Audio recording is already active.");
        return false;
    }

    AudioDeviceSpec want = desired ? *desired : audio_capture_device_default_spec();
    if (state->runtime_cfg.sample_rate > 0 && (!desired || desired->sample_rate <= 0)) {
        want.sample_rate = state->runtime_cfg.sample_rate;
    }
    if (!audio_capture_device_open(&recording->capture_device,
                                   device_name,
                                   &want,
                                   daw_audio_recording_capture_callback,
                                   recording)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, audio_capture_device_last_error(&recording->capture_device));
        return false;
    }
    recording->capture_device_open = true;

    AudioDeviceSpec have = recording->capture_device.spec;
    if (!daw_audio_recording_begin_take(state, track_index, start_frame, &have)) {
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
        return false;
    }

    if (!audio_capture_device_start(&recording->capture_device)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, audio_capture_device_last_error(&recording->capture_device));
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
        return false;
    }
    recording->capture_device_started = true;
    return true;
}

bool daw_audio_recording_begin_timeline_capture(AppState* state) {
    if (!state || !state->engine) {
        return false;
    }
    int track_index = daw_audio_recording_resolve_timeline_track(state);
    if (track_index < 0) {
        daw_audio_recording_set_status(&state->audio_recording, "Select a timeline track before recording.");
        return false;
    }
    AudioDeviceSpec desired = audio_capture_device_default_spec();
    if (state->runtime_cfg.sample_rate > 0) {
        desired.sample_rate = state->runtime_cfg.sample_rate;
    }
    if (state->runtime_cfg.block_size > 0) {
        desired.block_size = state->runtime_cfg.block_size;
    }
    desired.channels = 1;
    uint64_t start_frame = engine_get_transport_frame(state->engine);
    if (!daw_audio_recording_begin_capture(state, track_index, start_frame, NULL, &desired)) {
        return false;
    }
    state->audio_recording.transport_started_by_recording = false;
    state->active_track_index = track_index;
    state->selected_track_index = track_index;
    state->timeline_drop_track_index = track_index;
    return true;
}

bool daw_audio_recording_finish_timeline_capture(AppState* state, DawAudioRecordingResult* out_result) {
    if (!state || !state->engine) {
        return false;
    }
    bool stop_transport = state->audio_recording.transport_started_by_recording;
    bool ok = daw_audio_recording_finish(state, out_result);
    if (stop_transport) {
        (void)engine_transport_stop(state->engine);
    }
    if (!ok) {
        daw_audio_recording_cancel(&state->audio_recording);
        return false;
    }
    return true;
}

size_t daw_audio_recording_enqueue_frames(DawAudioRecordingState* recording,
                                          const float* input,
                                          size_t frames,
                                          int channels) {
    if (!recording || !recording->queue_ready || !input || frames == 0 || channels != recording->channels) {
        return 0;
    }
    return audio_queue_write(&recording->capture_queue, input, frames);
}

uint64_t daw_audio_recording_drain(DawAudioRecordingState* recording) {
    if (!recording || !recording->queue_ready || recording->channels <= 0) {
        return 0;
    }
    uint64_t drained_total = 0;
    float* temp = (float*)malloc(DAW_AUDIO_RECORDING_DRAIN_FRAMES *
                                 (size_t)recording->channels *
                                 sizeof(float));
    if (!temp) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to allocate audio recording drain buffer.");
        return 0;
    }

    while (audio_queue_available_frames(&recording->capture_queue) > 0) {
        size_t wanted = audio_queue_available_frames(&recording->capture_queue);
        if (wanted > DAW_AUDIO_RECORDING_DRAIN_FRAMES) {
            wanted = DAW_AUDIO_RECORDING_DRAIN_FRAMES;
        }
        size_t read = audio_queue_read(&recording->capture_queue, temp, wanted);
        if (read == 0) {
            break;
        }
        uint64_t needed = recording->take_frame_count + (uint64_t)read;
        if (!daw_audio_recording_grow_take(recording, needed)) {
            recording->status = DAW_AUDIO_RECORDING_ERROR;
            daw_audio_recording_set_status(recording, "Failed to grow audio recording take buffer.");
            break;
        }
        memcpy(recording->take_frames + recording->take_frame_count * (uint64_t)recording->channels,
               temp,
               read * (size_t)recording->channels * sizeof(float));
        recording->take_frame_count = needed;
        drained_total += (uint64_t)read;
    }

    free(temp);
    return drained_total;
}

uint64_t daw_audio_recording_drain_if_transport_playing(AppState* state) {
    if (!state || !state->engine || !daw_audio_recording_is_active(&state->audio_recording)) {
        return 0;
    }
    if (!engine_transport_is_playing(state->engine)) {
        if (state->audio_recording.queue_ready) {
            audio_queue_clear(&state->audio_recording.capture_queue);
        }
        return 0;
    }
    return daw_audio_recording_drain(&state->audio_recording);
}

bool daw_audio_recording_take_clip_view(const DawAudioRecordingState* recording, AudioMediaClip* out_clip) {
    if (!out_clip) {
        return false;
    }
    memset(out_clip, 0, sizeof(*out_clip));
    if (!recording || recording->status != DAW_AUDIO_RECORDING_ACTIVE ||
        !recording->take_frames || recording->take_frame_count == 0 ||
        recording->channels <= 0 || recording->sample_rate <= 0) {
        return false;
    }
    out_clip->samples = recording->take_frames;
    out_clip->frame_count = recording->take_frame_count;
    out_clip->channels = recording->channels;
    out_clip->sample_rate = recording->sample_rate;
    return true;
}

bool daw_audio_recording_finish(AppState* state, DawAudioRecordingResult* out_result) {
    if (out_result) {
        memset(out_result, 0, sizeof(*out_result));
        out_result->track_index = -1;
        out_result->clip_index = -1;
    }
    if (!state || !state->engine) {
        return false;
    }
    DawAudioRecordingState* recording = &state->audio_recording;
    if (!daw_audio_recording_is_active(recording)) {
        daw_audio_recording_set_status(recording, "No active audio recording to finish.");
        return false;
    }
    if (recording->capture_device_started) {
        audio_capture_device_stop(&recording->capture_device);
        recording->capture_device_started = false;
    }
    (void)daw_audio_recording_drain_if_transport_playing(state);

    if (recording->take_frame_count == 0 || !recording->take_frames) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Audio recording produced no frames.");
        return false;
    }

    char path[SESSION_PATH_MAX];
    if (!daw_audio_recording_next_path(state, path, sizeof(path))) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to allocate audio recording output path.");
        return false;
    }
    if (!wav_write_pcm16_dithered(path,
                                  recording->take_frames,
                                  recording->take_frame_count,
                                  recording->channels,
                                  recording->sample_rate,
                                  0xA23u)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to write recorded audio WAV.");
        return false;
    }

    MediaRegistryEntry media_entry = {0};
    const char* media_id = NULL;
    if (media_registry_ensure_for_path(&state->media_registry, path, "Recording", &media_entry)) {
        media_id = media_entry.id[0] != '\0' ? media_entry.id : NULL;
        (void)media_registry_save(&state->media_registry);
    } else {
        SDL_Log("Audio recording warning: failed to register %s; adding clip by path only.", path);
    }

    int clip_index = -1;
    if (!engine_add_clip_to_track_with_id(state->engine,
                                          recording->target_track_index,
                                          path,
                                          media_id,
                                          recording->start_frame,
                                          &clip_index)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to insert recorded audio clip.");
        return false;
    }
    daw_audio_recording_push_insert_undo(state, recording->target_track_index, clip_index);

    state->selection_count = 1;
    state->selection[0].track_index = recording->target_track_index;
    state->selection[0].clip_index = clip_index;
    state->active_track_index = recording->target_track_index;
    state->selected_track_index = recording->target_track_index;
    state->selected_clip_index = clip_index;
    state->timeline_drop_track_index = recording->target_track_index;

    if (out_result) {
        out_result->inserted = true;
        SDL_strlcpy(out_result->wav_path, path, sizeof(out_result->wav_path));
        out_result->track_index = recording->target_track_index;
        out_result->clip_index = clip_index;
        out_result->frame_count = recording->take_frame_count;
        out_result->sample_rate = recording->sample_rate;
        out_result->channels = recording->channels;
    }

    SDL_strlcpy(recording->pending_path, path, sizeof(recording->pending_path));
    daw_audio_recording_cancel(recording);
    daw_audio_recording_set_status(recording, "Audio recording finished.");
    return true;
}

void daw_audio_recording_cancel(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    if (recording->record_armed_engine) {
        (void)engine_set_record_armed_track(recording->record_armed_engine, -1);
        recording->record_armed_engine = NULL;
    }
    if (recording->capture_device_started) {
        audio_capture_device_stop(&recording->capture_device);
        recording->capture_device_started = false;
    }
    if (recording->capture_device_open || recording->capture_device.is_open) {
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
    }
    if (recording->queue_ready) {
        audio_queue_free(&recording->capture_queue);
        recording->queue_ready = false;
    }
    daw_audio_recording_clear_take(recording);
    recording->status = DAW_AUDIO_RECORDING_IDLE;
    recording->transport_started_by_recording = false;
    recording->record_armed_engine = NULL;
    recording->sample_rate = 0;
    recording->channels = 0;
    recording->target_track_index = -1;
    recording->start_frame = 0;
    atomic_store_explicit(&recording->dropped_frames, 0, memory_order_relaxed);
}
