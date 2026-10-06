#pragma once
#include "daw/save_file.h"

#include "config.h"
#include "effects/effects_manager.h"
#include "engine/automation.h"
#include "engine/engine_eq.h"
#include "engine/fade_curve.h"
#include "engine/instrument.h"
#include "engine/midi.h"
#include "engine/scope_host.h"
#include "time/tempo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AudioMediaClip;
typedef struct Engine Engine;
struct EngineGraph;
typedef struct EngineTrack EngineTrack;
typedef struct EngineClip EngineClip;
typedef struct EngineAudioSource EngineAudioSource;
// Reports lifetime command delivery counts without exposing mutable queue storage.
typedef struct EngineCommandStats {
    uint64_t accepted;
    uint64_t rejected;
    uint64_t applied;
    uint64_t safety_fallbacks;
} EngineCommandStats;

// Copies atomic delivery counters; totals may advance independently during a live read.
bool engine_get_command_stats(const Engine* engine, EngineCommandStats* out_stats);

// Reports independent lifetime counters; queue and command occupancy are bounded observations.
typedef struct {
    uint64_t callback_count, underrun_callbacks, underrun_frames;
    uint64_t render_blocks, render_over_budget, render_max_ns;
    uint64_t worker_cycles, worker_render_cycles, worker_over_budget, worker_max_ns, service_max_ns;
    int worker_priority_status; // 0 not yet requested, 1 accepted high priority, -1 platform refusal.
    uint64_t queue_target_frames; // Effective current target, distinct from lifetime high water.
    uint64_t processing_latency_frames; // Latest render-block DSP delay; excludes queue/device delay.
    uint64_t command_max_age_ns, command_last_age_ns;
    uint64_t queued_frames, queue_high_water_frames, pending_commands;
    EngineCommandStats commands;
} EngineDiagnostics;

// Reads software diagnostics without waiting for the render worker or audio callback.
bool engine_get_diagnostics(const Engine* engine, EngineDiagnostics* out);
// Formats compact lifetime diagnostics and returns whether any lifetime failures occurred.
bool engine_format_diagnostics(const Engine* engine, char* text, size_t capacity);


struct EngineSamplerSource;
struct EngineInstrumentSource;

#define ENGINE_CLIP_NAME_MAX 128
#define ENGINE_CLIP_PATH_MAX 512
#define ENGINE_MEDIA_ID_MAX 33
#define ENGINE_SPECTRUM_BINS 256
#define ENGINE_SPECTRUM_HISTORY 4
#define ENGINE_SPECTRUM_MIN_HZ 20.0f
#define ENGINE_SPECTRUM_MAX_HZ 20000.0f
#define ENGINE_SPECTRUM_DB_FLOOR -60.0f
#define ENGINE_SPECTRUM_DB_CEIL 6.0f
#define ENGINE_SPECTROGRAM_BINS 128
#define ENGINE_SPECTROGRAM_HISTORY 160
#define ENGINE_SPECTROGRAM_MIN_HZ 20.0f
#define ENGINE_SPECTROGRAM_MAX_HZ 20000.0f
#define ENGINE_SPECTROGRAM_DB_FLOOR -60.0f
#define ENGINE_SPECTROGRAM_DB_CEIL 0.0f
#define ENGINE_FX_METER_VEC_POINTS 64

typedef enum {
    ENGINE_SPECTRUM_VIEW_MASTER = 0,
    ENGINE_SPECTRUM_VIEW_TRACK
} EngineSpectrumView;

// Identifies which runtime payload a timeline clip owns.
typedef enum {
    ENGINE_CLIP_KIND_AUDIO = 0,
    ENGINE_CLIP_KIND_MIDI
} EngineClipKind;

// EngineSpectrogramSnapshot carries a rolling spectrogram history for UI display.
typedef struct {
    int bins;
    int frames;
    float db_floor;
    float db_ceil;
    int sample_rate, window_frames;
    uint64_t epoch, sequence, first_sample, track_id;
    uint32_t effect_id;
} EngineSpectrogramSnapshot;

// Snapshot of linear peak/RMS values plus clip hold for meters.
typedef struct {
    float peak;
    float rms;
    bool clipped;
    uint64_t runtime_id; // Identifies the track lifetime represented by this snapshot.
} EngineMeterSnapshot;

// Snapshot of per-FX metering values for analysis modules.
typedef struct {
    FxTypeId type;
    float peak;
    float rms;
    float lufs_integrated;
    float lufs_short_term;
    float lufs_momentary;
    float corr;
    float mid_rms;
    float side_rms;
    float vec_x;
    float vec_y;
    float vec_points_x[ENGINE_FX_METER_VEC_POINTS];
    float vec_points_y[ENGINE_FX_METER_VEC_POINTS];
    int vec_point_count;
    bool clipped;
    bool valid;
} EngineFxMeterSnapshot;

// Owns interleaved bounce samples and metadata produced by offline rendering.
typedef struct {
    float* data;
    uint64_t frame_count;
    int channels;
    int sample_rate;
} EngineBounceBuffer;

// Selects bounded preceding context, appended release/effect frames, and explicit peak normalization.
typedef struct EngineBounceOptions {
    uint64_t preroll_frames; // At most 60 seconds; truncated at project frame zero.
    uint64_t tail_frames;    // At most 60 seconds; later authored input is excluded.
    bool normalize_if_clipping;
} EngineBounceOptions;

// Selects the single requested WAV encoding; PCM16 uses a fixed reproducible TPDF seed.
typedef enum EngineBounceWavFormat {
    ENGINE_BOUNCE_WAV_PCM16 = 0,
    ENGINE_BOUNCE_WAV_FLOAT32 = 1
} EngineBounceWavFormat;

// Holds runtime clip state including timing, fades, and automation.
struct EngineClip {
    EngineClipKind kind;
    struct EngineSamplerSource* sampler;
    struct EngineInstrumentSource* instrument;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
    bool instrument_inherits_track;
    struct AudioMediaClip* media;
    EngineAudioSource* source;
    EngineMidiNoteList midi_notes;
    float gain;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
    uint64_t timeline_start_frames;
    uint64_t duration_frames;
    uint64_t offset_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    EngineAutomationLane* automation_lanes;
    int automation_lane_count;
    int automation_lane_capacity;
    uint64_t creation_index;
    bool selected;
};

// Owns editable track content and a runtime identity that survives array movement.
struct EngineTrack {
    uint64_t runtime_id;
    EngineClip* clips;
    int clip_count;
    int clip_capacity;
    float gain;
    float pan;
    bool muted;
    bool solo;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
    bool midi_instrument_enabled;
    EngineInstrumentPresetId midi_instrument_preset;
    EngineInstrumentParams midi_instrument_params;
    EngineAutomationLane* midi_instrument_automation_lanes;
    int midi_instrument_automation_lane_count;
    int midi_instrument_automation_lane_capacity;
    EngineEqState track_eq;
};

Engine* engine_create(const EngineRuntimeConfig* cfg);
void    engine_destroy(Engine* engine);
bool    engine_start(Engine* engine);
void    engine_stop(Engine* engine);
const EngineRuntimeConfig* engine_get_config(const Engine* engine);
// Borrows registry entries on the control thread until the next structural track/effect edit or destruction.
bool    engine_fx_get_registry(const Engine* engine, const FxRegistryEntry** out_entries, int* out_count);
bool    engine_fx_registry_get_desc(const Engine* engine, FxTypeId type, FxDesc* out_desc);
// Reads pending samples for a scope stream tied to an FX instance.
int     engine_get_fx_scope_samples(const Engine* engine,
                                    bool is_master,
                                    int track_index,
                                    FxInstId id,
                                    EngineScopeStreamKind kind,
                                    float* out_samples,
                                    int max_samples);
// Returns the parameter spec list for a given effect type.
bool    engine_fx_registry_get_param_specs(const Engine* engine,
                                           FxTypeId type,
                                           const EffectParamSpec** out_specs,
                                           uint32_t* out_count);
bool    engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot);
FxInstId engine_fx_master_add(Engine* engine, FxTypeId type);
// Adds an effect and captures complete history data before its single publication.
FxInstId engine_fx_add_capture(Engine* engine, int track_index, FxTypeId type, FxMasterInstanceInfo* out, int* position);
// Restores parameters, enabled state, order and identity in one chain publication (-1 selects master).
bool engine_fx_restore_instance(Engine* engine, int track_index, int position, const FxMasterInstanceInfo* instance);
bool    engine_fx_master_remove(Engine* engine, FxInstId id);
bool    engine_fx_master_reorder(Engine* engine, FxInstId id, int new_index);
bool    engine_fx_master_set_param(Engine* engine, FxInstId id, uint32_t param_index, float value);
bool    engine_fx_master_set_param_with_mode(Engine* engine,
                                             FxInstId id,
                                             uint32_t param_index,
                                             float value,
                                             FxParamMode mode,
                                             float beat_value);
bool    engine_fx_master_set_enabled(Engine* engine, FxInstId id, bool enabled);
FxInstId engine_fx_track_add(Engine* engine, int track_index, FxTypeId type);
bool    engine_fx_track_remove(Engine* engine, int track_index, FxInstId id);
bool    engine_fx_track_reorder(Engine* engine, int track_index, FxInstId id, int new_index);
bool    engine_fx_track_set_param(Engine* engine, int track_index, FxInstId id, uint32_t param_index, float value);
bool    engine_fx_track_set_param_with_mode(Engine* engine,
                                            int track_index,
                                            FxInstId id,
                                            uint32_t param_index,
                                            float value,
                                            FxParamMode mode,
                                            float beat_value);
bool    engine_fx_track_set_enabled(Engine* engine, int track_index, FxInstId id, bool enabled);
bool    engine_fx_track_snapshot(const Engine* engine, int track_index, FxMasterSnapshot* out_snapshot);
bool    engine_fx_set_track_count(Engine* engine, int track_count);
bool    engine_is_running(const Engine* engine);
size_t  engine_get_queued_frames(const Engine* engine);
bool    engine_transport_play(Engine* engine);
bool    engine_transport_stop(Engine* engine);
// Pauses at the callback-consumed cursor while retaining position for the next play.
bool engine_transport_pause(Engine* engine);

// Separates prepared audio, callback delivery, and a bounded software presentation estimate.
typedef struct EngineClockSnapshot {
    uint64_t epoch;
    uint64_t observed_ns;
    bool loop_enabled;
    uint64_t rendered_frame, consumed_frame, presentation_frame;
    uint64_t rendered_frames, consumed_frames, queued_frames;
    uint64_t requested_serial, applied_serial;
    bool playing, pending;
    bool hardware_position_known; // SDL callback delivery is not measured DAC presentation.
} EngineClockSnapshot;
// Reads one coherent clock generation without blocking the audio callback.
bool engine_get_clock_snapshot(const Engine* engine, EngineClockSnapshot* out);
// Returns the callback-based presentation estimate used by editing and display.
uint64_t engine_get_presentation_frame(const Engine* engine);
// Describes accepted playback intent and the worker's last applied playback command.
typedef struct EnginePlaybackSnapshot {
    uint64_t requested_serial;
    uint64_t applied_serial;
    bool requested_playing;
    bool applied_playing;
    bool pending;
} EnginePlaybackSnapshot;
// Reads a coherent play/stop snapshot; this does not represent the presentation clock.
EnginePlaybackSnapshot engine_transport_get_playback_snapshot(const Engine* engine);
// Returns the latest accepted playback intent, including commands awaiting consumption.
bool engine_transport_requested_playing(const Engine* engine);
bool    engine_transport_is_playing(const Engine* engine);
bool    engine_transport_seek(Engine* engine, uint64_t frame);
bool    engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame);
// Updates the worker-side tempo state for beat-synced FX conversions.
bool    engine_set_tempo_state(Engine* engine, const TempoState* tempo);
// Consumes a compatible graph on success or failure; borrowed source userdata must outlive the engine.
bool    engine_queue_graph_swap(Engine* engine, struct EngineGraph* new_graph);
bool    engine_load_wav(Engine* engine, const char* path);
bool    engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame);
bool    engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index);
bool    engine_add_clip_to_track_with_id(Engine* engine,
                                         int track_index,
                                         const char* filepath,
                                         const char* media_id,
                                         uint64_t start_frame,
                                         int* out_clip_index);
bool    engine_add_midi_clip_to_track(Engine* engine,
                                      int track_index,
                                      uint64_t start_frame,
                                      uint64_t duration_frames,
                                      int* out_clip_index);
int     engine_add_track(Engine* engine);
bool    engine_insert_track(Engine* engine, int track_index);
bool    engine_remove_track(Engine* engine, int track_index);
const EngineTrack* engine_get_tracks(const Engine* engine);
int     engine_get_track_count(const Engine* engine);
uint64_t engine_get_transport_frame(const Engine* engine);
bool    engine_clip_set_timeline_start(Engine* engine, int track_index, int clip_index, uint64_t start_frame, int* out_clip_index);

// Clears the spectrogram queue and history for a fresh capture window.
void    engine_spectrogram_clear_history(Engine* engine);
bool    engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames);
uint64_t engine_clip_get_total_frames(const Engine* engine, int track_index, int clip_index);
// Describes the complete placement, region, gain/fade, and MIDI state restored by a clip transform.
typedef struct EngineClipTransform {
    uint64_t start_frame;
    uint64_t offset_frames;
    uint64_t duration_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    float gain;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
    bool instrument_inherits_track;
    const EngineMidiNote* midi_notes;
    int midi_note_count;
} EngineClipTransform;
// Publishes a complete clip transform, including track movement, or preserves its original state.
bool engine_transform_clip(Engine* engine, int source_track, int clip_index, int destination_track,
                           const EngineClipTransform* transform, int* out_clip_index);
// Identifies one existing clip and its complete destination state within an atomic batch.
typedef struct EngineClipBatchTransform {
    uint64_t creation_index;
    int destination_track;
    EngineClipTransform transform;
} EngineClipBatchTransform;
// Publishes transforms and any required appended tracks together or preserves the logical project and revision.
bool engine_transform_clips(Engine* engine, const EngineClipBatchTransform* edits, int count);
// Restores transforms and removes only trailing tracks empty after the move, in one publication.
bool engine_transform_clips_trim_tracks(Engine* engine, const EngineClipBatchTransform* edits, int count, int retained_tracks);
// Moves a clip with one publication, preserving identity and restoring both tracks on rejection.
bool    engine_move_clip_to_track(Engine* engine, int source_track, int clip_index, int destination_track,
                                  uint64_t start_frame, int* out_clip_index);
bool    engine_remove_clip(Engine* engine, int track_index, int clip_index);
bool    engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name);
bool    engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain);
bool    engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames);
// Sets the fade curve shapes for the specified clip sides.
bool    engine_clip_set_fade_curves(Engine* engine,
                                    int track_index,
                                    int clip_index,
                                    EngineFadeCurve fade_in_curve,
                                    EngineFadeCurve fade_out_curve);
// Retrieves the automation lane for a clip target if it exists.
bool    engine_clip_get_automation_lane(const Engine* engine,
                                        int track_index,
                                        int clip_index,
                                        EngineAutomationTarget target,
                                        const EngineAutomationLane** out_lane);
// Ensures a lane exists; the borrowed pointer expires at the next accepted automation or structural clip edit.
bool    engine_clip_ensure_automation_lane(Engine* engine,
                                           int track_index,
                                           int clip_index,
                                           EngineAutomationTarget target,
                                           EngineAutomationLane** out_lane);
// Inserts or replaces an automation point for the clip target.
bool    engine_clip_add_automation_point(Engine* engine,
                                         int track_index,
                                         int clip_index,
                                         EngineAutomationTarget target,
                                         uint64_t frame,
                                         float value,
                                         int* out_index);
// Updates an automation point and returns its new index.
bool    engine_clip_update_automation_point(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineAutomationTarget target,
                                            int point_index,
                                            uint64_t frame,
                                            float value,
                                            int* out_index);
// Removes an automation point from a clip target.
bool    engine_clip_remove_automation_point(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineAutomationTarget target,
                                            int point_index);
// Replaces the full set of automation points for a clip target.
bool    engine_clip_set_automation_lane_points(Engine* engine,
                                               int track_index,
                                               int clip_index,
                                               EngineAutomationTarget target,
                                               const EngineAutomationPoint* points,
                                               int count);
// Replaces all automation lanes for a clip.
bool    engine_clip_set_automation_lanes(Engine* engine,
                                         int track_index,
                                         int clip_index,
                                         const EngineAutomationLane* lanes,
                                         int lane_count);
bool    engine_duplicate_clip(Engine* engine, int track_index, int clip_index, uint64_t start_frame_offset, int* out_clip_index);
bool    engine_track_set_name(Engine* engine, int track_index, const char* name);
bool    engine_track_midi_instrument_enabled(const Engine* engine, int track_index);
EngineInstrumentPresetId engine_track_midi_instrument_preset(const Engine* engine, int track_index);
EngineInstrumentParams engine_track_midi_instrument_params(const Engine* engine, int track_index);
bool    engine_track_midi_get_instrument_automation_lanes(const Engine* engine,
                                                          int track_index,
                                                          const EngineAutomationLane** out_lanes,
                                                          int* out_lane_count);
bool    engine_track_midi_set_instrument_preset(Engine* engine,
                                                int track_index,
                                                EngineInstrumentPresetId preset);
bool    engine_track_midi_set_instrument_params(Engine* engine,
                                                int track_index,
                                                EngineInstrumentParams params);
bool    engine_track_midi_set_instrument_enabled(Engine* engine, int track_index, bool enabled);
bool    engine_track_midi_set_instrument_automation_lanes(Engine* engine,
                                                          int track_index,
                                                          const EngineAutomationLane* lanes,
                                                          int lane_count);
bool    engine_track_midi_set_instrument_automation_lane_points(Engine* engine,
                                                                int track_index,
                                                                EngineAutomationTarget target,
                                                                const EngineAutomationPoint* points,
                                                                int count);
bool    engine_add_clip_segment(Engine* engine, int track_index, const EngineClip* source_clip,
                                uint64_t source_relative_offset_frames,
                                uint64_t segment_length_frames,
                                uint64_t start_frame,
                                int* out_clip_index);
const char* engine_clip_get_media_id(const EngineClip* clip);
const char* engine_clip_get_media_path(const EngineClip* clip);
EngineClipKind engine_clip_get_kind(const EngineClip* clip);
bool    engine_clip_midi_add_note(Engine* engine,
                                  int track_index,
                                  int clip_index,
                                  EngineMidiNote note,
                                  int* out_note_index);
bool    engine_clip_midi_update_note(Engine* engine,
                                     int track_index,
                                     int clip_index,
                                     int note_index,
                                     EngineMidiNote note,
                                     int* out_note_index);
bool    engine_clip_midi_remove_note(Engine* engine, int track_index, int clip_index, int note_index);
bool    engine_clip_midi_set_notes(Engine* engine,
                                   int track_index,
                                   int clip_index,
                                   const EngineMidiNote* notes,
                                   int note_count);
int     engine_clip_midi_note_count(const EngineClip* clip);
const EngineMidiNote* engine_clip_midi_notes(const EngineClip* clip);
EngineInstrumentPresetId engine_clip_midi_instrument_preset(const EngineClip* clip);
bool    engine_clip_midi_inherits_track_instrument(const EngineClip* clip);
bool    engine_clip_midi_set_inherits_track_instrument(Engine* engine,
                                                       int track_index,
                                                       int clip_index,
                                                       bool inherits_track);
EngineInstrumentPresetId engine_clip_midi_effective_instrument_preset(const Engine* engine,
                                                                      int track_index,
                                                                      int clip_index);
EngineInstrumentParams engine_clip_midi_effective_instrument_params(const Engine* engine,
                                                                    int track_index,
                                                                    int clip_index);
bool    engine_clip_midi_set_instrument_preset(Engine* engine,
                                               int track_index,
                                               int clip_index,
                                               EngineInstrumentPresetId preset);
EngineInstrumentParams engine_clip_midi_instrument_params(const EngineClip* clip);
bool    engine_clip_midi_set_instrument_params(Engine* engine,
                                               int track_index,
                                               int clip_index,
                                               EngineInstrumentParams params);
bool    engine_clip_midi_set_instrument_param(Engine* engine,
                                              int track_index,
                                              int clip_index,
                                              EngineInstrumentParamId param,
                                              float value);
bool    engine_midi_audition_note_on(Engine* engine,
                                     int track_index,
                                     EngineInstrumentPresetId preset,
                                     EngineInstrumentParams params,
                                     uint8_t note,
                                     float velocity);
bool    engine_midi_audition_note_off(Engine* engine, uint8_t note);
void    engine_midi_audition_all_notes_off(Engine* engine);
bool    engine_track_apply_no_overlap(Engine* engine,
                                      int track_index,
                                      struct EngineSamplerSource* anchor_sampler,
                                      int* out_anchor_index);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
bool    engine_track_set_solo(Engine* engine, int track_index, bool solo);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
bool    engine_set_record_armed_track(Engine* engine, int track_index);
// Holds the scalar mixer and MIDI instrument settings restored together by track history.
typedef struct EngineTrackSettings {
    float gain;
    float pan;
    bool muted;
    bool solo;
    bool instrument_enabled;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
} EngineTrackSettings;
// Publishes complete track settings once, preserving all previous settings on rejection.
bool engine_track_set_settings(Engine* engine, int track_index, const EngineTrackSettings* settings);
bool    engine_track_set_gain(Engine* engine, int track_index, float gain);
bool    engine_track_set_pan(Engine* engine, int track_index, float pan);
bool    engine_set_master_eq_curve(Engine* engine, const EngineEqCurve* curve);
bool    engine_set_track_eq_curve(Engine* engine, int track_index, const EngineEqCurve* curve);
int     engine_get_spectrum_snapshot(const Engine* engine, float* out_bins, int max_bins);
int     engine_get_track_spectrum_snapshot(const Engine* engine, int track_index, float* out_bins, int max_bins);
void    engine_set_spectrum_target(Engine* engine, EngineSpectrumView view, int track_index, bool enabled);
// Copies the latest master meter snapshot for UI display.
bool    engine_get_master_meter_snapshot(const Engine* engine, EngineMeterSnapshot* out_snapshot);
// Copies the latest track meter snapshot for UI display.
bool    engine_get_track_meter_snapshot(const Engine* engine, int track_index, EngineMeterSnapshot* out_snapshot);
// Sets the active FX meter tap so the engine only updates one meter snapshot.
void    engine_set_active_fx_meter(Engine* engine, bool is_master, int track_index, FxInstId id);
// Copies the latest master FX meter snapshot for a specific instance id.
bool    engine_get_master_fx_meter_snapshot(const Engine* engine, FxInstId id, EngineFxMeterSnapshot* out_snapshot);
// Copies the latest track FX meter snapshot for a specific instance id.
bool    engine_get_track_fx_meter_snapshot(const Engine* engine, int track_index, FxInstId id, EngineFxMeterSnapshot* out_snapshot);
// Copies the latest spectrogram history into out_frames (newest-first rows).
bool    engine_get_fx_spectrogram_snapshot(const Engine* engine,
                                           EngineSpectrogramSnapshot* out_meta,
                                           float* out_frames,
                                           int max_frames,
                                           int max_bins);
// Enables or disables spectrogram capture for a track FX meter instance.
void    engine_set_fx_spectrogram_target(Engine* engine, int track_index, FxInstId id, bool enabled);
void    engine_set_logging(Engine* engine, bool engine_logs, bool cache_logs, bool timing_logs);
// Supplies the legacy exact-range, cold-start, normalize-if-clipping policy.
EngineBounceOptions engine_bounce_options_default(void);
// Captures and renders authored state independently; failure preserves an empty caller-owned output.
bool engine_bounce_range_to_buffer_with_options(Engine* engine, uint64_t start_frame, uint64_t end_frame,
    const EngineBounceOptions* options,
    void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
    void* user, EngineBounceBuffer* out_buffer);
// Atomically writes only the requested WAV encoding, with repeatable PCM16 dither.
bool engine_bounce_write_wav(const EngineBounceBuffer* buffer, const char* path, EngineBounceWavFormat format);
// Renders an offline bounce directly into an owned interleaved float buffer.
bool    engine_bounce_range_to_buffer(Engine* engine,
                                      uint64_t start_frame,
                                      uint64_t end_frame,
                                      void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                                      void* user,
                                      EngineBounceBuffer* out_buffer);
// Releases memory owned by an EngineBounceBuffer.
void    engine_bounce_buffer_free(EngineBounceBuffer* buffer);
bool    engine_bounce_range(Engine* engine,
                            uint64_t start_frame,
                            uint64_t end_frame,
                            const char* out_path,
                            void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                            void* user);

// Identifies the only thread allowed to inspect or mutate editable project state.
bool engine_is_control_thread(const Engine* engine);
// Reads accepted control EQ parameters, using a negative track index for the master.
bool engine_get_eq_curve(const Engine* engine, int track_index, EngineEqCurve* out_curve);

// Supplies cancellation/progress and a borrowed normalized chunk observer for streaming exports.
typedef struct EngineBounceStreamCallbacks {
    bool (*progress)(uint64_t completed, uint64_t total, void* user);
    void (*samples)(const float* samples, uint64_t first_frame, uint32_t frames, int channels, void* user);
    void* user;
} EngineBounceStreamCallbacks;

// Streams one captured render through an anonymous float spool into a durably published WAV.
DawSaveResult engine_bounce_range_to_wav(Engine* engine, uint64_t start, uint64_t end,
                                        const EngineBounceOptions* options, const char* path,
                                        EngineBounceWavFormat format, const EngineBounceStreamCallbacks* callbacks);

// Confirms that an anchored take still belongs to the active non-looping epoch without reading clock diagnostics.
bool engine_capture_epoch_is_current(const Engine* engine, uint64_t epoch);

// Publishes privately prepared media through the existing transactional clip path; adoption may consume samples even if publication fails.
bool engine_add_prepared_clip(Engine* engine, int track, const char* path, const char* media_id,
                              uint64_t start_frame, struct AudioMediaClip* prepared, int* out_clip);
// Reports pinned decoded residency independently of pending import memory.
size_t engine_media_resident_bytes(const Engine* engine);

// Checks current-version cache residency without opening a decoder.
bool engine_has_cached_media(Engine* engine, const char* path, const char* media_id);
// Publishes a resident source with no decode, reporting a cache miss separately from publication failure.
bool engine_add_cached_clip(Engine* engine, int track, const char* path, const char* media_id,
                             uint64_t start_frame, int* out_clip, bool* available);

// Reclaims only source plans already retired by the worker, allowing idle cache pins to settle.
void engine_collect_retired_media(Engine* engine);

// Reports lifetime analyzer counters and approximate pending packets; timing covers only transforms.
typedef struct EngineAnalysisDiagnostics {
    uint64_t queued, consumed, published, dropped, stale, pending, backlog_high_water, computed;
    double compute_total_ms, compute_max_ms;
} EngineAnalysisDiagnostics;

// Reads independent atomic counters for the spectrum or spectrogram consumer without blocking it.
bool engine_get_analysis_diagnostics(const Engine* engine, bool spectrogram, EngineAnalysisDiagnostics* out);

// Opaque immutable affected-track clip content, invalidated when its owning engine is destroyed.
typedef struct EngineClipContentSnapshot EngineClipContentSnapshot;
// Captures a complete track, or prepares a new empty track when index is -1.
EngineClipContentSnapshot* engine_track_history_capture(Engine* engine, int track_index);
// Restores retained track content and its chain in one publication without replacing neighbors.
bool engine_track_history_insert(Engine* engine, int track_index, const EngineClipContentSnapshot* snapshot);
// Removes the captured track by runtime identity, rejecting a missing target.
bool engine_track_history_remove(Engine* engine, const EngineClipContentSnapshot* snapshot);
// Captures existing tracks with owned metadata and retained decoded media versions.
EngineClipContentSnapshot* engine_clip_content_capture(Engine* engine, const int* tracks, int count);
// Shares a live snapshot on the control thread.
bool engine_clip_content_retain(EngineClipContentSnapshot* snapshot);
// Releases a live or project-invalidated snapshot safely.
void engine_clip_content_release(EngineClipContentSnapshot* snapshot);
// Publishes all captured track content together or preserves the current project.
bool engine_clip_content_restore(Engine* engine, const EngineClipContentSnapshot* snapshot);
// Publishes placement, required track growth and overlap with retained before/after content for atomic history.
bool engine_clip_content_drop(Engine* engine, const EngineClipBatchTransform* initial,
                              const EngineClipBatchTransform* final, int count,
                              EngineClipContentSnapshot** out_before, EngineClipContentSnapshot** out_after);

// Duplicates or removes every stable target together and returns retained content history on success.
bool engine_clip_content_edit(Engine* engine, const uint64_t* identities, int count, bool duplicate, uint64_t gap,
                              uint64_t* output_ids, EngineClipContentSnapshot** before, EngineClipContentSnapshot** after);

// Borrows authored clip fields while a complete insertion batch prepares its owned render sources.
typedef struct {
    EngineClipKind kind;
    const char* media_id;
    const char* media_path;
    const char* name;
    EngineClipTransform transform;
    const EngineAutomationLane* automation_lanes;
    int automation_lane_count;
} EngineClipInsert;

// Inserts every prepared descriptor on one destination, including required tracks, with retained history.
bool engine_clip_content_insert(Engine* engine, int destination, const EngineClipInsert* clips, int count,
                                uint64_t* output_ids, EngineClipContentSnapshot** before, EngineClipContentSnapshot** after);
