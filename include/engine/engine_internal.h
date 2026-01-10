#pragma once

#include "engine/engine.h"
#include "engine/graph.h"
#include "engine/sources.h"
#include "engine/ringbuf.h"

#include "audio/media_cache.h"
#include "audio/audio_device.h"
#include "audio/audio_queue.h"

#include "effects/effects_manager.h"

#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENGINE_CMD_PLAY = 1,
    ENGINE_CMD_STOP = 2,
    ENGINE_CMD_GRAPH_SWAP = 3,
    ENGINE_CMD_SEEK = 4,
    ENGINE_CMD_SET_LOOP = 5,
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
    } payload;
} EngineCommand;

typedef struct EngineFxSnapshot {
    FxMasterSnapshot master;
    FxMasterSnapshot* tracks;
    int track_count;
} EngineFxSnapshot;

struct Engine {
    EngineRuntimeConfig config;
    AudioDevice device;
    bool device_started;
    AudioQueue output_queue;
    SDL_Thread* worker_thread;
    atomic_bool worker_running;
    atomic_bool transport_playing;
    RingBuffer command_queue;
    EffectsManager* fxm;
    SDL_mutex* fxm_mutex;
    EngineGraph* graph;
    EngineToneSource* tone_source;
    EngineGraphSourceOps tone_ops;
    EngineGraphSourceOps sampler_ops;
    EngineTrack* tracks;
    int track_count;
    int track_capacity;
    uint64_t transport_frame;
    uint64_t next_clip_id;
    AudioMediaCache media_cache;
    atomic_bool loop_enabled;
    atomic_uint_fast64_t loop_start_frame;
    atomic_uint_fast64_t loop_end_frame;
    SDL_mutex* spectrum_mutex;
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
    atomic_bool spectrum_enabled;
    atomic_int spectrum_view;
    atomic_int spectrum_target_track;
};

void engine_trace(const Engine* engine, const char* fmt, ...);
void engine_timing_trace(const Engine* engine, const char* fmt, ...);
void engine_audio_callback(float* output, int frames, int channels, void* userdata);
void engine_sanitize_block(float* buf, size_t samples);
bool engine_post_command(Engine* engine, const EngineCommand* cmd);
void engine_rebuild_sources(Engine* engine);
void engine_mix_tracks(Engine* engine,
                       uint64_t transport_frame,
                       int frames,
                       float* interleaved_out,
                       float* track_buffer,
                       int channels);
bool engine_spectrum_begin_block(Engine* engine);
void engine_spectrum_update(Engine* engine, const float* interleaved, int frames, int channels);
void engine_spectrum_update_track(Engine* engine, int track_index, const float* interleaved, int frames, int channels);
void engine_clip_destroy(Engine* engine, EngineClip* clip);
void engine_track_init(EngineTrack* track);
void engine_track_clear(Engine* engine, EngineTrack* track);
EngineTrack* engine_get_track_mutable(Engine* engine, int track_index);
bool engine_ensure_track_capacity(Engine* engine, int required_tracks);
bool engine_fx_snapshot_all(Engine* engine, EngineFxSnapshot* out_snap);
void engine_fx_restore_all(Engine* engine, const EngineFxSnapshot* snap);
