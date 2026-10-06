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

// Owns prepared source objects and media pins independently of editable clips.
typedef struct EngineSourcePlan EngineSourcePlan;

typedef struct EngineMixState EngineMixState;

// Identifies one coherent analyzer selection and its source track lifetime.
typedef struct EngineAnalysisSelection {
    uint64_t revision;
    uint64_t track_id;
    int view;
    FxInstId effect_id;
    bool enabled;
} EngineAnalysisSelection;

// Publishes analyzer selection fields through an atomic bounded-read revision protocol.
typedef struct EngineAnalysisTarget {
    atomic_uint_fast64_t revision;
    atomic_uint_fast64_t track_id;
    atomic_int view;
    atomic_uint effect_id;
    atomic_bool enabled;
} EngineAnalysisTarget;

// Identifies a captured window without retaining its sample payload.
typedef struct EngineAnalysisStamp {
    EngineAnalysisSelection selection;
    uint64_t epoch, sequence, first_sample;
} EngineAnalysisStamp;

// Transfers a complete contiguous analysis window and the identity of its capture target.
typedef struct EngineAnalysisPacket {
    EngineAnalysisSelection selection;
    uint64_t epoch, sequence, first_sample;
    float samples[ENGINE_SPECTRUM_FFT_SIZE];
} EngineAnalysisPacket;

// Owns producer-only window assembly and atomic diagnostics for one analyzer stream.
typedef struct EngineAnalysisStream {
    EngineAnalysisTarget target;
    EngineAnalysisPacket capture;
    int filled;
    uint64_t next_sequence, sample_cursor;
    bool block_seen, block_appended;
    atomic_uint_fast64_t queued;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t published;
    atomic_uint_fast64_t consumed, stale, backlog_high_water;
    atomic_uint_fast64_t computed, compute_ticks, compute_max_ticks;
} EngineAnalysisStream;

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
    ENGINE_CMD_PAUSE = 13,
} EngineCommandType;

typedef struct {
    EngineCommandType type;
    uint64_t submitted_ns; // Monotonic FIFO admission timestamp.
    uint64_t transport_serial; // Orders all accepted transport operations.
    union {
        uint64_t transport_token;
        struct {
            EngineSourcePlan* plan;
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
            uint64_t track_runtime_id;
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
    uint64_t runtime_id;
} EngineFxMeterBank;

// Stores a published effect observation without mutable DSP history.
typedef struct {
    FxInstId id;
    EngineFxMeterSnapshot snapshot;
} EngineFxMeterSnapshotTap;

// Stores one coherent published chain of effect observations and its track identity.
typedef struct {
    EngineFxMeterSnapshotTap taps[FX_MASTER_MAX];
    int count;
    uint64_t runtime_id;
} EngineFxMeterSnapshotBank;


// Owns track metadata, DSP histories, and meter accumulation for one rendering revision.
struct EngineMixState {
    Engine* owner;
    EngineTrack* tracks;
    int track_count;
    int* previous_tracks;
    FxSampleRamp* audition_gain_ramps; // Prepared stopped-audition gain and mute transitions.
    FxSampleRamp* track_pan_ramps; // Worker-owned live pan transitions for surviving tracks.
    EffectsManager* fxm;
    EngineEqState master_eq;
    EngineMeterState master_meter;
    EngineMeterState* track_meters;
    EngineFxMeterBank master_fx_meters;
    EngineFxMeterBank* track_fx_meters;
};

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
    bool scalar_mix_supported; // Control-owned eligibility; borrowed external graphs use full preparation.
    AudioQueue output_queue;
    atomic_size_t output_target_frames; // Effective block-aligned target, fixed before worker startup.
    // Atomic clock fields are serialized across discontinuities by the SDL endpoint lock.
    atomic_uint_fast64_t clock_epoch, clock_render_seq, clock_callback_seq;
    atomic_uint_fast64_t clock_capture_epoch; // Zero during discontinuity or non-linear/non-playing transport.
    atomic_uint_fast64_t clock_origin, clock_rendered, clock_consumed, clock_fallback_frame;
    atomic_uint_fast64_t clock_callback_start, clock_callback_frames, clock_callback_ns;
    atomic_uint_fast64_t clock_loop_start, clock_loop_end;
    atomic_bool clock_advancing;
    atomic_uint_fast64_t diag_processing_latency; // Latest render-block common DSP delay.
    atomic_uint_fast64_t diag_callback_count;
    atomic_uint_fast64_t diag_underrun_callbacks;
    atomic_uint_fast64_t diag_underrun_frames;
    atomic_uint_fast64_t diag_worker_cycles, diag_worker_render_cycles, diag_worker_over_budget;
    atomic_uint_fast64_t diag_worker_max_ns, diag_service_max_ns;
    atomic_int diag_worker_priority_status; // 0 pending, 1 high requested successfully, -1 refused.
    atomic_uint_fast64_t diag_render_blocks;
    atomic_uint_fast64_t diag_render_over_budget;
    atomic_uint_fast64_t diag_render_max_ns;
    atomic_uint_fast64_t diag_command_max_age_ns;
    atomic_uint_fast64_t diag_command_last_age_ns;
    atomic_uint_fast64_t diag_queue_high_water_frames;

    atomic_uint_fast64_t transport_requested_serial, transport_applied_serial;
    float* render_buffer; // Allocated before worker startup and freed after its final join.
    float* render_track_buffer;
    atomic_bool runtime_engine_logs;
    atomic_bool runtime_timing_logs;
    SDL_Thread* worker_thread;
    TempoState tempo; // Worker-side tempo snapshot for FX beat conversions.
    atomic_bool worker_running;
    atomic_bool transport_playing;
    atomic_uint_fast64_t playback_requested_token;
    atomic_uint_fast64_t playback_applied_token;
    atomic_uint_fast64_t playback_safety_token;
    atomic_bool rebuild_sources_pending;
    atomic_int record_armed_track_index;
    RingBuffer command_queue;
    SDL_threadID control_thread_id; // The creating thread is the sole command producer.
    atomic_uint command_safety_pending; // Overflow requests for stop and all audition notes off.
    atomic_uint_fast64_t commands_accepted;
    atomic_uint_fast64_t commands_rejected;
    atomic_uint_fast64_t commands_applied;
    atomic_uint_fast64_t command_safety_fallbacks;
    _Atomic SDL_threadID worker_thread_id; // Tracks the engine worker thread id for render-thread checks.
    bool render_warned_fxm_mutex; // Tracks if fxm_mutex render-thread warning has been emitted.
    bool render_warned_meter_mutex; // Tracks if meter_mutex render-thread warning has been emitted.
    EffectsManager* fxm;
    SDL_mutex* fxm_mutex;
    SDL_mutex* eq_mutex;
    EngineGraph* graph;
    EngineSourcePlan* active_source_plan; // Worker-owned while running; control-owned while stopped.
    _Atomic(EngineSourcePlan*) pending_source_plan;
    _Atomic(EngineSourcePlan*) retired_source_plans;
    EngineToneSource* tone_source;
    EngineGraphSourceOps tone_ops;
    EngineGraphSourceOps sampler_ops;
    EngineGraphSourceOps instrument_ops;
    struct EngineInstrumentSource* midi_audition_source;
    EngineMidiNoteList midi_audition_notes;
    EngineInstrumentPresetId midi_audition_preset;
    EngineInstrumentParams midi_audition_params;
    uint64_t midi_audition_idle_frame;
    uint64_t midi_audition_tail_until;
    int midi_audition_track_index;
    EngineEqState master_eq;
    EngineTrack* tracks;
    int track_count;
    int track_capacity;
    atomic_uint_fast64_t transport_frame; // Render clock published atomically to UI readers.
    uint64_t next_clip_id;
    AudioMediaCache media_cache;
    EngineClipContentSnapshot* clip_history_snapshots; // Invalidates retained edit history before cache teardown.
    EngineAudioSource** audio_sources; // Stable metadata objects behind a growable pointer table.
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
    EngineAnalysisStream spectrum_stream;
    EngineAnalysisStream spectrogram_stream;
    EngineAnalysisStamp spectrogram_result_stamp; // Protected by spectrogram_mutex.
    uint64_t spectrum_result_epoch, spectrogram_result_epoch; // Protected by respective result mutexes.
    uint64_t spectrum_result_track_id; // Protected by spectrum_mutex.
    int spectrum_result_view; // Protected by spectrum_mutex.
    SDL_Thread* spectrum_thread;
    SDL_Thread* spectrogram_thread;
    atomic_bool spectrum_running;
    atomic_bool spectrogram_running;
    float spectrum_history[ENGINE_SPECTRUM_HISTORY][ENGINE_SPECTRUM_BINS];
    float* track_spectra;
    int track_spectrum_capacity;
    int spectrum_history_index;
    int spectrum_bins;
    bool spectrum_update_active;
    bool spectrum_update_master;
    bool spectrum_update_track;
    int spectrum_active_track;
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
    EngineFxMeterSnapshotBank master_fx_meter_snapshots[2];
    EngineFxMeterSnapshotBank* track_fx_meter_snapshots;
};

void engine_trace(const Engine* engine, const char* fmt, ...);
// Initializes analyzer publication before any producer or consumer starts.
void engine_analysis_init(EngineAnalysisStream* stream);
// Publishes a new control-thread selection only when its meaning changes.
bool engine_analysis_select(EngineAnalysisStream* stream, uint64_t track_id, int view, FxInstId effect_id, bool enabled);
// Invalidates queued/result generations without modifying live ring indices.
void engine_analysis_invalidate(EngineAnalysisStream* stream);
// Reads one coherent target without waiting for the control thread.
bool engine_analysis_selection(const EngineAnalysisStream* stream, EngineAnalysisSelection* selection);
// Begins producer-owned capture, discarding an obsolete partial window.
bool engine_analysis_begin(EngineAnalysisStream* stream);
// Enqueues whole windows or counts a drop without stealing consumer ownership.
void engine_analysis_append(EngineAnalysisStream* stream, RingBuffer* queue, const float* input,
                            int frames, int channels, int window_frames);
// Checks whether a packet still belongs to the current enabled selection.
bool engine_analysis_current(const EngineAnalysisStream* stream, const EngineAnalysisPacket* packet);
void engine_timing_trace(const Engine* engine, const char* fmt, ...);
void engine_audio_callback(float* output, int frames, int channels, void* userdata);
void engine_sanitize_block(float* buf, size_t samples);
bool engine_post_command(Engine* engine, const EngineCommand* cmd);
void engine_rebuild_sources(Engine* engine);
// Builds and publishes a complete source plan on the creating control thread.
bool engine_source_plan_publish(Engine* engine);
// Adopts a prepared plan at a worker block boundary without allocating or freeing it.
void engine_source_plan_apply(Engine* engine);
// Wraps a compatible caller graph in an owned plan, consuming the graph on all outcomes.
EngineSourcePlan* engine_source_plan_wrap_graph(Engine* engine, EngineGraph* graph);
// Adopts a specific queued plan and retires the previous revision at a block boundary.
void engine_source_plan_adopt(Engine* engine, EngineSourcePlan* plan);
// Releases an unpublished plan on the control thread.
void engine_source_plan_discard(Engine* engine, EngineSourcePlan* plan);
// Cancels queued commands after the worker stops, reclaiming owned payloads.
void engine_cancel_commands(Engine* engine);
// Reclaims plans retired by the worker on the control thread.
void engine_source_plan_collect(Engine* engine);
// Releases all source plans after every audio/analysis reader has stopped.
void engine_source_plan_shutdown(Engine* engine);
// Resolves the worker's source graph without borrowing control-model objects.
EngineGraph* engine_render_source_graph(Engine* engine);
// Returns exclusively render-owned track and DSP state for the active revision.
EngineMixState* engine_render_mix_state(const Engine* engine);
// Binds existing meter delivery to a separately owned render effect manager.
void engine_bind_fx_meter_tap(EngineMixState* mix);
// Publishes a coherent meter frame without waiting for the UI or reading editable tracks.
void engine_publish_mix_meters(EngineMixState* mix);
// Binds existing scope delivery to a separately owned render effect manager.
void engine_bind_fx_scope_tap(Engine* engine, EffectsManager* fxm);
// Queues a source rebuild on the worker thread when running, otherwise rebuilds immediately.
bool engine_request_rebuild_sources(Engine* engine);
void engine_process_commands(Engine* engine);
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
// Mixes one prepared revision; a null observer isolates offline rendering and rejects invalid output.
bool engine_mix_prepared(Engine* observer, EngineGraph* graph, EngineMixState* mix,
                         uint64_t start, int frames, float* out, float* scratch, int channels);
// Prepares an export-only snapshot on the control thread; discard it with engine_source_plan_discard.
EngineSourcePlan* engine_source_plan_prepare_export(Engine* engine, uint64_t end, uint64_t tail);
// Reads export-owned latency after preparing its first DSP block.
uint64_t engine_source_plan_export_latency(EngineSourcePlan* plan);
// Renders a captured export block without live publication or telemetry.
bool engine_source_plan_render_export(EngineSourcePlan* plan, uint64_t start, int frames,
                                     float* output, float* scratch, int channels);
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
bool engine_midi_notes_fit_duration(const EngineMidiNoteList* notes, uint64_t duration_frames);
void engine_midi_audition_apply_note_on(Engine* engine,
                                        int track_index,
                                        EngineInstrumentPresetId preset,
                                        EngineInstrumentParams params,
                                        uint8_t note,
                                        float velocity);
void engine_midi_audition_apply_note_off(Engine* engine, uint8_t note);
void engine_midi_audition_apply_all_off(Engine* engine);
// Retires released audition voices and bounds stopped effect-tail processing.
void engine_midi_audition_retire(Engine* engine);
// Clears prepared DSP histories and snaps controls at explicit transport or idle-audition resets.
void engine_transport_reset_history(Engine* engine);
// Sorts track clip descriptors without mutating their owned sources.
void engine_track_sort_clips(EngineTrack* track);
void engine_track_init(EngineTrack* track);
void engine_track_clear(Engine* engine, EngineTrack* track);
EngineTrack* engine_get_track_mutable(Engine* engine, int track_index);
bool engine_ensure_track_capacity(Engine* engine, int required_tracks);
// Invalidates track-indexed meter and scope readback after accepted topology changes.
void engine_track_reset_published_caches(Engine* engine);
bool engine_fx_snapshot_all(Engine* engine, EngineFxSnapshot* out_snap);
void engine_fx_restore_all(Engine* engine, const EngineFxSnapshot* snap);

// Maps elapsed audio frames into a project timeline, including an intro and repeated loop wraps.
uint64_t engine_clock_advance(uint64_t origin, uint64_t frames, uint64_t loop_start, uint64_t loop_end);
// Flushes an old stream generation at a callback boundary, optionally retaining its consumed cursor.
void engine_clock_discontinuity(Engine* engine, uint64_t frame, bool hold, bool advancing);
// Publishes a fully rendered block with its monotonic stream-frame count.
size_t engine_clock_write(Engine* engine, const float* buffer, size_t frames);

// Accounts for one complete worker block against its nominal audio duration.
void engine_diagnostics_render(Engine* engine, uint64_t elapsed_ns, size_t frames);

// Starts a capture block without carrying partial samples across a transport epoch.
bool engine_analysis_begin_at(EngineAnalysisStream* stream, uint64_t epoch);

// Publishes gain, pan, mute, and solo targets without cloning sources or DSP histories.
bool engine_request_mixer_update(Engine* engine);

// Accounts for one complete busy worker iteration, excluding its intentional queue/idle sleep.
void engine_diagnostics_worker(Engine* engine, uint64_t elapsed_ns, uint64_t service_ns, bool rendered);
// Computes a block-aligned render-ahead target with room for at least two device callbacks.
size_t engine_output_target_frames(int block, int callback, int requested_blocks);

// Receives one complete packet and records consumer-observed backlog without touching producer state.
bool engine_analysis_receive(EngineAnalysisStream* stream, RingBuffer* queue, EngineAnalysisPacket* packet);
// Records transform duration on the sole analyzer consumer thread.
void engine_analysis_computed(EngineAnalysisStream* stream, uint64_t began);

// Invalidates retained clip history while engine-owned media pins can still be released.
void engine_clip_history_invalidate(Engine* engine);

// Prepares complete clip fields without mutating the original clip ownership.
bool engine_clip_prepare_transform(const EngineClip* original, const EngineClipTransform* transform, EngineClip* moved);
// Resolves overlap only on an exclusively owned detached candidate track.
bool engine_track_prepare_no_overlap(Engine* engine, EngineTrack* candidate, struct EngineSamplerSource* anchor_sampler);
