#pragma once

#include "audio/audio_capture_device.h"
#include "audio/media_clip.h"
#include "audio/audio_queue.h"
#include "session.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AppState AppState;
typedef struct Engine Engine;

typedef enum {
    DAW_AUDIO_RECORDING_IDLE = 0,
    DAW_AUDIO_RECORDING_ACTIVE,
    DAW_AUDIO_RECORDING_ERROR
} DawAudioRecordingStatus;

typedef struct {
    bool inserted;
    char wav_path[SESSION_PATH_MAX];
    int track_index;
    int clip_index;
    uint64_t frame_count;
    int sample_rate;
    int channels;
} DawAudioRecordingResult;

typedef struct {
    DawAudioRecordingStatus status;
    AudioCaptureDevice capture_device;
    AudioQueue capture_queue;
    bool queue_ready;
    bool capture_device_open;
    bool capture_device_started;
    float* take_frames;
    uint64_t take_frame_count;
    uint64_t take_frame_capacity;
    atomic_uint_fast64_t dropped_frames;
    bool transport_started_by_recording;
    Engine* record_armed_engine;
    int sample_rate;
    int channels;
    int target_track_index;
    uint64_t start_frame;
    char pending_path[SESSION_PATH_MAX];
    char status_message[256];
} DawAudioRecordingState;

void daw_audio_recording_init(DawAudioRecordingState* recording);
void daw_audio_recording_free(DawAudioRecordingState* recording);
bool daw_audio_recording_is_active(const DawAudioRecordingState* recording);
const char* daw_audio_recording_status_message(const DawAudioRecordingState* recording);
bool daw_audio_recording_next_path(const AppState* state, char* out, size_t len);
bool daw_audio_recording_begin_take(AppState* state,
                                    int track_index,
                                    uint64_t start_frame,
                                    const AudioDeviceSpec* desired);
bool daw_audio_recording_begin_capture(AppState* state,
                                       int track_index,
                                       uint64_t start_frame,
                                       const char* device_name,
                                       const AudioDeviceSpec* desired);
bool daw_audio_recording_begin_timeline_capture(AppState* state);
bool daw_audio_recording_finish_timeline_capture(AppState* state, DawAudioRecordingResult* out_result);
size_t daw_audio_recording_enqueue_frames(DawAudioRecordingState* recording,
                                          const float* input,
                                          size_t frames,
                                          int channels);
uint64_t daw_audio_recording_drain(DawAudioRecordingState* recording);
uint64_t daw_audio_recording_drain_if_transport_playing(AppState* state);
bool daw_audio_recording_take_clip_view(const DawAudioRecordingState* recording, AudioMediaClip* out_clip);
bool daw_audio_recording_finish(AppState* state, DawAudioRecordingResult* out_result);
void daw_audio_recording_cancel(DawAudioRecordingState* recording);
