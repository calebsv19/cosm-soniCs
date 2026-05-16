#pragma once

#include "engine/engine.h"
#include "engine/audio_source.h"
#include "engine/graph.h"
#include "engine/sources.h"
#include "engine/ringbuf.h"
#include "engine/scope_host.h"

#include "audio/media_cache.h"
#include "audio/audio_device.h"
#include "audio/audio_queue.h"

#include "effects/effects_manager.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define ENGINE_SPECTRUM_FFT_SIZE 2048
#define ENGINE_SPECTRUM_AVG_FRAMES 2
#define ENGINE_SPECTRUM_QUEUE_BYTES (1u << 20)
#define ENGINE_SPECTROGRAM_FFT_SIZE 1024
#define ENGINE_SPECTROGRAM_QUEUE_BYTES (1u << 20)
#define ENGINE_FX_LUFS_MAX_CHANNELS 2
#define ENGINE_FX_LUFS_BLOCK_HZ 10
#define ENGINE_FX_LUFS_MOMENTARY_BLOCKS 4
#define ENGINE_FX_LUFS_SHORT_BLOCKS 30
#define ENGINE_FX_LUFS_HIST_MIN_DB -100.0f
#define ENGINE_FX_LUFS_HIST_MAX_DB 10.0f
#define ENGINE_FX_LUFS_HIST_STEP_DB 0.1f
#define ENGINE_FX_LUFS_HIST_BINS 1101
#define ENGINE_SCOPE_SAMPLE_CAPACITY 256
#define ENGINE_SCOPE_STREAM_CAPACITY_BYTES (ENGINE_SCOPE_SAMPLE_CAPACITY * sizeof(float))

typedef enum {
    ENGINE_CMD_PLAY = 1,
    ENGINE_CMD_STOP = 2,
    ENGINE_CMD_GRAPH_SWAP = 3,
    ENGINE_CMD_SEEK = 4,
    ENGINE_CMD_SET_LOOP = 5,
    ENGINE_CMD_SET_EQ = 6,
    ENGINE_CMD_REBUILD_SOURCES = 7,
    ENGINE_CMD_SET_FX_PARAM = 8,
    ENGINE_CMD_SET_TEMPO = 9,
    ENGINE_CMD_MIDI_AUDITION_NOTE_ON = 10,
    ENGINE_CMD_MIDI_AUDITION_NOTE_OFF = 11,
    ENGINE_CMD_MIDI_AUDITION_ALL_OFF = 12,
} EngineCommandType;

typedef struct {
    EngineCommandType type;
    union {
        struct {
            EngineGraph* new_graph;
        } graph_swap;
        struct {
            uint64_t frame;
        } seek;
        struct {
            bool enabled;
            uint64_t start_frame;
            uint64_t end_frame;
        } loop;
        struct {
            int target; // 0 = master, 1 = track
            int track_index;
            EngineEqCurve curve;
        } eq;
        // Queues a single FX parameter change for worker-thread application.
        struct {
            bool is_master;
            int track_index;
            FxInstId id;
            uint32_t param_index;
            float value;
            FxParamMode mode;
            float beat_value;
        } fx_param;
        // Updates the worker-side tempo state used for beat-synced FX params.
        struct {
            TempoState tempo;
        } tempo;
        struct {
            int track_index;
            EngineInstrumentPresetId preset;
            EngineInstrumentParams params;
            uint8_t note;
            float velocity;
        } midi_audition;
    } payload;
} EngineCommand;

typedef struct EngineFxSnapshot {
    FxMasterSnapshot master;
    FxMasterSnapshot* tracks;
    int track_count;
} EngineFxSnapshot;

// Holds biquad filter coefficients and state for LUFS K-weighting.
typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1;
    float z2;
} EngineFxLufsBiquad;

// Tracks rolling LUFS state for a single metering instance.
typedef struct {
    int sample_rate;
    int channels;
    int block_target;
    int block_samples;
    double block_sum;
    double block_history[ENGINE_FX_LUFS_SHORT_BLOCKS];
    int block_head;
    int block_count;
    double hist_sum[ENGINE_FX_LUFS_HIST_BINS];
    int hist_count[ENGINE_FX_LUFS_HIST_BINS];
    float lufs_integrated;
    float lufs_short_term;
    float lufs_momentary;
    EngineFxLufsBiquad hp[ENGINE_FX_LUFS_MAX_CHANNELS];
    EngineFxLufsBiquad hs[ENGINE_FX_LUFS_MAX_CHANNELS];
} EngineFxLufsState;

// Stores the running linear peak/RMS values and clip hold for a meter.
typedef struct {
    float peak;
    float rms;
    int clip_hold;
} EngineMeterState;

// Stores the latest FX meter snapshot keyed by instance id.
typedef struct {
    FxInstId id;
    EngineFxMeterSnapshot snapshot;
    EngineFxLufsState lufs_state;
} EngineFxMeterTap;

// Stores FX meter taps for a single chain (master or track).
typedef struct {
    EngineFxMeterTap taps[FX_MASTER_MAX];
    int count;
} EngineFxMeterBank;

// Stores samples for a single FX scope stream.
typedef struct {
    FxInstId id;
    EngineScopeStreamKind kind;
    RingBuffer buffer;
} EngineFxScopeTap;

// Stores FX scope taps for a single chain (master or track).
typedef struct {
    EngineFxScopeTap taps[FX_MASTER_MAX];
    int count;
} EngineFxScopeBank;

// Owns scope stream storage for master and track FX instances.
typedef struct {
    EngineFxScopeBank master;
    EngineFxScopeBank* tracks;
    int track_capacity;
} EngineScopeHost;

// Stores the rolling spectrogram history and capture controls for a single active meter.
typedef struct {
    float history[ENGINE_SPECTROGRAM_HISTORY][ENGINE_SPECTROGRAM_BINS];
    int head;
    int count;
    int bins;
    int last_track;
    FxInstId last_id;
} EngineFxSpectrogramState;

struct Engine {
    EngineRuntimeConfig config;
    AudioDevice device;
    bool device_started;
    AudioQueue output_queue;
    SDL_Thread* worker_thread;
    TempoState tempo; // Worker-side tempo snapshot for FX beat conversions.
    atomic_bool worker_running;
    atomic_bool transport_playing;
    atomic_bool rebuild_sources_pending;
    atomic_int record_armed_track_index;
    RingBuffer command_queue;
    SDL_threadID worker_thread_id; // Tracks the engine worker thread id for render-thread checks.
    bool render_warned_fxm_mutex; // Tracks if fxm_mutex render-thread warning has been emitted.
    bool render_warned_meter_mutex; // Tracks if meter_mutex render-thread warning has been emitted.
    EffectsManager* fxm;
    SDL_mutex* fxm_mutex;
    SDL_mutex* eq_mutex;
    EngineGraph* graph;
    EngineToneSource* tone_source;
    EngineGraphSourceOps tone_ops;
    EngineGraphSourceOps sampler_ops;
    EngineGraphSourceOps instrument_ops;
    struct EngineInstrumentSource* midi_audition_source;
    EngineMidiNoteList midi_audition_notes;
    EngineInstrumentPresetId midi_audition_preset;
    EngineInstrumentParams midi_audition_params;
    uint64_t midi_audition_idle_frame;
    int midi_audition_track_index;
    EngineEqState master_eq;
    EngineTrack* tracks;
    int track_count;
    int track_capacity;
    uint64_t transport_frame;
    uint64_t next_clip_id;
    AudioMediaCache media_cache;
    EngineAudioSource* audio_sources;
    int audio_source_count;
    int audio_source_capacity;
    atomic_bool loop_enabled;
    atomic_uint_fast64_t loop_start_frame;
    atomic_uint_fast64_t loop_end_frame;
    SDL_mutex* spectrum_mutex;
    SDL_mutex* spectrogram_mutex;
    SDL_mutex* meter_mutex;
    RingBuffer spectrum_queue;
    RingBuffer spectrogram_queue;
    SDL_Thread* spectrum_thread;
    SDL_Thread* spectrogram_thread;
    atomic_bool spectrum_running;
    atomic_bool spectrogram_running;
    float spectrum_history[ENGINE_SPECTRUM_HISTORY][ENGINE_SPECTRUM_BINS];
    float* track_spectra;
    int track_spectrum_capacity;
    int spectrum_history_index;
    int spectrum_bins;
    int spectrum_block_counter;
    int spectrum_block_skip;
    bool spectrum_update_active;
    bool spectrum_update_master;
    bool spectrum_update_track;
    int spectrum_active_track;
    float spectrum_window[ENGINE_SPECTRUM_FFT_SIZE];
    int spectrum_window_index;
    int spectrum_window_filled;
    float spectrum_avg_history[ENGINE_SPECTRUM_AVG_FRAMES][ENGINE_SPECTRUM_BINS];
    int spectrum_avg_index;
    int spectrum_avg_count;
    int spectrum_last_view;
    int spectrum_last_track;
    atomic_bool spectrum_enabled;
    atomic_int spectrum_view;
    atomic_int spectrum_target_track;
    atomic_bool spectrogram_enabled;
    atomic_int spectrogram_target_track;
    atomic_uint spectrogram_target_id;
    int spectrogram_block_counter;
    int spectrogram_block_skip;
    bool spectrogram_update_active;
    EngineFxSpectrogramState spectrogram_state;
    EngineMeterState master_meter;
    EngineMeterState* track_meters;
    int track_meter_capacity;
    EngineFxMeterBank master_fx_meters;
    EngineFxMeterBank* track_fx_meters;
    int track_fx_meter_capacity;
    FxInstId active_fx_meter_id;
    bool active_fx_meter_is_master;
    int active_fx_meter_track;
    EngineScopeHost scope_host;
    EngineMeterSnapshot master_meter_snapshots[2];
    EngineMeterSnapshot* track_meter_snapshots;
    atomic_int meter_snapshot_index;
    EngineFxMeterBank master_fx_meter_snapshots[2];
    EngineFxMeterBank* track_fx_meter_snapshots;
};

void engine_trace(const Engine* engine, const char* fmt, ...);
void engine_timing_trace(const Engine* engine, const char* fmt, ...);
void engine_audio_callback(float* output, int frames, int channels, void* userdata);
void engine_sanitize_block(float* buf, size_t samples);
bool engine_post_command(Engine* engine, const EngineCommand* cmd);
void engine_rebuild_sources(Engine* engine);
// Queues a source rebuild on the worker thread when running, otherwise rebuilds immediately.
bool engine_request_rebuild_sources(Engine* engine);
// Resets a meter state to silence and clears clip hold.
void engine_meter_reset_state(EngineMeterState* state);
// Clears all per-FX meter banks for the current engine state.
void engine_fx_meter_clear_all(Engine* engine);
// Registers the FX meter tap callback on the active effects manager.
void engine_register_fx_meter_tap(Engine* engine);
// Registers the FX scope tap callback on the active effects manager.
void engine_register_fx_scope_tap(Engine* engine);
// Writes a gain reduction value into the scope host for an FX instance.
void engine_scope_write_gr(Engine* engine, bool is_master, int track_index, FxInstId id, float gr_db);
// Ensures the scope host has capacity for the requested track count.
bool engine_scope_ensure_track_capacity(Engine* engine, int required_tracks);
// Clears transient scope bank samples when inserting a track before engine->track_count is incremented.
bool engine_scope_insert_track_bank(Engine* engine, int track_index);
// Clears transient scope bank samples when removing a track before engine->track_count is decremented.
void engine_scope_remove_track_bank(Engine* engine, int track_index);
// Allocates and initializes the scope host for the engine.
bool engine_scope_host_init(Engine* engine, int track_capacity);
// Releases resources owned by the engine scope host.
void engine_scope_host_free(Engine* engine);
// Resets the scope bank for a track without reallocating buffers.
void engine_scope_reset_track_bank(Engine* engine, int track_index);
void engine_mix_tracks(Engine* engine,
                       uint64_t transport_frame,
                       int frames,
                       float* interleaved_out,
                       float* track_buffer,
                       int channels);
void engine_mix_midi_audition_only(Engine* engine,
                                   uint64_t transport_frame,
                                   int frames,
                                   float* interleaved_out,
                                   float* track_buffer,
                                   int channels);
int engine_spectrum_thread_main(void* userdata);
bool engine_spectrum_begin_block(Engine* engine);
void engine_spectrum_update(Engine* engine, const float* interleaved, int frames, int channels);
void engine_spectrum_update_track(Engine* engine, int track_index, const float* interleaved, int frames, int channels);
// Runs the background thread that builds spectrogram history frames.
int engine_spectrogram_thread_main(void* userdata);
// Prepares spectrogram capture for the current audio block.
bool engine_spectrogram_begin_block(Engine* engine);
// Writes spectrogram input samples from the active FX meter tap.
void engine_spectrogram_update_fx(Engine* engine,
                                  bool is_master,
                                  int track_index,
                                  FxInstId id,
                                  const float* interleaved,
                                  int frames,
                                  int channels);
void engine_clip_destroy(Engine* engine, EngineClip* clip);
void engine_midi_audition_apply_note_on(Engine* engine,
                                        int track_index,
                                        EngineInstrumentPresetId preset,
                                        EngineInstrumentParams params,
                                        uint8_t note,
                                        float velocity);
void engine_midi_audition_apply_note_off(Engine* engine, uint8_t note);
void engine_midi_audition_apply_all_off(Engine* engine);
void engine_track_init(EngineTrack* track);
void engine_track_clear(Engine* engine, EngineTrack* track);
EngineTrack* engine_get_track_mutable(Engine* engine, int track_index);
bool engine_ensure_track_capacity(Engine* engine, int required_tracks);
bool engine_fx_snapshot_all(Engine* engine, EngineFxSnapshot* out_snap);
void engine_fx_restore_all(Engine* engine, const EngineFxSnapshot* snap);
