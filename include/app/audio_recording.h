#pragma once

#include "audio/audio_capture_device.h"
#include "audio/media_clip.h"
#include "engine/ringbuf.h"
#include "engine/engine.h"
#include "audio/take_journal.h"
#include "session.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAW_RECORDING_PREVIEW_FRAMES 65536u

typedef struct AppState AppState;
typedef struct Engine Engine;

typedef enum {
    DAW_AUDIO_RECORDING_IDLE = 0,
    DAW_AUDIO_RECORDING_ACTIVE,
    DAW_AUDIO_RECORDING_ERROR
} DawAudioRecordingStatus;

typedef struct {
    bool inserted;
    bool saved, insertion_pending; // Durable output and asynchronous insertion are distinct outcomes.
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
    RingBuffer capture_packets;
    DawTakeJournal journal;
    bool timeline_aligned; // Immutable while the capture callback runs.
    uint64_t producer_epoch; // Capture-thread-owned continuous transport segment.
    bool producer_has_epoch;
    atomic_uint_fast64_t captured_frames, captured_packets, first_capture_ns, last_capture_ns;
    atomic_bool capture_halted;
    uint64_t next_capture_frame, gap_frames, checkpoint_frames;
    int64_t alignment_error_frames;
    bool drain_failed;
    bool queue_ready;
    bool capture_device_open;
    bool capture_device_started;
    struct DawRecordingWorker* worker; // Owns production journal I/O until joined.
    float* take_frames; // Bounded recent samples, never the full long take.
    uint64_t take_frame_count;
    uint64_t take_frame_capacity;
    atomic_uint_fast64_t dropped_frames;
    atomic_uint_fast64_t clock_continuity_frames; // Samples retained through a current-epoch check while diagnostics were busy.
    atomic_uint_fast64_t queue_dropped_frames, clock_missing_frames; // Separates storage pressure from clock-read rejection.
    bool transport_started_by_recording;
    Engine* record_armed_engine;
    int sample_rate;
    int channels;
    int target_track_index;
    uint64_t target_track_id;
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

// Captures a timestamped block against an observed output clock without allocating or performing I/O.
size_t daw_audio_recording_enqueue_timed(DawAudioRecordingState* recording, const float* input,
                                         size_t frames, int channels, uint64_t timestamp_ns,
                                         const EngineClockSnapshot* clock);
