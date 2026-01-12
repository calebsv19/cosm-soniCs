#pragma once

#include "config.h"
#include "effects/effects_manager.h"
#include "engine/engine_eq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AudioMediaClip;
typedef struct Engine Engine;
struct EngineGraph;
typedef struct EngineTrack EngineTrack;
typedef struct EngineClip EngineClip;
typedef struct EngineAudioSource EngineAudioSource;
struct EngineSamplerSource;

#define ENGINE_CLIP_NAME_MAX 128
#define ENGINE_CLIP_PATH_MAX 512
#define ENGINE_MEDIA_ID_MAX 33
#define ENGINE_SPECTRUM_BINS 256
#define ENGINE_SPECTRUM_HISTORY 4
#define ENGINE_SPECTRUM_MIN_HZ 20.0f
#define ENGINE_SPECTRUM_MAX_HZ 20000.0f
#define ENGINE_SPECTRUM_DB_FLOOR -60.0f
#define ENGINE_SPECTRUM_DB_CEIL 6.0f
#define ENGINE_FX_METER_VEC_POINTS 64

typedef enum {
    ENGINE_SPECTRUM_VIEW_MASTER = 0,
    ENGINE_SPECTRUM_VIEW_TRACK
} EngineSpectrumView;

// Snapshot of linear peak/RMS values plus clip hold for meters.
typedef struct {
    float peak;
    float rms;
    bool clipped;
} EngineMeterSnapshot;

// Snapshot of per-FX metering values for analysis modules.
typedef struct {
    FxTypeId type;
    float peak;
    float rms;
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

struct EngineClip {
    struct EngineSamplerSource* sampler;
    struct AudioMediaClip* media;
    EngineAudioSource* source;
    float gain;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
    uint64_t timeline_start_frames;
    uint64_t duration_frames;
    uint64_t offset_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    uint64_t creation_index;
    bool selected;
};

struct EngineTrack {
    EngineClip* clips;
    int clip_count;
    int clip_capacity;
    float gain;
    float pan;
    bool muted;
    bool solo;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
    EngineEqState track_eq;
};

Engine* engine_create(const EngineRuntimeConfig* cfg);
void    engine_destroy(Engine* engine);
bool    engine_start(Engine* engine);
void    engine_stop(Engine* engine);
const EngineRuntimeConfig* engine_get_config(const Engine* engine);
bool    engine_fx_get_registry(const Engine* engine, const FxRegistryEntry** out_entries, int* out_count);
bool    engine_fx_registry_get_desc(const Engine* engine, FxTypeId type, FxDesc* out_desc);
bool    engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot);
FxInstId engine_fx_master_add(Engine* engine, FxTypeId type);
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
bool    engine_transport_is_playing(const Engine* engine);
bool    engine_transport_seek(Engine* engine, uint64_t frame);
bool    engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame);
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
int     engine_add_track(Engine* engine);
bool    engine_insert_track(Engine* engine, int track_index);
bool    engine_remove_track(Engine* engine, int track_index);
const EngineTrack* engine_get_tracks(const Engine* engine);
int     engine_get_track_count(const Engine* engine);
uint64_t engine_get_transport_frame(const Engine* engine);
bool    engine_clip_set_timeline_start(Engine* engine, int track_index, int clip_index, uint64_t start_frame, int* out_clip_index);
bool    engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames);
uint64_t engine_clip_get_total_frames(const Engine* engine, int track_index, int clip_index);
bool    engine_remove_clip(Engine* engine, int track_index, int clip_index);
bool    engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name);
bool    engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain);
bool    engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames);
bool    engine_duplicate_clip(Engine* engine, int track_index, int clip_index, uint64_t start_frame_offset, int* out_clip_index);
bool    engine_track_set_name(Engine* engine, int track_index, const char* name);
bool    engine_add_clip_segment(Engine* engine, int track_index, const EngineClip* source_clip,
                                uint64_t source_relative_offset_frames,
                                uint64_t segment_length_frames,
                                uint64_t start_frame,
                                int* out_clip_index);
const char* engine_clip_get_media_id(const EngineClip* clip);
const char* engine_clip_get_media_path(const EngineClip* clip);
bool    engine_track_apply_no_overlap(Engine* engine,
                                      int track_index,
                                      struct EngineSamplerSource* anchor_sampler,
                                      int* out_anchor_index);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
bool    engine_track_set_solo(Engine* engine, int track_index, bool solo);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
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
void    engine_set_logging(Engine* engine, bool engine_logs, bool cache_logs, bool timing_logs);
bool    engine_bounce_range(Engine* engine,
                            uint64_t start_frame,
                            uint64_t end_frame,
                            const char* out_path,
                            void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                            void* user);
