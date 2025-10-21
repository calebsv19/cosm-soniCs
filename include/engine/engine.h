#pragma once

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AudioMediaClip;
typedef struct Engine Engine;
struct EngineGraph;
typedef struct EngineTrack EngineTrack;
typedef struct EngineClip EngineClip;
struct EngineSamplerSource;

#define ENGINE_CLIP_NAME_MAX 128
#define ENGINE_CLIP_PATH_MAX 512

struct EngineClip {
    struct EngineSamplerSource* sampler;
    struct AudioMediaClip* media;
    float gain;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
    char media_path[ENGINE_CLIP_PATH_MAX];
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
    bool muted;
    bool solo;
    bool active;
    char name[ENGINE_CLIP_NAME_MAX];
};

Engine* engine_create(const EngineRuntimeConfig* cfg);
void    engine_destroy(Engine* engine);
bool    engine_start(Engine* engine);
void    engine_stop(Engine* engine);
const EngineRuntimeConfig* engine_get_config(const Engine* engine);
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
int     engine_add_track(Engine* engine);
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
bool    engine_track_apply_no_overlap(Engine* engine,
                                      int track_index,
                                      struct EngineSamplerSource* anchor_sampler,
                                      int* out_anchor_index);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
bool    engine_track_set_solo(Engine* engine, int track_index, bool solo);
bool    engine_track_set_muted(Engine* engine, int track_index, bool muted);
bool    engine_track_set_gain(Engine* engine, int track_index, float gain);
void    engine_set_logging(Engine* engine, bool engine_logs, bool cache_logs, bool timing_logs);
