#pragma once

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Engine Engine;
struct EngineGraph;
typedef struct EngineTrack EngineTrack;
typedef struct EngineClip EngineClip;
struct EngineSamplerSource;

struct EngineClip {
    struct EngineSamplerSource* sampler;
    float gain;
    bool active;
};

struct EngineTrack {
    EngineClip* clips;
    int clip_count;
    int clip_capacity;
    float gain;
    bool active;
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
bool    engine_queue_graph_swap(Engine* engine, struct EngineGraph* new_graph);
bool    engine_load_wav(Engine* engine, const char* path);
bool    engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame);
const EngineTrack* engine_get_tracks(const Engine* engine);
int     engine_get_track_count(const Engine* engine);
uint64_t engine_get_transport_frame(const Engine* engine);
void    engine_set_clip_gain(Engine* engine, int track_index, int clip_index, float gain);
