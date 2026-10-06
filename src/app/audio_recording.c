#include "app/media_import.h"
#include "app/audio_recording.h"

#include "app_state.h"
#include "audio/media_registry.h"
#include "audio/wav_writer.h"
#include "daw/data_paths.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "engine/track_role.h"
#include "input/timeline_selection.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core_time.h"

#define DAW_AUDIO_RECORDING_QUEUE_SECONDS 10u
#define DAW_AUDIO_RECORDING_DRAIN_FRAMES 4096u
#define DAW_CAPTURE_PACKET_FRAMES 256u
#define DAW_CAPTURE_MAX_CHANNELS 8u

// Transfers sample-clock position and capture-time transport context with one complete audio block.
typedef struct DawCapturePacket {
    uint64_t first_frame, timestamp_ns, project_frame;
    uint32_t frames;
    bool timed;
    float samples[DAW_CAPTURE_PACKET_FRAMES * DAW_CAPTURE_MAX_CHANNELS];
} DawCapturePacket;

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

static const char* daw_audio_recording_root(const AppState* state) {
    if (!state) {
        return DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT;
    }
    if (daw_data_path_is_safe_write_root(state->data_paths.library_copy_root)) {
        return state->data_paths.library_copy_root;
    }
    if (daw_data_path_is_safe_write_root(state->data_paths.output_root)) {
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
        ringbuf_free(&recording->capture_packets);
        recording->queue_ready = false;
    }
    if (channels > (int)DAW_CAPTURE_MAX_CHANNELS ||
        !ringbuf_init(&recording->capture_packets,
                     (size_t)(capacity_frames / DAW_CAPTURE_PACKET_FRAMES + 1) * sizeof(DawCapturePacket))) {
        daw_audio_recording_set_status(recording, "Failed to allocate audio recording packet queue.");
        return false;
    }
    recording->queue_ready = true;
    return true;
}

// Reserves a fixed recent-sample preview; the complete take lives only in its journal.
static bool daw_audio_recording_grow_take(DawAudioRecordingState* recording, uint64_t needed) {
    (void)needed;
    if (recording->take_frames) return true;
    recording->take_frames = calloc(DAW_RECORDING_PREVIEW_FRAMES * recording->channels, sizeof(float));
    if (!recording->take_frames) return false;
    recording->take_frame_capacity = DAW_RECORDING_PREVIEW_FRAMES;
    return true;
}

// Owns journal I/O and publishes bounded snapshots without sharing editable UI state.
typedef struct DawRecordingWorker {
    DawAudioRecordingState* owner;
    DawAudioRecordingState drain, snapshot;
    SDL_Thread* thread;
    SDL_mutex* mutex;
    atomic_bool stop;
} DawRecordingWorker;

static bool daw_recording_worker_start(DawAudioRecordingState* recording);
static void daw_recording_worker_stop(DawAudioRecordingState* recording);
static uint64_t daw_recording_worker_poll(DawAudioRecordingState* recording);

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

static void daw_audio_recording_set_active_status_for_track(DawAudioRecordingState* recording,
                                                            const Engine* engine,
                                                            int track_index) {
    if (!recording) {
        return;
    }
    EngineTrackRole role = ENGINE_TRACK_ROLE_EMPTY;
    if (engine_track_role_resolve(engine, track_index, &role)) {
        char message[128];
        snprintf(message,
                 sizeof(message),
                 "Audio recording active on %s track %d.",
                 engine_track_role_label(role),
                 track_index + 1);
        daw_audio_recording_set_status(recording, message);
        return;
    }
    daw_audio_recording_set_status(recording, "Audio recording active.");
}

static bool daw_audio_recording_target_allows_audio(DawAudioRecordingState* recording,
                                                    const Engine* engine,
                                                    int track_index) {
    EngineTrackRole role = ENGINE_TRACK_ROLE_EMPTY;
    if (!engine_track_role_resolve(engine, track_index, &role)) {
        return false;
    }
    if (role == ENGINE_TRACK_ROLE_MIDI) {
        char message[160];
        snprintf(message,
                 sizeof(message),
                 "Audio recording needs an empty, audio, or mixed track; track %d is MIDI-only.",
                 track_index + 1);
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, message);
        return false;
    }
    return true;
}

static void daw_audio_recording_capture_callback(const float* input,
                                                 int frames,
                                                 int channels,
                                                 void* userdata) {
    DawAudioRecordingState* recording = (DawAudioRecordingState*)userdata;
    if (!recording || !input || frames <= 0 || channels <= 0 || atomic_load(&recording->capture_halted)) {
        return;
    }
    if (recording->timeline_aligned) {
        EngineClockSnapshot clock;
        if (!engine_get_clock_snapshot(recording->record_armed_engine, &clock)) {
            if (recording->producer_has_epoch) {
                if (engine_capture_epoch_is_current(recording->record_armed_engine, recording->producer_epoch)) {
                    size_t accepted = daw_audio_recording_enqueue_frames(recording, input, (size_t)frames, channels);
                    atomic_fetch_add(&recording->clock_continuity_frames, accepted);
                } else {
                    atomic_store(&recording->capture_halted, true);
                }
            }
            return;
        }
        (void)daw_audio_recording_enqueue_timed(recording, input, (size_t)frames, channels, core_time_now_ns(), &clock);
    } else {
        (void)daw_audio_recording_enqueue_frames(recording, input, (size_t)frames, channels);
    }
}

void daw_audio_recording_init(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    memset(recording, 0, sizeof(*recording));
    recording->status = DAW_AUDIO_RECORDING_IDLE;
    recording->target_track_index = -1;
    recording->target_track_id = 0;
    atomic_init(&recording->dropped_frames, 0);
    atomic_init(&recording->queue_dropped_frames, 0);
    atomic_init(&recording->clock_missing_frames, 0);
    atomic_init(&recording->clock_continuity_frames, 0);
    atomic_init(&recording->captured_frames, 0);
    atomic_init(&recording->captured_packets, 0);
    atomic_init(&recording->first_capture_ns, 0);
    atomic_init(&recording->last_capture_ns, 0);
    atomic_init(&recording->capture_halted, false);
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
    return recording && (recording->status == DAW_AUDIO_RECORDING_ACTIVE ||
                         (recording->status == DAW_AUDIO_RECORDING_ERROR && recording->take_frame_count > 0));
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
    if (!daw_data_path_ensure_directory_recursive(dir)) {
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
        if (!daw_data_path_exists(out)) {
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
        SDL_Log("audio_recording: begin_take failed reason=invalid_state track=%d track_count=%d",
                track_index,
                (state && state->engine) ? engine_get_track_count(state->engine) : -1);
        return false;
    }
    DawAudioRecordingState* recording = &state->audio_recording;
    if (daw_audio_recording_is_active(recording)) {
        daw_audio_recording_set_status(recording, "Audio recording is already active.");
        SDL_Log("audio_recording: begin_take failed reason=already_active track=%d", track_index);
        return false;
    }
    if (!daw_audio_recording_target_allows_audio(recording, state->engine, track_index)) {
        SDL_Log("audio_recording: begin_take failed reason=target_policy track=%d", track_index);
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

    if (spec.channels > (int)DAW_CAPTURE_MAX_CHANNELS || spec.sample_rate != engine_get_config(state->engine)->sample_rate) {
        daw_audio_recording_set_status(recording, "Capture must use the project sample rate and 1-8 channels.");
        return false;
    }
    // Quiesce a failed take's callback before replacing its queue or channel metadata.
    if (recording->capture_device_started || recording->worker) daw_audio_recording_cancel(recording);
    uint64_t queue_frames = (uint64_t)spec.sample_rate * DAW_AUDIO_RECORDING_QUEUE_SECONDS;
    if (!daw_audio_recording_prepare_queue(recording, spec.channels, queue_frames)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        SDL_Log("audio_recording: begin_take failed reason=queue_prepare sample_rate=%d channels=%d queue_frames=%llu",
                spec.sample_rate,
                spec.channels,
                (unsigned long long)queue_frames);
        return false;
    }

    daw_audio_recording_clear_take(recording);
    if (!engine_set_record_armed_track(state->engine, track_index)) {
        ringbuf_free(&recording->capture_packets);
        recording->queue_ready = false;
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Could not prepare the recording track. Please retry.");
        return false;
    }
    recording->sample_rate = spec.sample_rate;
    recording->channels = spec.channels;
    recording->target_track_index = track_index;
    recording->target_track_id = engine_get_tracks(state->engine)[track_index].runtime_id;
    recording->start_frame = start_frame;
    recording->transport_started_by_recording = false;
    recording->record_armed_engine = state->engine;
    recording->pending_path[0] = '\0';
    atomic_store_explicit(&recording->dropped_frames, 0, memory_order_relaxed);
    atomic_store(&recording->queue_dropped_frames, 0);
    atomic_store(&recording->clock_missing_frames, 0);
    atomic_store(&recording->clock_continuity_frames, 0);
    recording->timeline_aligned = false;
    recording->producer_has_epoch = false;
    recording->producer_epoch = 0;
    recording->next_capture_frame = recording->gap_frames = recording->checkpoint_frames = 0;
    recording->alignment_error_frames = 0;
    recording->drain_failed = false;
    atomic_store(&recording->captured_frames, 0);
    atomic_store(&recording->captured_packets, 0);
    atomic_store(&recording->first_capture_ns, 0);
    atomic_store(&recording->last_capture_ns, 0);
    atomic_store(&recording->capture_halted, false);
    char directory[SESSION_PATH_MAX];
    if (!daw_audio_recording_next_path(state, directory, sizeof(directory))) {
        daw_audio_recording_cancel(recording);
        daw_audio_recording_set_status(recording, "Cannot create a recoverable recording checkpoint.");
        return false;
    }
    char* slash = strrchr(directory, '/');
    if (slash) *slash = 0;
    (void)daw_take_journal_close(&recording->journal);
    if (!daw_take_journal_open(&recording->journal, directory, spec.sample_rate, spec.channels, start_frame)) {
        daw_audio_recording_cancel(recording);
        daw_audio_recording_set_status(recording, "Cannot create a recoverable recording checkpoint.");
        return false;
    }
    recording->status = DAW_AUDIO_RECORDING_ACTIVE;
    SDL_Log("audio_recording: recovery journal=%s", recording->journal.path);
    daw_audio_recording_set_active_status_for_track(recording, state->engine, track_index);
    SDL_Log("audio_recording: begin_take ok track=%d start_frame=%llu sample_rate=%d channels=%d queue_frames=%llu",
            track_index,
            (unsigned long long)start_frame,
            spec.sample_rate,
            spec.channels,
            (unsigned long long)queue_frames);
    return true;
}

// Prepares all callback-owned fields before starting raw or software-aligned capture.
static bool daw_audio_recording_begin_capture_impl(AppState* state,
                                       int track_index,
                                       uint64_t start_frame,
                                       const char* device_name,
                                       const AudioDeviceSpec* desired, bool aligned) {
    if (!state || !state->engine) {
        return false;
    }
    DawAudioRecordingState* recording = &state->audio_recording;
    if (daw_audio_recording_is_active(recording)) {
        daw_audio_recording_set_status(recording, "Audio recording is already active.");
        SDL_Log("audio_recording: begin_capture failed reason=already_active track=%d", track_index);
        return false;
    }
    if (track_index >= 0 && track_index < engine_get_track_count(state->engine) &&
        !daw_audio_recording_target_allows_audio(recording, state->engine, track_index)) {
        SDL_Log("audio_recording: begin_capture failed reason=target_policy track=%d", track_index);
        return false;
    }

    AudioDeviceSpec want = desired ? *desired : audio_capture_device_default_spec();
    if (state->runtime_cfg.sample_rate > 0 && (!desired || desired->sample_rate <= 0)) {
        want.sample_rate = state->runtime_cfg.sample_rate;
    }
    // A failed drain may leave capture active; fully retire it before opening its replacement.
    if (recording->capture_device_started || recording->capture_device_open || recording->capture_device.is_open) {
        daw_audio_recording_cancel(recording);
    }
    SDL_Log("audio_recording: begin_capture request track=%d start_frame=%llu device=%s sample_rate=%d channels=%d block_size=%d",
            track_index,
            (unsigned long long)start_frame,
            (device_name && device_name[0] != '\0') ? device_name : "default",
            want.sample_rate,
            want.channels,
            want.block_size);
    if (!audio_capture_device_open(&recording->capture_device,
                                   device_name,
                                   &want,
                                   daw_audio_recording_capture_callback,
                                   recording)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, audio_capture_device_last_error(&recording->capture_device));
        SDL_Log("audio_recording: begin_capture failed stage=open track=%d reason=%s",
                track_index,
                audio_capture_device_last_error(&recording->capture_device));
        return false;
    }
    recording->capture_device_open = true;

    AudioDeviceSpec have = recording->capture_device.spec;
    if (!daw_audio_recording_begin_take(state, track_index, start_frame, &have)) {
        SDL_Log("audio_recording: begin_capture failed stage=begin_take track=%d", track_index);
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
        return false;
    }

    recording->timeline_aligned = aligned;
    if (aligned && !daw_recording_worker_start(recording)) {
        daw_audio_recording_cancel(recording);
        daw_audio_recording_set_status(recording, "Cannot start recording storage worker.");
        return false;
    }
    if (!audio_capture_device_start(&recording->capture_device)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, audio_capture_device_last_error(&recording->capture_device));
        SDL_Log("audio_recording: begin_capture failed stage=start track=%d reason=%s",
                track_index,
                audio_capture_device_last_error(&recording->capture_device));
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
        char error[sizeof(recording->status_message)];
        SDL_strlcpy(error, recording->status_message, sizeof(error));
        daw_audio_recording_cancel(recording);
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, error);
        return false;
    }
    recording->capture_device_started = true;
    SDL_Log("audio_recording: begin_capture ok track=%d device=%s sample_rate=%d channels=%d block_size=%d",
            track_index,
            recording->capture_device.name,
            recording->capture_device.spec.sample_rate,
            recording->capture_device.spec.channels,
            recording->capture_device.spec.block_size);
    return true;
}

// Opens raw capture for callers that explicitly manage their own sample-clock placement.
bool daw_audio_recording_begin_capture(AppState* state, int track, uint64_t start, const char* name,
                                       const AudioDeviceSpec* desired) {
    return daw_audio_recording_begin_capture_impl(state, track, start, name, desired, false);
}

bool daw_audio_recording_begin_timeline_capture(AppState* state) {
    if (!state || !state->engine) {
        return false;
    }
    int track_index = daw_audio_recording_resolve_timeline_track(state);
    if (track_index < 0) {
        daw_audio_recording_set_status(&state->audio_recording, "Select a timeline track before recording.");
        SDL_Log("audio_recording: begin_timeline_capture failed reason=no_track_focus");
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
    EngineClockSnapshot clock;
    if (!engine_get_clock_snapshot(state->engine, &clock) || clock.loop_enabled || state->loop_enabled) {
        daw_audio_recording_set_status(&state->audio_recording, "Disable looping before recording a linear take.");
        return false;
    }
    uint64_t start_frame = clock.presentation_frame;
    if (!daw_audio_recording_begin_capture_impl(state, track_index, start_frame, NULL, &desired, true)) {
        SDL_Log("audio_recording: begin_timeline_capture failed track=%d start_frame=%llu",
                track_index,
                (unsigned long long)start_frame);
        return false;
    }
    state->audio_recording.transport_started_by_recording = false;
    timeline_selection_set_track_focus(state, track_index);
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
        SDL_Log("audio_recording: finish_timeline_capture failed stop_transport=%d", stop_transport ? 1 : 0);
        return false; // Keep the take and its recovery checkpoint available for retry.
    }
    return true;
}

// Publishes complete bounded packets and advances the sample clock even when capacity rejects data.
static size_t daw_recording_enqueue(DawAudioRecordingState* recording, const float* input, size_t frames,
                                    int channels, uint64_t timestamp, bool timed, uint64_t project) {
    if (!recording || !recording->queue_ready || !input || !frames || channels != recording->channels ||
        atomic_load(&recording->capture_halted)) return 0;
    uint64_t first = atomic_fetch_add(&recording->captured_frames, frames);
    if (!atomic_load(&recording->first_capture_ns)) atomic_store(&recording->first_capture_ns, timestamp);
    atomic_store(&recording->last_capture_ns, timestamp);
    size_t accepted = 0;
    for (size_t offset = 0; offset < frames;) {
        size_t count = frames - offset;
        if (count > DAW_CAPTURE_PACKET_FRAMES) count = DAW_CAPTURE_PACKET_FRAMES;
        DawCapturePacket packet = {.first_frame = first + offset, .timestamp_ns = timestamp,
            .project_frame = project + offset, .frames = (uint32_t)count, .timed = timed};
        memcpy(packet.samples, input + offset * channels, count * channels * sizeof(float));
        if (ringbuf_write_exact(&recording->capture_packets, &packet, sizeof(packet))) {
            accepted += count;
            atomic_fetch_add(&recording->captured_packets, 1);
        } else {
            atomic_fetch_add(&recording->dropped_frames, count);
            atomic_fetch_add(&recording->queue_dropped_frames, count);
        }
        offset += count;
    }
    return accepted;
}

// Supports synthetic/raw producers whose caller owns transport gating and placement.
size_t daw_audio_recording_enqueue_frames(DawAudioRecordingState* recording, const float* input, size_t frames, int channels) {
    return daw_recording_enqueue(recording, input, frames, channels, core_time_now_ns(), false, 0);
}

// Anchors one linear take to capture-time output observations and halts instead of concatenating transport jumps.
size_t daw_audio_recording_enqueue_timed(DawAudioRecordingState* recording, const float* input, size_t frames,
                                         int channels, uint64_t timestamp, const EngineClockSnapshot* clock) {
    if (!recording || !clock || !frames) return 0;
    if (recording->producer_has_epoch && (clock->epoch != recording->producer_epoch || !clock->playing || clock->loop_enabled)) {
        atomic_store(&recording->capture_halted, true);
        return 0;
    }
    if (!clock->playing || clock->loop_enabled) return 0;
    recording->producer_has_epoch = true;
    recording->producer_epoch = clock->epoch;
    uint64_t project = clock->presentation_frame > frames ? clock->presentation_frame - frames : 0;
    return daw_recording_enqueue(recording, input, frames, channels, timestamp, true, project);
}

// Appends owned preview data and journal records together, preserving prior checkpoints on failure.
static bool daw_recording_append(DawAudioRecordingState* recording, const float* samples, uint32_t frames, uint64_t timestamp) {
    uint64_t needed = recording->take_frame_count + frames;
    if (!daw_audio_recording_grow_take(recording, needed) ||
        !daw_take_journal_append(&recording->journal, samples, frames, timestamp)) return false;
    uint64_t retained = recording->take_frame_count < recording->take_frame_capacity
                            ? recording->take_frame_count : recording->take_frame_capacity;
    if (retained + frames > recording->take_frame_capacity) {
        uint64_t keep = recording->take_frame_capacity - frames;
        memmove(recording->take_frames, recording->take_frames + (retained - keep) * recording->channels,
                keep * recording->channels * sizeof(float));
        retained = keep;
    }
    memcpy(recording->take_frames + retained * recording->channels,
           samples, (size_t)frames * recording->channels * sizeof(float));
    recording->take_frame_count = needed;
    return true;
}

// Preserves missing source-clock intervals as silence rather than shortening the take.
static bool daw_recording_fill_gap(DawAudioRecordingState* recording, uint64_t frames, uint64_t timestamp) {
    float silence[DAW_AUDIO_RECORDING_DRAIN_FRAMES * DAW_CAPTURE_MAX_CHANNELS] = {0};
    while (frames) {
        uint32_t count = frames > DAW_AUDIO_RECORDING_DRAIN_FRAMES ? DAW_AUDIO_RECORDING_DRAIN_FRAMES : (uint32_t)frames;
        if (!daw_recording_append(recording, silence, count, timestamp)) return false;
        recording->gap_frames += count;
        recording->next_capture_frame += count;
        frames -= count;
    }
    return true;
}

// Drains capture-time packets and synchronizes their checkpoint without allocating on the callback thread.
static uint64_t daw_recording_drain_queue(DawAudioRecordingState* recording, RingBuffer* queue) {
    if (!recording || !recording->queue_ready || recording->drain_failed) return 0;
    uint64_t prior = recording->take_frame_count;
    DawCapturePacket packet;
    size_t budget = ringbuf_available_read(queue) / sizeof(packet);
    while (budget-- && ringbuf_read_exact(queue, &packet, sizeof(packet))) {
        if (packet.timed && recording->take_frame_count == 0) {
            uint64_t origin = packet.project_frame >= packet.first_frame ? packet.project_frame - packet.first_frame : 0;
            recording->start_frame = origin;
            if (!daw_take_journal_set_start(&recording->journal, origin)) { recording->drain_failed = true; break; }
        }
        if (packet.first_frame < recording->next_capture_frame) continue;
        if (!daw_recording_fill_gap(recording, packet.first_frame - recording->next_capture_frame, packet.timestamp_ns) ||
            !daw_recording_append(recording, packet.samples, packet.frames, packet.timestamp_ns)) {
            recording->drain_failed = true; break;
        }
        if (packet.timed) recording->alignment_error_frames = (int64_t)packet.project_frame -
            (int64_t)(recording->start_frame + recording->take_frame_count - packet.frames);
        recording->next_capture_frame = packet.first_frame + packet.frames;
    }
    if (recording->take_frame_count > prior && !daw_take_journal_sync(&recording->journal)) recording->drain_failed = true;
    if (recording->drain_failed) {
        atomic_store(&recording->capture_halted, true);
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Recording checkpoint failed; retained journal requires recovery.");
    } else {
        recording->checkpoint_frames = recording->take_frame_count;
        if (atomic_load(&recording->capture_halted))
            daw_audio_recording_set_status(recording, "Transport changed; finish this retained take before recording another.");
        else if (recording->status == DAW_AUDIO_RECORDING_ACTIVE) {
            snprintf(recording->status_message, sizeof(recording->status_message),
                "Recording: %.1fs saved | missing %llu frames | alignment %+lld frames",
                recording->sample_rate > 0 ? (double)recording->checkpoint_frames / recording->sample_rate : 0,
                (unsigned long long)atomic_load(&recording->dropped_frames),
                (long long)recording->alignment_error_frames);
        }
    }
    return recording->take_frame_count - prior;
}

// Copies only worker-owned progress and recent preview data at a publication boundary.
static void daw_recording_copy_snapshot(DawAudioRecordingState* dst, const DawAudioRecordingState* src) {
    dst->take_frame_count = src->take_frame_count;
    dst->next_capture_frame = src->next_capture_frame;
    dst->gap_frames = src->gap_frames;
    dst->checkpoint_frames = src->checkpoint_frames;
    dst->alignment_error_frames = src->alignment_error_frames;
    dst->start_frame = src->start_frame;
    dst->drain_failed = src->drain_failed;
    dst->status = src->status;
    memcpy(dst->status_message, src->status_message, sizeof(dst->status_message));
    size_t count = src->take_frame_count < src->take_frame_capacity ? src->take_frame_count : src->take_frame_capacity;
    if (count) memcpy(dst->take_frames, src->take_frames, count * src->channels * sizeof(float));
}

// Drains the SPSC ring on a storage worker and never holds the publication lock during disk I/O.
static int daw_recording_worker_run(void* user) {
    DawRecordingWorker* worker = user;
    do {
        bool stopping = atomic_load(&worker->stop);
        atomic_store(&worker->drain.capture_halted, atomic_load(&worker->owner->capture_halted));
        atomic_store(&worker->drain.dropped_frames, atomic_load(&worker->owner->dropped_frames));
        daw_recording_drain_queue(&worker->drain, &worker->owner->capture_packets);
        if (worker->drain.drain_failed) atomic_store(&worker->owner->capture_halted, true);
        SDL_LockMutex(worker->mutex);
        daw_recording_copy_snapshot(&worker->snapshot, &worker->drain);
        SDL_UnlockMutex(worker->mutex);
        if (stopping) break;
        if (!atomic_load(&worker->stop)) SDL_Delay(10);
    } while (true);
    return 0;
}

// Starts journal ownership transfer only after all fixed preview buffers have been allocated.
static bool daw_recording_worker_start(DawAudioRecordingState* recording) {
    DawRecordingWorker* worker = calloc(1, sizeof(*worker));
    if (!worker) return false;
    worker->owner = recording;
    daw_audio_recording_init(&worker->drain);
    daw_audio_recording_init(&worker->snapshot);
    worker->drain.channels = worker->snapshot.channels = recording->channels;
    worker->drain.sample_rate = recording->sample_rate;
    worker->drain.start_frame = recording->start_frame;
    worker->drain.status = DAW_AUDIO_RECORDING_ACTIVE;
    worker->drain.queue_ready = true;
    atomic_init(&worker->stop, false);
    worker->mutex = SDL_CreateMutex();
    if (!worker->mutex || !daw_audio_recording_grow_take(recording, 1) ||
        !daw_audio_recording_grow_take(&worker->drain, 1) ||
        !daw_audio_recording_grow_take(&worker->snapshot, 1)) goto failed;
    daw_recording_copy_snapshot(&worker->snapshot, recording);
    worker->drain.journal = recording->journal;
    worker->thread = SDL_CreateThread(daw_recording_worker_run, "take-journal", worker);
    if (!worker->thread) goto failed;
    recording->journal.file = NULL;
    recording->worker = worker;
    return true;
failed:
    if (worker->mutex) SDL_DestroyMutex(worker->mutex);
    daw_audio_recording_clear_take(&worker->drain);
    daw_audio_recording_clear_take(&worker->snapshot);
    free(worker);
    return false;
}

// Copies a ready snapshot without making the UI wait for journal writes or synchronization.
static uint64_t daw_recording_worker_poll(DawAudioRecordingState* recording) {
    DawRecordingWorker* worker = recording->worker;
    if (SDL_TryLockMutex(worker->mutex) != 0) return 0;
    uint64_t prior = recording->take_frame_count;
    daw_recording_copy_snapshot(recording, &worker->snapshot);
    SDL_UnlockMutex(worker->mutex);
    return recording->take_frame_count - prior;
}

// Joins the sole consumer before transferring the journal back for finalization or cancellation.
static void daw_recording_worker_stop(DawAudioRecordingState* recording) {
    DawRecordingWorker* worker = recording->worker;
    if (!worker) return;
    atomic_store(&worker->stop, true);
    SDL_WaitThread(worker->thread, NULL);
    daw_recording_copy_snapshot(recording, &worker->drain);
    recording->journal = worker->drain.journal;
    recording->worker = NULL;
    SDL_DestroyMutex(worker->mutex);
    daw_audio_recording_clear_take(&worker->drain);
    daw_audio_recording_clear_take(&worker->snapshot);
    free(worker);
}

// Polls production storage work or synchronously drains explicitly managed synthetic/raw takes.
uint64_t daw_audio_recording_drain(DawAudioRecordingState* recording) {
    if (!recording) return 0;
    if (recording->worker) return daw_recording_worker_poll(recording);
    return daw_recording_drain_queue(recording, &recording->capture_packets);
}

// Uses capture-time gating for aligned takes; raw/synthetic callers retain their explicit UI gate.
uint64_t daw_audio_recording_drain_if_transport_playing(AppState* state) {
    if (!state || !state->engine || !daw_audio_recording_is_active(&state->audio_recording)) return 0;
    DawAudioRecordingState* recording = &state->audio_recording;
    if (!recording->timeline_aligned && !engine_transport_is_playing(state->engine)) {
        DawCapturePacket packet;
        size_t budget = ringbuf_available_read(&recording->capture_packets) / sizeof(packet);
        while (budget-- && ringbuf_read_exact(&recording->capture_packets, &packet, sizeof(packet)))
            recording->next_capture_frame = packet.first_frame + packet.frames;
        return 0;
    }
    return daw_audio_recording_drain(recording);
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
    out_clip->frame_count = recording->take_frame_count < recording->take_frame_capacity
                                ? recording->take_frame_count : recording->take_frame_capacity;
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
        SDL_Log("audio_recording: finish failed reason=not_active");
        return false;
    }
    if (recording->capture_device_started) {
        audio_capture_device_stop(&recording->capture_device);
        recording->capture_device_started = false;
    }
    daw_recording_worker_stop(recording);
    (void)daw_audio_recording_drain_if_transport_playing(state);
    uint64_t captured = atomic_load(&recording->captured_frames);
    if (!recording->drain_failed && captured > recording->next_capture_frame &&
        (recording->timeline_aligned || engine_transport_is_playing(state->engine))) {
        if (!daw_recording_fill_gap(recording, captured - recording->next_capture_frame, core_time_now_ns()) ||
            !daw_take_journal_sync(&recording->journal)) recording->drain_failed = true;
    }
    if (recording->drain_failed) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Checkpoint failed; retained journal requires recovery before insertion.");
        return false;
    }

    recording->checkpoint_frames = recording->take_frame_count;

    // Resolve the take's original track identity before creating a file or inserting a clip.
    recording->target_track_index = -1;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int i = 0; i < engine_get_track_count(state->engine); ++i) {
        if (tracks[i].runtime_id == recording->target_track_id) {
            recording->target_track_index = i;
            break;
        }
    }
    if (recording->target_track_index < 0) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "The recording track was removed. The captured take has not been inserted.");
        return false;
    }

    if (recording->take_frame_count == 0 || !recording->take_frames) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Audio recording produced no frames.");
        SDL_Log("audio_recording: finish failed reason=no_frames track=%d dropped_frames=%llu",
                recording->target_track_index,
                (unsigned long long)atomic_load_explicit(&recording->dropped_frames, memory_order_relaxed));
        return false;
    }

    char path[SESSION_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s.wav", recording->journal.path) >= (int)sizeof(path)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to allocate audio recording output path.");
        SDL_Log("audio_recording: finish failed reason=output_path track=%d root=%s",
                recording->target_track_index,
                daw_audio_recording_root(state));
        return false;
    }
    if (daw_take_journal_publish(recording->journal.path, path, false, 0xA23u, recording->take_frame_count, NULL) != DAW_SAVE_SYNCED) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to write recorded audio WAV.");
        SDL_Log("audio_recording: finish failed reason=wav_write path=%s frames=%llu sample_rate=%d channels=%d",
                path,
                (unsigned long long)recording->take_frame_count,
                recording->sample_rate,
                recording->channels);
        return false;
    }

    MediaRegistryEntry media_entry = {0};
    const char* media_id = NULL;
    if (!state->media_import && media_registry_ensure_for_path(&state->media_registry, path, "Recording", &media_entry)) {
        media_id = media_entry.id[0] != '\0' ? media_entry.id : NULL;
        (void)media_registry_save(&state->media_registry);
    } else if (!state->media_import) {
        SDL_Log("Audio recording warning: failed to register %s; adding clip by path only.", path);
    }

    if (state->media_import) {
        library_browser_scan(&state->library, &state->media_registry);
        uint64_t id = daw_media_import_submit(state, path, media_id, recording->target_track_index,
                                             recording->start_frame, AUDIO_MEDIA_JOB_RECORDING);
        if (out_result) {
            out_result->saved = true;
            out_result->insertion_pending = id != 0;
            SDL_strlcpy(out_result->wav_path, path, sizeof(out_result->wav_path));
            out_result->track_index = recording->target_track_index;
            out_result->frame_count = recording->take_frame_count;
            out_result->sample_rate = recording->sample_rate;
            out_result->channels = recording->channels;
        }
        daw_audio_recording_cancel(recording);
        daw_audio_recording_set_status(recording, id ? "Recording saved; insertion pending (Ctrl/Cmd+Esc cancels)." :
            "Recording saved; insertion unavailable. Import the WAV from the library to retry.");
        SDL_strlcpy(recording->pending_path, path, sizeof(recording->pending_path));
        return true;
    }
    // Explicit headless/raw recording callers retain their synchronous insertion contract.
    int clip_index = -1;
    if (!engine_add_clip_to_track_with_id(state->engine,
                                          recording->target_track_index,
                                          path,
                                          media_id,
                                          recording->start_frame,
                                          &clip_index)) {
        recording->status = DAW_AUDIO_RECORDING_ERROR;
        daw_audio_recording_set_status(recording, "Failed to insert recorded audio clip.");
        SDL_Log("audio_recording: finish failed reason=insert_clip track=%d path=%s",
                recording->target_track_index,
                path);
        return false;
    }
    daw_audio_recording_push_insert_undo(state, recording->target_track_index, clip_index);

    timeline_selection_set_single(state, recording->target_track_index, clip_index);

    if (out_result) {
        out_result->saved = true;
        out_result->inserted = true;
        SDL_strlcpy(out_result->wav_path, path, sizeof(out_result->wav_path));
        out_result->track_index = recording->target_track_index;
        out_result->clip_index = clip_index;
        out_result->frame_count = recording->take_frame_count;
        out_result->sample_rate = recording->sample_rate;
        out_result->channels = recording->channels;
    }

    SDL_strlcpy(recording->pending_path, path, sizeof(recording->pending_path));
    SDL_Log("audio_recording: finish ok track=%d clip=%d path=%s frames=%llu sample_rate=%d channels=%d dropped_frames=%llu",
            recording->target_track_index,
            clip_index,
            path,
            (unsigned long long)recording->take_frame_count,
            recording->sample_rate,
            recording->channels,
            (unsigned long long)atomic_load_explicit(&recording->dropped_frames, memory_order_relaxed));
    daw_audio_recording_cancel(recording);
    daw_audio_recording_set_status(recording, "Audio recording finished.");
    return true;
}

void daw_audio_recording_cancel(DawAudioRecordingState* recording) {
    if (!recording) {
        return;
    }
    bool finishing_successfully = recording->pending_path[0] != '\0';
    bool had_state = !finishing_successfully &&
                     (recording->status == DAW_AUDIO_RECORDING_ACTIVE ||
                      recording->capture_device_started ||
                      recording->capture_device_open ||
                      recording->take_frame_count > 0);
    if (had_state) {
        SDL_Log("audio_recording: cancel status=%d track=%d frames=%llu dropped_frames=%llu capture_open=%d capture_started=%d",
                (int)recording->status,
                recording->target_track_index,
                (unsigned long long)recording->take_frame_count,
                (unsigned long long)atomic_load_explicit(&recording->dropped_frames, memory_order_relaxed),
                recording->capture_device_open ? 1 : 0,
                recording->capture_device_started ? 1 : 0);
    }
    if (recording->capture_device_started) {
        audio_capture_device_stop(&recording->capture_device);
        recording->capture_device_started = false;
    }
    daw_recording_worker_stop(recording);
    if (recording->capture_device_open || recording->capture_device.is_open) {
        audio_capture_device_close(&recording->capture_device);
        recording->capture_device_open = false;
    }
    if (recording->record_armed_engine) {
        (void)engine_set_record_armed_track(recording->record_armed_engine, -1);
        recording->record_armed_engine = NULL;
    }
    (void)daw_take_journal_close(&recording->journal);
    if (recording->queue_ready) {
        ringbuf_free(&recording->capture_packets);
        recording->queue_ready = false;
    }
    daw_audio_recording_clear_take(recording);
    recording->status = DAW_AUDIO_RECORDING_IDLE;
    recording->transport_started_by_recording = false;
    recording->record_armed_engine = NULL;
    recording->sample_rate = 0;
    recording->channels = 0;
    recording->target_track_index = -1;
    recording->target_track_id = 0;
    recording->start_frame = 0;
    atomic_store_explicit(&recording->dropped_frames, 0, memory_order_relaxed);
    atomic_store(&recording->queue_dropped_frames, 0);
    atomic_store(&recording->clock_missing_frames, 0);
    atomic_store(&recording->clock_continuity_frames, 0);
}
