#include "engine/engine.h"
#include "engine/graph.h"
#include "engine/sampler.h"
#include "engine/sources.h"
#include "engine/ringbuf.h"

#include "audio/media_clip.h"
#include "audio/media_cache.h"
#include "audio/audio_device.h"
#include "audio/audio_queue.h"
#include "audio/wav_writer.h"

#include "effects/effects_manager.h"
#include "effects/effects_api.h"
#include "effects/effects_builtin.h"



#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

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
};




static void engine_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !engine->config.enable_engine_logs || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, args);
    va_end(args);
}

static void engine_timing_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !engine->config.enable_timing_logs || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE, fmt, args);
    va_end(args);
}

static inline float sanitize_sample(float v) {
    if (!isfinite(v) || fabsf(v) > 64.0f) {
        return 0.0f;
    }
    if (fabsf(v) < 1e-12f) {
        return 0.0f;
    }
    return v;
}

static void sanitize_block(float* buf, size_t samples) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < samples; ++i) {
        buf[i] = sanitize_sample(buf[i]);
    }
}

static void engine_clip_destroy(Engine* engine, EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->sampler) {
        engine_sampler_source_destroy(clip->sampler);
        clip->sampler = NULL;
    }
    if (clip->media) {
        if (engine) {
            audio_media_cache_release(&engine->media_cache, clip->media);
        } else {
            audio_media_clip_free(clip->media);
            free(clip->media);
        }
        clip->media = NULL;
    }
    clip->gain = 0.0f;
    clip->active = false;
    clip->name[0] = '\0';
    clip->media_path[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->creation_index = 0;
    clip->selected = false;
    clip->media = NULL;
}

static void engine_clip_set_name_from_path(EngineClip* clip, const char* path) {
    if (!clip) {
        return;
    }
    clip->name[0] = '\0';
    if (!path) {
        return;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    char temp[ENGINE_CLIP_NAME_MAX];
    strncpy(temp, base, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* dot = strrchr(temp, '.');
    if (dot) {
        *dot = '\0';
    }
    strncpy(clip->name, temp, sizeof(clip->name) - 1);
    clip->name[sizeof(clip->name) - 1] = '\0';
}

static void engine_track_init(EngineTrack* track) {
    if (!track) {
        return;
    }
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->muted = false;
    track->solo = false;
    track->active = true;
    track->name[0] = '\0';
}

static void engine_track_clear(Engine* engine, EngineTrack* track) {
    if (!track) {
        return;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        engine_clip_destroy(engine, &track->clips[i]);
    }
    free(track->clips);
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->muted = false;
    track->solo = false;
    track->active = true;
    track->name[0] = '\0';
}

static bool engine_ensure_track_capacity(Engine* engine, int required_tracks) {
    if (!engine) {
        return false;
    }
    if (required_tracks <= engine->track_capacity) {
        return true;
    }
    int new_capacity = engine->track_capacity;
    while (new_capacity < required_tracks) {
        new_capacity *= 2;
    }
    EngineTrack* resized = (EngineTrack*)realloc(engine->tracks, sizeof(EngineTrack) * (size_t)new_capacity);
    if (!resized) {
        return false;
    }
    engine->tracks = resized;
    for (int i = engine->track_capacity; i < new_capacity; ++i) {
        engine_track_init(&engine->tracks[i]);
    }
    engine->track_capacity = new_capacity;
    return true;
}

static EngineTrack* engine_get_track_mutable(Engine* engine, int track_index) {
    if (!engine || track_index < 0) {
        return NULL;
    }
    if (!engine_ensure_track_capacity(engine, track_index + 1)) {
        return NULL;
    }
    while (engine->track_count <= track_index) {
        engine_track_init(&engine->tracks[engine->track_count]);
        engine->tracks[engine->track_count].active = false;
        ++engine->track_count;
    }
    return &engine->tracks[track_index];
}

static EngineClip* engine_track_append_clip(Engine* engine, EngineTrack* track) {
    if (!engine || !track) {
        return NULL;
    }
    if (track->clip_count == track->clip_capacity) {
        int new_cap = track->clip_capacity == 0 ? 4 : track->clip_capacity * 2;
        EngineClip* new_clips = (EngineClip*)realloc(track->clips, sizeof(EngineClip) * (size_t)new_cap);
        if (!new_clips) {
            return NULL;
        }
        track->clips = new_clips;
        track->clip_capacity = new_cap;
    }
    EngineClip* clip = &track->clips[track->clip_count++];
    clip->sampler = NULL;
    clip->media = NULL;
    clip->gain = 1.0f;
    clip->active = true;
    clip->name[0] = '\0';
    clip->media_path[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->creation_index = engine->next_clip_id++;
    clip->selected = false;
    return clip;
}

static int engine_clip_compare_timeline(const void* a, const void* b) {
    const EngineClip* ca = (const EngineClip*)a;
    const EngineClip* cb = (const EngineClip*)b;
    if (ca->timeline_start_frames < cb->timeline_start_frames) {
        return -1;
    } else if (ca->timeline_start_frames > cb->timeline_start_frames) {
        return 1;
    }
    if (ca->creation_index < cb->creation_index) {
        return -1;
    }
    if (ca->creation_index > cb->creation_index) {
        return 1;
    }
    return 0;
}

static void engine_track_sort_clips(EngineTrack* track) {
    if (!track || track->clip_count <= 1 || !track->clips) {
        return;
    }
    qsort(track->clips, (size_t)track->clip_count, sizeof(EngineClip), engine_clip_compare_timeline);
}

static uint64_t engine_clip_effective_duration(const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    if (clip->duration_frames > 0) {
        return clip->duration_frames;
    }
    if (!clip->media || clip->media->frame_count == 0) {
        return 0;
    }
    if (clip->offset_frames >= clip->media->frame_count) {
        return 0;
    }
    return clip->media->frame_count - clip->offset_frames;
}

static int engine_track_find_clip_by_sampler(const EngineTrack* track, EngineSamplerSource* sampler) {
    if (!track || !sampler) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].sampler == sampler) {
            return i;
        }
    }
    return -1;
}

static uint64_t engine_ms_to_frames(const EngineRuntimeConfig* cfg, float ms) {
    if (!cfg || cfg->sample_rate <= 0 || ms <= 0.0f) {
        return 0;
    }
    double frames = (double)cfg->sample_rate * (double)ms / 1000.0;
    if (frames <= 0.0) {
        return 0;
    }
    return (uint64_t)(frames + 0.5);
}

static void engine_compute_default_fades(const EngineRuntimeConfig* cfg,
                                         uint64_t clip_length,
                                         uint64_t* out_fade_in,
                                         uint64_t* out_fade_out) {
    if (!out_fade_in || !out_fade_out) {
        return;
    }
    *out_fade_in = 0;
    *out_fade_out = 0;
    if (!cfg || clip_length == 0) {
        return;
    }

    uint64_t fade_in = engine_ms_to_frames(cfg, cfg->default_fade_in_ms);
    uint64_t fade_out = engine_ms_to_frames(cfg, cfg->default_fade_out_ms);

    if (fade_in > clip_length) {
        fade_in = clip_length;
    }
    if (fade_out > clip_length) {
        fade_out = clip_length;
    }
    if (fade_in + fade_out > clip_length) {
        uint64_t excess = (fade_in + fade_out) - clip_length;
        if (fade_out >= excess) {
            fade_out -= excess;
        } else if (fade_in >= excess) {
            fade_in -= excess;
        } else {
            fade_in = 0;
            fade_out = 0;
        }
    }

    *out_fade_in = fade_in;
    *out_fade_out = fade_out;
}

static void engine_clip_refresh_sampler(EngineClip* clip) {
    if (!clip || !clip->sampler || !clip->media) {
        return;
    }
    engine_sampler_source_set_clip(clip->sampler, clip->media,
                                   clip->timeline_start_frames,
                                   clip->offset_frames,
                                   clip->duration_frames,
                                   clip->fade_in_frames,
                                   clip->fade_out_frames);
}

static bool engine_post_command(Engine* engine, const EngineCommand* cmd) {
    if (!engine || !cmd) {
        return false;
    }
    return ringbuf_write(&engine->command_queue, cmd, sizeof(*cmd)) == sizeof(*cmd);
}

static void engine_rebuild_sources(Engine* engine) {
    if (!engine || !engine->graph) {
        return;
    }
    if (engine->fxm && engine->fxm_mutex) {
        SDL_LockMutex(engine->fxm_mutex);
        fxm_set_track_count(engine->fxm, engine->track_count);
        SDL_UnlockMutex(engine->fxm_mutex);
    }
    engine_graph_clear_sources(engine->graph);
    bool any_solo = false;
    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (!track->active || track->clip_count == 0 || track->muted) {
            continue;
        }
        if (track->solo) {
            any_solo = true;
            break;
        }
    }

    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (!track->active || track->clip_count == 0 || track->muted) {
            continue;
        }
        if (any_solo && !track->solo) {
            continue;
        }
        float track_gain = track->gain != 0.0f ? track->gain : 1.0f;
        for (int c = 0; c < track->clip_count; ++c) {
            EngineClip* clip = &track->clips[c];
            if (!clip->active || !clip->sampler) {
                continue;
            }
            float clip_gain = clip->gain != 0.0f ? clip->gain : 1.0f;
            engine_graph_add_source(engine->graph, &engine->sampler_ops, clip->sampler, track_gain * clip_gain, i);
        }
    }
    engine_graph_reset(engine->graph);
}

static void engine_audio_callback(float* output, int frames, int channels, void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!output || frames <= 0 || !engine) {
        return;
    }
    size_t grabbed = audio_queue_read(&engine->output_queue, output, (size_t)frames);
    if (grabbed < (size_t)frames) {
        size_t missing = (size_t)frames - grabbed;
        memset(output + grabbed * channels, 0, missing * (size_t)channels * sizeof(float));
    }
}

static void engine_process_commands(Engine* engine) {
    EngineCommand cmd;
    while (ringbuf_read(&engine->command_queue, &cmd, sizeof(cmd)) == sizeof(cmd)) {
        switch (cmd.type) {
            case ENGINE_CMD_PLAY:
                atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
                break;
        case ENGINE_CMD_STOP:
            atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
            audio_queue_clear(&engine->output_queue);
            engine_graph_reset(engine->graph);
            break;
            case ENGINE_CMD_GRAPH_SWAP:
                if (cmd.payload.graph_swap.new_graph) {
                    EngineGraph* old = engine->graph;
                    engine->graph = cmd.payload.graph_swap.new_graph;
                    if (old) {
                        engine_graph_destroy(old);
                    }
                    engine_graph_reset(engine->graph);
                }
                break;
            case ENGINE_CMD_SEEK:
                engine->transport_frame = cmd.payload.seek.frame;
                audio_queue_clear(&engine->output_queue);
                engine_graph_reset(engine->graph);
                break;
            case ENGINE_CMD_SET_LOOP:
                atomic_store_explicit(&engine->loop_enabled, cmd.payload.loop.enabled, memory_order_release);
                atomic_store_explicit(&engine->loop_start_frame, cmd.payload.loop.start_frame, memory_order_release);
                atomic_store_explicit(&engine->loop_end_frame, cmd.payload.loop.end_frame, memory_order_release);
                break;
            default:
                break;
        }
    }
}

static void engine_mix_tracks(Engine* engine,
                              uint64_t start_frame,
                              int frames,
                              float* out,
                              float* track_buffer,
                              int channels) {
    if (!engine || !out || !track_buffer || frames <= 0 || channels <= 0) {
        return;
    }
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    int tcount = engine->track_count;
    for (int t = 0; t < tcount; ++t) {
        memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        engine_graph_render_track(engine->graph,
                                  track_buffer,
                                  frames,
                                  start_frame,
                                  t);
        if (engine->fxm && engine->fxm_mutex) {
            SDL_LockMutex(engine->fxm_mutex);
            fxm_render_track(engine->fxm, t, track_buffer, frames, channels);
            SDL_UnlockMutex(engine->fxm_mutex);
        }
        for (int s = 0; s < frames * channels; ++s) {
            out[s] += track_buffer[s];
        }
    }

    if (engine->fxm && engine->fxm_mutex) {
        SDL_LockMutex(engine->fxm_mutex);
        fxm_render_master(engine->fxm, out, frames, channels);
        SDL_UnlockMutex(engine->fxm_mutex);
    }

    sanitize_block(out, (size_t)frames * (size_t)channels);
}

static int engine_worker_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    int channels = engine_graph_get_channels(engine->graph);
    if (channels <= 0) {
        channels = engine->output_queue.channels > 0 ? engine->output_queue.channels : 2;
    }
    const int block = engine->config.block_size;
    float* block_buffer = (float*)malloc((size_t)block * (size_t)channels * sizeof(float));
    float* track_buffer = (float*)malloc((size_t)block * (size_t)channels * sizeof(float));
    if (!block_buffer || !track_buffer) {
        return -1;
    }

    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_report_counter = SDL_GetPerformanceCounter();
    double accum_ms = 0.0;
    int accum_blocks = 0;

    while (atomic_load_explicit(&engine->worker_running, memory_order_acquire)) {
        engine_process_commands(engine);
        if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
            SDL_Delay(2);
            continue;
        }

        if (audio_queue_space_frames(&engine->output_queue) < (size_t)block) {
            SDL_Delay(1);
            continue;
        }

        if (!engine->config.enable_timing_logs) {
            accum_ms = 0.0;
            accum_blocks = 0;
            last_report_counter = SDL_GetPerformanceCounter();
        }

        int graph_channels = engine_graph_get_channels(engine->graph);
        if (graph_channels <= 0) {
            graph_channels = channels;
        }
        if (graph_channels != channels) {
            float* new_buffer = (float*)realloc(block_buffer, (size_t)block * (size_t)graph_channels * sizeof(float));
            if (!new_buffer) {
                break;
            }
            block_buffer = new_buffer;
            channels = graph_channels;
        }

        int frames_remaining = block;
        int produced = 0;
        while (frames_remaining > 0) {
            bool loop_enabled = atomic_load_explicit(&engine->loop_enabled, memory_order_acquire);
            uint64_t loop_start = atomic_load_explicit(&engine->loop_start_frame, memory_order_acquire);
            uint64_t loop_end = atomic_load_explicit(&engine->loop_end_frame, memory_order_acquire);
            if (loop_enabled && loop_end <= loop_start) {
                loop_enabled = false;
            }
            int chunk = frames_remaining;
            if (loop_enabled) {
                uint64_t current = engine->transport_frame;
                uint64_t loop_len = loop_end - loop_start;
                if (loop_len == 0) {
                    loop_enabled = false;
                } else {
                    uint64_t frames_until_end = (current < loop_end) ? (loop_end - current) : 0;
                    if (frames_until_end == 0) {
                        engine->transport_frame = loop_start;
                        engine_graph_reset(engine->graph);
                        current = loop_start;
                        frames_until_end = loop_len;
                    }
                    if (frames_until_end > 0 && (uint64_t)chunk > frames_until_end) {
                        chunk = (int)frames_until_end;
                    }
                }
            }

            if (chunk <= 0) {
                chunk = frames_remaining;
            }

            Uint64 start_counter = engine->config.enable_timing_logs ? SDL_GetPerformanceCounter() : 0;

            // mix tracks with per-track FX
            float* out_ptr = block_buffer + produced * channels;
            engine_mix_tracks(engine, engine->transport_frame, chunk, out_ptr, track_buffer, channels);

            engine->transport_frame += (uint64_t)chunk;
            produced += chunk;
            frames_remaining -= chunk;

            if (engine->config.enable_timing_logs && perf_freq > 0) {
                Uint64 end_counter = SDL_GetPerformanceCounter();
                double elapsed_ms = (double)(end_counter - start_counter) * 1000.0 / (double)perf_freq;
                accum_ms += elapsed_ms;
                accum_blocks += 1;
                if ((end_counter - last_report_counter) >= perf_freq && accum_blocks > 0) {
                    double avg_ms = accum_ms / (double)accum_blocks;
                    size_t queued = audio_queue_available_frames(&engine->output_queue);
                    engine_timing_trace(engine, "worker avg render %.3fms (%d blocks) queue=%zu", avg_ms, accum_blocks, queued);
                    accum_ms = 0.0;
                    accum_blocks = 0;
                    last_report_counter = end_counter;
                }
            }

            if (loop_enabled) {
                uint64_t loop_start_cur = atomic_load_explicit(&engine->loop_start_frame, memory_order_acquire);
                uint64_t loop_end_cur = atomic_load_explicit(&engine->loop_end_frame, memory_order_acquire);
                if (atomic_load_explicit(&engine->loop_enabled, memory_order_acquire) && loop_end_cur > loop_start_cur && 
				engine->transport_frame >= loop_end_cur) {
                    engine->transport_frame = loop_start_cur;
                    engine_graph_reset(engine->graph);
                }
            }
        }

        // master effects chain over the completed block
        if (engine->fxm) {
            SDL_LockMutex(engine->fxm_mutex);
            fxm_render_master(engine->fxm, block_buffer, block, channels);
            SDL_UnlockMutex(engine->fxm_mutex);
        }

        sanitize_block(block_buffer, (size_t)block * (size_t)channels);

        audio_queue_write(&engine->output_queue, block_buffer, (size_t)block);
    }

    free(block_buffer);
    free(track_buffer);
    return 0;
}


Engine* engine_create(const EngineRuntimeConfig* cfg) {
    Engine* engine = (Engine*)calloc(1, sizeof(Engine));
    if (!engine) {
        return NULL;
    }
    if (cfg) {
        engine->config = *cfg;
    } else {
        config_set_defaults(&engine->config);
    }
    engine->device_started = false;
    engine->worker_thread = NULL;
    atomic_init(&engine->worker_running, false);
    atomic_init(&engine->transport_playing, false);
    atomic_init(&engine->loop_enabled, false);
    atomic_init(&engine->loop_start_frame, 0);
    atomic_init(&engine->loop_end_frame, 0);
    if (!ringbuf_init(&engine->command_queue, sizeof(EngineCommand) * 64)) {
        free(engine);
        return NULL;
    }
    engine->fxm_mutex = SDL_CreateMutex();
    if (!engine->fxm_mutex) {
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
    if (!engine->graph) {
        SDL_DestroyMutex(engine->fxm_mutex);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine_tone_source_ops(&engine->tone_ops);
    engine_sampler_source_ops(&engine->sampler_ops);
    engine->tone_source = engine_tone_source_create();
    if (!engine->tone_source) {
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }

    engine->track_capacity = 4;
    engine->tracks = (EngineTrack*)calloc((size_t)engine->track_capacity, sizeof(EngineTrack));
    if (!engine->tracks) {
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_init(&engine->tracks[i]);
    }
    engine->track_count = 0;
    engine->transport_frame = 0;
    engine->next_clip_id = 1;

    engine_graph_clear_sources(engine->graph);
    engine_graph_add_source(engine->graph, &engine->tone_ops, engine->tone_source, 1.0f, -1);
    engine_graph_reset(engine->graph);

    audio_media_cache_init(&engine->media_cache, engine->config.enable_cache_logs);
    engine_add_track(engine);
    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_stop(engine);
    audio_device_close(&engine->device);

    // >>> NEW: destroy effects manager <<<
    if (engine->fxm) {
        fxm_destroy(engine->fxm);
        engine->fxm = NULL;
    }

    if (engine->graph) {
        engine_graph_destroy(engine->graph);
        engine->graph = NULL;
    }
    if (engine->tone_source) {
        engine_tone_source_destroy(engine->tone_source);
        engine->tone_source = NULL;
    }
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_clear(engine, &engine->tracks[i]);
    }
    free(engine->tracks);
    engine->tracks = NULL;
    engine->track_count = 0;
   engine->track_capacity = 0;
   audio_media_cache_shutdown(&engine->media_cache);
   ringbuf_free(&engine->command_queue);
   audio_queue_free(&engine->output_queue);
    if (engine->fxm_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        engine->fxm_mutex = NULL;
    }
    free(engine);
}

typedef struct EngineFxSnapshot {
    FxMasterSnapshot master;
    FxMasterSnapshot* tracks;
    int track_count;
} EngineFxSnapshot;

static bool engine_fx_snapshot_all(Engine* engine, EngineFxSnapshot* out_snap) {
    if (!engine || !engine->fxm_mutex || !out_snap) {
        return false;
    }
    SDL_zero(*out_snap);
    SDL_LockMutex(engine->fxm_mutex);
    bool ok = false;
    FxMasterSnapshot master = {0};
    if (engine->fxm && fxm_master_snapshot(engine->fxm, &master)) {
        ok = true;
    }
    int tcount = engine->track_count;
    FxMasterSnapshot* tracks = NULL;
    if (ok && tcount > 0) {
        tracks = (FxMasterSnapshot*)calloc((size_t)tcount, sizeof(FxMasterSnapshot));
        if (!tracks) {
            ok = false;
        } else {
            for (int t = 0; t < tcount; ++t) {
                fxm_track_snapshot(engine->fxm, t, &tracks[t]);
            }
        }
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!ok) {
        free(tracks);
        return false;
    }
    out_snap->master = master;
    out_snap->tracks = tracks;
    out_snap->track_count = tcount;
    return true;
}

static void engine_fx_restore_all(Engine* engine, const EngineFxSnapshot* snap) {
    if (!engine || !engine->fxm_mutex || !snap || !engine->fxm) {
        return;
    }
    SDL_LockMutex(engine->fxm_mutex);
    fxm_set_track_count(engine->fxm, snap->track_count);
    for (int i = 0; i < snap->master.count && i < FX_MASTER_MAX; ++i) {
        const FxMasterInstanceInfo* src = &snap->master.items[i];
        FxInstId id = fxm_master_add(engine->fxm, src->type);
        if (!id) continue;
        uint32_t pc = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
        for (uint32_t p = 0; p < pc; ++p) {
            FxParamMode mode = src->param_mode[p];
            float beat_value = src->param_beats[p];
            if (mode == FX_PARAM_MODE_NATIVE) {
                fxm_master_set_param(engine->fxm, id, p, src->params[p]);
            } else {
                fxm_master_set_param_with_mode(engine->fxm, id, p, src->params[p], mode, beat_value);
            }
        }
        if (!src->enabled) {
            fxm_master_set_enabled(engine->fxm, id, false);
        }
    }
    for (int t = 0; t < snap->track_count; ++t) {
        const FxMasterSnapshot* ts = &snap->tracks[t];
        for (int i = 0; i < ts->count && i < FX_MASTER_MAX; ++i) {
            const FxMasterInstanceInfo* src = &ts->items[i];
            FxInstId id = fxm_track_add(engine->fxm, t, src->type);
            if (!id) continue;
            uint32_t pc = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < pc; ++p) {
                FxParamMode mode = src->param_mode[p];
                float beat_value = src->param_beats[p];
                if (mode == FX_PARAM_MODE_NATIVE) {
                    fxm_track_set_param(engine->fxm, t, id, p, src->params[p]);
                } else {
                    fxm_track_set_param_with_mode(engine->fxm, t, id, p, src->params[p], mode, beat_value);
                }
            }
            if (!src->enabled) {
                fxm_track_set_enabled(engine->fxm, t, id, false);
            }
        }
    }
    SDL_UnlockMutex(engine->fxm_mutex);
}

bool engine_start(Engine* engine) {
    if (!engine) {
        return false;
    }

    AudioDeviceSpec want = {
        .sample_rate = engine->config.sample_rate,
        .block_size = engine->config.block_size,
        .channels = 2
    };
    EngineFxSnapshot fx_snap = {0};
    bool had_fxm = engine_fx_snapshot_all(engine, &fx_snap);

    if (!engine->device.is_open) {
        if (!audio_device_open(&engine->device, &want, engine_audio_callback, engine)) {
            SDL_Log("engine_start: failed to open audio device");
            free(fx_snap.tracks);
            return false;
        }
    }
    const AudioDeviceSpec* have = &engine->device.spec;
    engine->config.sample_rate = have->sample_rate;
    engine->config.block_size = have->block_size;

    size_t capacity_frames = (size_t)engine->config.block_size * 32;
    if (engine->output_queue.channels != have->channels || engine->output_queue.buffer.data == NULL) {
        audio_queue_free(&engine->output_queue);
        if (!audio_queue_init(&engine->output_queue, have->channels, capacity_frames)) {
            SDL_Log("engine_start: failed to init audio queue");
            return false;
        }
    } else {
        audio_queue_clear(&engine->output_queue);
    }

    ringbuf_reset(&engine->command_queue);
    if (engine_graph_configure(engine->graph, have->sample_rate, have->channels, engine->config.block_size) != 0) {
        SDL_Log("engine_start: failed to configure graph");
        return false;
    }
    engine_rebuild_sources(engine);

    FxConfig fxcfg = {
        .sample_rate  = have->sample_rate,
        .max_block    = engine->config.block_size,
        .max_channels = have->channels,
        .pool         = NULL, // not needed for interleaved v1
    };

    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        fxm_destroy(engine->fxm);
        engine->fxm = NULL;
    }

    EffectsManager* new_fxm = fxm_create(&fxcfg);
    if (!new_fxm) {
        SDL_UnlockMutex(engine->fxm_mutex);
        SDL_Log("engine_start: failed to create effects manager");
        return false;
    }

    fx_register_builtins_all(new_fxm);
    engine->fxm = new_fxm;

    if (had_fxm) {
        engine_fx_restore_all(engine, &fx_snap);
    }
    free(fx_snap.tracks);

    SDL_UnlockMutex(engine->fxm_mutex);

    engine->transport_frame = 0;

    atomic_store_explicit(&engine->worker_running, true, memory_order_release);
    engine->worker_thread = SDL_CreateThread(engine_worker_main, "engine_worker", engine);
    if (!engine->worker_thread) {
        SDL_Log("engine_start: failed to create worker thread: %s", SDL_GetError());
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        return false;
    }

    if (!audio_device_start(&engine->device)) {
        SDL_Log("engine_start: failed to start audio device");
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
        return false;
    }
    engine->device_started = true;

    SDL_Log("Audio device running: %d Hz, %d channels, block size %d",
            have->sample_rate, have->channels, have->block_size);

    return true;
}

void engine_stop(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine->device_started) {
        audio_device_stop(&engine->device);
        engine->device_started = false;
    }
    if (engine->worker_thread) {
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    engine_graph_reset(engine->graph);
    engine->transport_frame = 0;
}

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    if (!engine) {
        return NULL;
    }
    return &engine->config;
}

bool engine_fx_get_registry(const Engine* engine, const FxRegistryEntry** out_entries, int* out_count) {
    if (!engine || !engine->fxm_mutex) {
        return false;
    }
    if (out_entries) {
        *out_entries = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    SDL_LockMutex(engine->fxm_mutex);
    const FxRegistryEntry* entries = NULL;
    if (engine->fxm) {
        entries = fxm_get_registry(engine->fxm, out_count);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!entries) {
        return false;
    }
    if (out_entries) {
        *out_entries = entries;
    }
    return true;
}

bool engine_fx_registry_get_desc(const Engine* engine, FxTypeId type, FxDesc* out_desc) {
    if (!engine || !out_desc || !engine->fxm_mutex) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_registry_get_desc(engine->fxm, type, out_desc);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot || !engine->fxm_mutex) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_snapshot(engine->fxm, out_snapshot);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

FxInstId engine_fx_master_add(Engine* engine, FxTypeId type) {
    if (!engine || !engine->fxm_mutex) {
        return 0;
    }
    FxInstId id = 0;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        id = fxm_master_add(engine->fxm, type);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return id;
}

bool engine_fx_master_remove(Engine* engine, FxInstId id) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_remove(engine->fxm, id);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_set_param(Engine* engine, FxInstId id, uint32_t param_index, float value) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_set_param(engine->fxm, id, param_index, value);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_set_param_with_mode(Engine* engine,
                                          FxInstId id,
                                          uint32_t param_index,
                                          float value,
                                          FxParamMode mode,
                                          float beat_value) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_set_param_with_mode(engine->fxm, id, param_index, value, mode, beat_value);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_set_enabled(Engine* engine, FxInstId id, bool enabled) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_set_enabled(engine->fxm, id, enabled);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

FxInstId engine_fx_track_add(Engine* engine, int track_index, FxTypeId type) {
    if (!engine || !engine->fxm_mutex) return 0;
    FxInstId id = 0;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        id = fxm_track_add(engine->fxm, track_index, type);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return id;
}

bool engine_fx_track_remove(Engine* engine, int track_index, FxInstId id) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_remove(engine->fxm, track_index, id);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_reorder(Engine* engine, int track_index, FxInstId id, int new_index) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_reorder(engine->fxm, track_index, id, new_index);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_set_param(Engine* engine, int track_index, FxInstId id, uint32_t param_index, float value) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_set_param(engine->fxm, track_index, id, param_index, value);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_set_param_with_mode(Engine* engine,
                                         int track_index,
                                         FxInstId id,
                                         uint32_t param_index,
                                         float value,
                                         FxParamMode mode,
                                         float beat_value) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_set_param_with_mode(engine->fxm, track_index, id, param_index, value, mode, beat_value);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_set_enabled(Engine* engine, int track_index, FxInstId id, bool enabled) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_set_enabled(engine->fxm, track_index, id, enabled);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_snapshot(const Engine* engine, int track_index, FxMasterSnapshot* out_snapshot) {
    if (!engine || !engine->fxm_mutex || !out_snapshot) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_snapshot(engine->fxm, track_index, out_snapshot);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_set_track_count(Engine* engine, int track_count) {
    if (!engine || !engine->fxm_mutex || track_count < 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_set_track_count(engine->fxm, track_count);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_bounce_range(Engine* engine,
                         uint64_t start_frame,
                         uint64_t end_frame,
                         const char* out_path,
                         void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                         void* user) {
    if (!engine || !out_path || end_frame <= start_frame) {
        return false;
    }
    int channels = engine_graph_get_channels(engine->graph);
    int sample_rate = engine->config.sample_rate;
    if (channels <= 0 || sample_rate <= 0) {
        return false;
    }

    uint64_t total_frames = end_frame - start_frame;
    if (total_frames == 0) {
        return false;
    }

    bool was_running = engine_is_running(engine);
    bool was_playing = engine_transport_is_playing(engine);
    uint64_t prev_transport = engine_get_transport_frame(engine);

    if (was_running) {
        engine_transport_stop(engine);
        engine_stop(engine);
    }

    engine_rebuild_sources(engine);
    engine_graph_reset(engine->graph);

    int block = engine->config.block_size > 0 ? engine->config.block_size : 512;
    int offline_block = block;
    float* buffer = (float*)calloc((size_t)total_frames * (size_t)channels, sizeof(float));
    float* track_buffer = (float*)calloc((size_t)offline_block * (size_t)channels, sizeof(float));
    if (!buffer || !track_buffer) {
        if (was_running) {
            engine_start(engine);
            engine_transport_seek(engine, prev_transport);
            if (was_playing) {
                engine_transport_play(engine);
            }
        }
        return false;
    }

    uint64_t rendered = 0;
    while (rendered < total_frames) {
        uint64_t remaining = total_frames - rendered;
        int chunk = (int)(remaining > (uint64_t)offline_block ? (uint64_t)offline_block : remaining);
        float* out = buffer + rendered * (uint64_t)channels;

        engine_mix_tracks(engine, start_frame + rendered, chunk, out, track_buffer, channels);

        rendered += (uint64_t)chunk;
        if (progress_cb) {
            progress_cb(rendered, total_frames, user);
        }
    }

    // Normalize to prevent clipping; also emit a float WAV for fidelity/debug.
    float peak = 0.0f;
    size_t total_samples = (size_t)total_frames * (size_t)channels;
    for (size_t i = 0; i < total_samples; ++i) {
        float a = fabsf(buffer[i]);
        if (a > peak) peak = a;
    }
    float norm = (peak > 1.0f) ? (1.0f / peak) : 1.0f;
    if (norm < 1.0f) {
        for (size_t i = 0; i < total_samples; ++i) {
            buffer[i] *= norm;
        }
    }

    // Write a float WAV alongside the PCM16 for comparison.
    char float_path[512];
    snprintf(float_path, sizeof(float_path), "%s.f32.wav", out_path);

    bool ok_float = wav_write_f32(float_path, buffer, total_frames, channels, sample_rate);
    uint32_t dither_seed = (uint32_t)(SDL_GetPerformanceCounter() & 0xffffffffu);
    bool ok = wav_write_pcm16_dithered(out_path, buffer, total_frames, channels, sample_rate, dither_seed);
    (void)ok_float;
    free(buffer);
    free(track_buffer);

    if (was_running) {
        if (engine_start(engine)) {
            engine_transport_seek(engine, prev_transport);
            if (was_playing) {
                engine_transport_play(engine);
            }
        } else {
            ok = false;
        }
    }

    return ok;
}

bool engine_is_running(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return engine->device_started;
}

size_t engine_get_queued_frames(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return audio_queue_available_frames(&engine->output_queue);
}

bool engine_transport_play(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_PLAY};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_play: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
    engine_trace(engine, "transport play");
    return true;
}

bool engine_transport_stop(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_STOP};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_stop: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    engine_trace(engine, "transport stop");
    return true;
}

bool engine_transport_is_playing(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
}

bool engine_transport_seek(Engine* engine, uint64_t frame) {
    if (!engine) {
        return false;
    }
    if (!engine->device_started || !engine->worker_thread) {
        engine->transport_frame = frame;
        audio_queue_clear(&engine->output_queue);
        engine_graph_reset(engine->graph);
        engine_trace(engine, "transport seek frame=%llu", (unsigned long long)frame);
        return true;
    }

    EngineCommand cmd = {
        .type = ENGINE_CMD_SEEK,
        .payload.seek.frame = frame,
    };
    if (!engine_post_command(engine, &cmd)) {
        return false;
    }
    engine_trace(engine, "transport seek queued frame=%llu", (unsigned long long)frame);
    return true;
}

bool engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame) {
    if (!engine) {
        return false;
    }
    if (enabled && end_frame <= start_frame) {
        enabled = false;
    }

    if (!engine->device_started || !engine->worker_thread) {
        atomic_store_explicit(&engine->loop_enabled, enabled, memory_order_release);
        atomic_store_explicit(&engine->loop_start_frame, start_frame, memory_order_release);
        atomic_store_explicit(&engine->loop_end_frame, end_frame, memory_order_release);
        engine_trace(engine, "transport loop %s start=%llu end=%llu",
                     enabled ? "enabled" : "disabled",
                     (unsigned long long)start_frame,
                     (unsigned long long)end_frame);
        return true;
    }

    EngineCommand cmd = {
        .type = ENGINE_CMD_SET_LOOP,
        .payload.loop = {
            .enabled = enabled,
            .start_frame = start_frame,
            .end_frame = end_frame,
        },
    };
    if (!engine_post_command(engine, &cmd)) {
        return false;
    }
    engine_trace(engine, "transport loop queued %s start=%llu end=%llu",
                 enabled ? "enabled" : "disabled",
                 (unsigned long long)start_frame,
                 (unsigned long long)end_frame);
    return true;
}

bool engine_queue_graph_swap(Engine* engine, EngineGraph* new_graph) {
    if (!engine || !new_graph) {
        return false;
    }
    EngineCommand cmd = {
        .type = ENGINE_CMD_GRAPH_SWAP,
        .payload.graph_swap.new_graph = new_graph,
    };
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_queue_graph_swap: command queue full");
        engine_graph_destroy(new_graph);
        return false;
    }
    return true;
}

bool engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame) {
    return engine_add_clip_to_track(engine, 0, filepath, start_frame, NULL);
}

int engine_add_track(Engine* engine) {
    if (!engine) {
        return -1;
    }
    int index = engine->track_count;
    if (!engine_get_track_mutable(engine, index)) {
        return -1;
    }
    engine->tracks[index].active = false;
    engine_track_set_name(engine, index, NULL);
    return index;
}

bool engine_remove_track(Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }

    EngineTrack* track = &engine->tracks[track_index];
    engine_track_clear(engine, track);

    if (track_index < engine->track_count - 1) {
        memmove(&engine->tracks[track_index],
                &engine->tracks[track_index + 1],
                (size_t)(engine->track_count - track_index - 1) * sizeof(EngineTrack));
    }

    engine->track_count--;
    if (engine->track_count < 0) {
        engine->track_count = 0;
    }
    if (engine->track_count >= 0 && engine->track_count < engine->track_capacity) {
        engine_track_init(&engine->tracks[engine->track_count]);
    }

    engine_rebuild_sources(engine);
    return true;
}

bool engine_track_set_name(Engine* engine, int track_index, const char* name) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    if (name && name[0] != '\0') {
        strncpy(track->name, name, sizeof(track->name) - 1);
        track->name[sizeof(track->name) - 1] = '\0';
    } else {
        snprintf(track->name, sizeof(track->name), "Track %d", track_index + 1);
    }
    return true;
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    if (!engine || !filepath) {
        return false;
    }

    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

    AudioMediaClip* cached_media = NULL;
    if (!audio_media_cache_acquire(&engine->media_cache, filepath, engine->config.sample_rate, &cached_media)) {
        SDL_Log("engine_add_clip_to_track: failed to load %s", filepath);
        return false;
    }

    if (!cached_media || cached_media->channels <= 0) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return false;
    }

    EngineClip* clip_slot = engine_track_append_clip(engine, track);
    if (!clip_slot) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return false;
    }

    clip_slot->sampler = engine_sampler_source_create();
    if (!clip_slot->sampler) {
        track->clip_count--;
        audio_media_cache_release(&engine->media_cache, cached_media);
        return false;
    }

    clip_slot->media = cached_media;
    clip_slot->timeline_start_frames = start_frame;
    clip_slot->offset_frames = 0;
    clip_slot->duration_frames = cached_media->frame_count;
    clip_slot->selected = false;
    engine_clip_set_name_from_path(clip_slot, filepath);
    strncpy(clip_slot->media_path, filepath, sizeof(clip_slot->media_path) - 1);
    clip_slot->media_path[sizeof(clip_slot->media_path) - 1] = '\0';
    clip_slot->gain = 1.0f;
    clip_slot->active = true;
    uint64_t default_fade_in = 0;
    uint64_t default_fade_out = 0;
    uint64_t clip_length = clip_slot->duration_frames > 0 ? clip_slot->duration_frames : cached_media->frame_count;
    engine_compute_default_fades(&engine->config, clip_length, &default_fade_in, &default_fade_out);
    clip_slot->fade_in_frames = default_fade_in;
    clip_slot->fade_out_frames = default_fade_out;
    engine_clip_refresh_sampler(clip_slot);

    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == clip_slot->sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_trace(engine, "clip add track=%d start=%llu path=%s",
                 track_index,
                 (unsigned long long)start_frame,
                 filepath ? filepath : "");

    engine_rebuild_sources(engine);
    return true;
}

const EngineTrack* engine_get_tracks(const Engine* engine) {
    if (!engine) {
        return NULL;
    }
    return engine->tracks;
}

int engine_get_track_count(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return engine->track_count;
}

uint64_t engine_get_transport_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return engine->transport_frame;
}

bool engine_load_wav(Engine* engine, const char* path) {
    if (!engine || !path) {
        return false;
    }
    return engine_add_clip(engine, path, 0);
}

bool engine_clip_set_timeline_start(Engine* engine, int track_index, int clip_index, uint64_t start_frame, int* out_clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip || !clip->sampler || !clip->media) {
        return false;
    }

    clip->timeline_start_frames = start_frame;
    engine_clip_refresh_sampler(clip);

    engine_trace(engine, "clip move track=%d clip=%d start=%llu",
                 track_index,
                 clip_index,
                 (unsigned long long)start_frame);

    EngineSamplerSource* sampler = clip->sampler;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = clip_index;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip || !clip->sampler || !clip->media) {
        return false;
    }

    uint64_t total_frames = clip->media ? clip->media->frame_count : 0;
    if (total_frames == 0) {
        return false;
    }

    if (offset_frames >= total_frames) {
        offset_frames = total_frames - 1;
    }
    uint64_t max_duration = total_frames - offset_frames;
    if (max_duration == 0) {
        max_duration = 1;
    }
    if (duration_frames == 0 || duration_frames > max_duration) {
        duration_frames = max_duration;
    }
    if (duration_frames == 0) {
        duration_frames = 1;
    }

    clip->offset_frames = offset_frames;
    clip->duration_frames = duration_frames;
    engine_clip_refresh_sampler(clip);

    engine_rebuild_sources(engine);
    return true;
}

uint64_t engine_clip_get_total_frames(const Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return 0;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return 0;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip || !clip->media) {
        return 0;
    }
    return clip->media->frame_count;
}

bool engine_remove_clip(Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }

    EngineClip* clip = &track->clips[clip_index];
    engine_clip_destroy(engine, clip);
    int remaining = track->clip_count - clip_index - 1;
    if (remaining > 0) {
        memmove(&track->clips[clip_index], &track->clips[clip_index + 1], (size_t)remaining * sizeof(EngineClip));
    }
    track->clip_count--;
    if (track->clip_count >= 0) {
        memset(&track->clips[track->clip_count], 0, sizeof(EngineClip));
    }
    if (track->clip_count > 0) {
        track->active = true;
    } else {
        track->active = false;
    }

    engine_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return false;
    }
    clip->name[0] = '\0';
    if (name) {
        strncpy(clip->name, name, sizeof(clip->name) - 1);
        clip->name[sizeof(clip->name) - 1] = '\0';
    }
    return true;
}

bool engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return false;
    }
    clip->gain = gain;
    engine_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return false;
    }
    uint64_t max_len = clip->duration_frames;
    if (max_len == 0) {
        max_len = engine_clip_get_total_frames(engine, track_index, clip_index);
    }
    if (fade_in_frames > max_len) {
        fade_in_frames = max_len;
    }
    if (fade_out_frames > max_len) {
        fade_out_frames = max_len;
    }
    if (fade_in_frames + fade_out_frames > max_len) {
        uint64_t total = fade_in_frames + fade_out_frames;
        uint64_t excess = total - max_len;
        if (fade_out_frames >= excess) {
            fade_out_frames -= excess;
        } else if (fade_in_frames >= excess) {
            fade_in_frames -= excess;
        } else {
            fade_out_frames = 0;
            fade_in_frames = 0;
        }
    }
    clip->fade_in_frames = fade_in_frames;
    clip->fade_out_frames = fade_out_frames;
    engine_clip_refresh_sampler(clip);
    engine_trace(engine, "clip fades track=%d clip=%d in=%llu out=%llu",
                 track_index,
                 clip_index,
                 (unsigned long long)fade_in_frames,
                 (unsigned long long)fade_out_frames);
    return true;
}

typedef enum {
    ENGINE_NO_OVERLAP_REMOVE = 1,
    ENGINE_NO_OVERLAP_TRIM_END,
    ENGINE_NO_OVERLAP_SHIFT_START,
} EngineNoOverlapAction;

typedef struct {
    EngineSamplerSource* sampler;
    EngineNoOverlapAction action;
} EngineNoOverlapOp;

typedef struct {
    char media_path[ENGINE_CLIP_PATH_MAX];
    char name[ENGINE_CLIP_NAME_MAX];
    float gain;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    uint64_t start_frames;
    uint64_t offset_frames;
    uint64_t duration_frames;
} EngineNoOverlapSpawn;

static void engine_clip_clamp_fades(Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return;
    }
    uint64_t effective = engine_clip_effective_duration(clip);
    uint64_t fade_in = clip->fade_in_frames > effective ? effective : clip->fade_in_frames;
    uint64_t fade_out = clip->fade_out_frames > effective ? effective : clip->fade_out_frames;
    engine_clip_set_fades(engine, track_index, clip_index, fade_in, fade_out);
}

bool engine_track_apply_no_overlap(Engine* engine,
                                   int track_index,
                                   EngineSamplerSource* anchor_sampler,
                                   int* out_anchor_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count || !anchor_sampler) {
        return false;
    }

    EngineTrack* track = &engine->tracks[track_index];
    if (!track || track->clip_count <= 0) {
        return false;
    }

    int anchor_index = engine_track_find_clip_by_sampler(track, anchor_sampler);
    if (anchor_index < 0) {
        return false;
    }

    EngineClip* anchor_clip = &track->clips[anchor_index];
    uint64_t anchor_duration = engine_clip_effective_duration(anchor_clip);
    if (anchor_duration == 0) {
        if (out_anchor_index) {
            *out_anchor_index = anchor_index;
        }
        return true;
    }

    uint64_t anchor_start = anchor_clip->timeline_start_frames;
    uint64_t anchor_end = anchor_start + anchor_duration;

    if (track->clip_count <= 1) {
        if (out_anchor_index) {
            *out_anchor_index = anchor_index;
        }
        return true;
    }

    EngineNoOverlapOp* ops = NULL;
    EngineNoOverlapSpawn* spawns = NULL;
    if (track->clip_count > 0) {
        ops = (EngineNoOverlapOp*)SDL_calloc((size_t)track->clip_count, sizeof(EngineNoOverlapOp));
        if (!ops) {
            return false;
        }
        spawns = (EngineNoOverlapSpawn*)SDL_calloc((size_t)track->clip_count, sizeof(EngineNoOverlapSpawn));
        if (!spawns) {
            SDL_free(ops);
            return false;
        }
    }
    int op_count = 0;
    int spawn_count = 0;

    for (int i = 0; i < track->clip_count; ++i) {
        EngineClip* clip = &track->clips[i];
        if (!clip || !clip->sampler || clip->sampler == anchor_sampler) {
            continue;
        }
        uint64_t clip_duration = engine_clip_effective_duration(clip);
        if (clip_duration == 0) {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
            op_count++;
            continue;
        }
        uint64_t clip_start = clip->timeline_start_frames;
        uint64_t clip_end = clip_start + clip_duration;

        bool clip_newer = clip->creation_index > anchor_clip->creation_index;
        if (clip_newer) {
            continue;
        }

        if (clip_end <= anchor_start || clip_start >= anchor_end) {
            continue;
        }

        bool overlaps_left = clip_start < anchor_start;
        bool overlaps_right = clip_end > anchor_end;

        if (overlaps_left && overlaps_right) {
            uint64_t total_frames = engine_clip_get_total_frames(engine, track_index, i);
            uint64_t left_duration = anchor_start - clip_start;
            uint64_t right_duration = clip_end - anchor_end;

            if (left_duration == 0) {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
                op_count++;
            } else {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_TRIM_END;
                op_count++;
            }

            if (right_duration > 0 && spawn_count < track->clip_count) {
                EngineNoOverlapSpawn* spawn = &spawns[spawn_count++];
                memset(spawn, 0, sizeof(*spawn));
                strncpy(spawn->media_path, clip->media_path, sizeof(spawn->media_path) - 1);
                strncpy(spawn->name, clip->name, sizeof(spawn->name) - 1);
                spawn->gain = clip->gain;
                spawn->fade_in_frames = clip->fade_in_frames;
                spawn->fade_out_frames = clip->fade_out_frames;
                spawn->start_frames = anchor_end;
                uint64_t offset = clip->offset_frames + (anchor_end - clip_start);
                if (total_frames > 0 && offset >= total_frames) {
                    offset = total_frames - 1;
                }
                spawn->offset_frames = offset;
                spawn->duration_frames = right_duration;
                if (total_frames > 0 && spawn->offset_frames + spawn->duration_frames > total_frames) {
                    spawn->duration_frames = total_frames - spawn->offset_frames;
                }
            }
            continue;
        }

        if (overlaps_left) {
            uint64_t new_duration = anchor_start > clip_start ? anchor_start - clip_start : 0;
            if (new_duration == 0) {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
                op_count++;
            } else {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_TRIM_END;
                op_count++;
            }
            continue;
        }

        if (!overlaps_right) {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
            op_count++;
            continue;
        }

        ops[op_count].sampler = clip->sampler;
        ops[op_count].action = ENGINE_NO_OVERLAP_SHIFT_START;
        op_count++;
    }

    bool changed = false;
    for (int i = 0; i < op_count; ++i) {
        EngineTrack* current_track = &engine->tracks[track_index];
        int clip_index = engine_track_find_clip_by_sampler(current_track, ops[i].sampler);
        if (clip_index < 0) {
            continue;
        }

        EngineClip* clip = &current_track->clips[clip_index];
        switch (ops[i].action) {
            case ENGINE_NO_OVERLAP_REMOVE: {
                if (engine_remove_clip(engine, track_index, clip_index)) {
                    changed = true;
                }
                break;
            }
            case ENGINE_NO_OVERLAP_TRIM_END: {
                uint64_t clip_start = clip->timeline_start_frames;
                if (clip_start >= anchor_start) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                    break;
                }
                uint64_t new_duration = anchor_start - clip_start;
                if (!engine_clip_set_region(engine, track_index, clip_index, clip->offset_frames, new_duration)) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                } else {
                    engine_clip_clamp_fades(engine, track_index, clip_index);
                    changed = true;
                }
                break;
            }
            case ENGINE_NO_OVERLAP_SHIFT_START: {
                uint64_t clip_start = clip->timeline_start_frames;
                uint64_t clip_duration = engine_clip_effective_duration(clip);
                if (clip_start >= anchor_end || clip_duration == 0) {
                    break;
                }
                uint64_t delta = anchor_end > clip_start ? anchor_end - clip_start : 0;
                if (delta >= clip_duration) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                    break;
                }
                uint64_t new_offset = clip->offset_frames + delta;
                uint64_t total_frames = engine_clip_get_total_frames(engine, track_index, clip_index);
                if (total_frames == 0 || new_offset >= total_frames) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                    break;
                }
                uint64_t max_duration = total_frames - new_offset;
                if (max_duration == 0) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                    break;
                }
                uint64_t new_duration = clip_duration - delta;
                if (new_duration == 0 || new_duration > max_duration) {
                    new_duration = max_duration;
                }
                if (!engine_clip_set_region(engine, track_index, clip_index, new_offset, new_duration)) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                    break;
                }
                int updated_index = clip_index;
                if (!engine_clip_set_timeline_start(engine, track_index, clip_index, anchor_end, &updated_index)) {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                } else {
                    engine_clip_clamp_fades(engine, track_index, updated_index);
                    changed = true;
                }
                break;
            }
        }
    }

    if (ops) {
        SDL_free(ops);
    }
    if (spawns && spawn_count > 0) {
        for (int i = 0; i < spawn_count; ++i) {
            EngineNoOverlapSpawn* spawn = &spawns[i];
            if (spawn->media_path[0] == '\0' || spawn->duration_frames == 0) {
                continue;
            }
            int new_index = -1;
            if (!engine_add_clip_to_track(engine, track_index, spawn->media_path, spawn->start_frames, &new_index)) {
                continue;
            }
            const EngineTrack* refreshed_tracks = engine_get_tracks(engine);
            int refreshed_count = engine_get_track_count(engine);
            if (!refreshed_tracks || track_index < 0 || track_index >= refreshed_count) {
                continue;
            }
            const EngineTrack* refreshed_track = &refreshed_tracks[track_index];
            if (!refreshed_track || new_index < 0 || new_index >= refreshed_track->clip_count) {
                continue;
            }
            engine_clip_set_region(engine, track_index, new_index, spawn->offset_frames, spawn->duration_frames);
            engine_clip_set_gain(engine, track_index, new_index, spawn->gain);
            uint64_t clamped_fade_in = spawn->fade_in_frames > spawn->duration_frames ? spawn->duration_frames : spawn->fade_in_frames;
            uint64_t clamped_fade_out = spawn->fade_out_frames > spawn->duration_frames ? spawn->duration_frames : spawn->fade_out_frames;
            engine_clip_set_fades(engine, track_index, new_index, clamped_fade_in, clamped_fade_out);
            if (spawn->name[0] != '\0') {
                engine_clip_set_name(engine, track_index, new_index, spawn->name);
            }
        }
        changed = true;
    }
    if (spawns) {
        SDL_free(spawns);
    }

    EngineTrack* final_track = &engine->tracks[track_index];
    int final_anchor_index = engine_track_find_clip_by_sampler(final_track, anchor_sampler);
    if (final_anchor_index < 0) {
        return false;
    }
    if (out_anchor_index) {
        *out_anchor_index = final_anchor_index;
    }
    return changed || op_count == 0;
}

bool engine_add_clip_segment(Engine* engine, int track_index, const EngineClip* source_clip,
                             uint64_t source_relative_offset_frames,
                             uint64_t segment_length_frames,
                             uint64_t start_frame,
                             int* out_clip_index) {
    if (!engine || !source_clip || !source_clip->media || segment_length_frames == 0) {
        return false;
    }
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

    const AudioMediaClip* media_src = source_clip->media;
    if (!media_src || media_src->frame_count == 0) {
        return false;
    }

    if (source_relative_offset_frames >= media_src->frame_count) {
        return false;
    }

    uint64_t max_length = media_src->frame_count - source_relative_offset_frames;
    if (segment_length_frames > max_length) {
        segment_length_frames = max_length;
    }
    if (segment_length_frames == 0) {
        return false;
    }

    AudioMediaClip* media_copy = (AudioMediaClip*)malloc(sizeof(AudioMediaClip));
    if (!media_copy) {
        return false;
    }

    size_t total_samples = (size_t)media_src->frame_count * (size_t)media_src->channels;
    float* sample_copy = (float*)malloc(total_samples * sizeof(float));
    if (!sample_copy) {
        free(media_copy);
        return false;
    }
    memcpy(sample_copy, media_src->samples, total_samples * sizeof(float));

    media_copy->samples = sample_copy;
    media_copy->frame_count = media_src->frame_count;
    media_copy->channels = media_src->channels;
    media_copy->sample_rate = media_src->sample_rate;

    EngineClip* new_clip = engine_track_append_clip(engine, track);
    if (!new_clip) {
        audio_media_clip_free(media_copy);
        free(media_copy);
        return false;
    }

    new_clip->sampler = engine_sampler_source_create();
    if (!new_clip->sampler) {
        track->clip_count--;
        audio_media_clip_free(media_copy);
        free(media_copy);
        return false;
    }

    new_clip->media = media_copy;
    new_clip->timeline_start_frames = start_frame;
    new_clip->offset_frames = source_clip->offset_frames + source_relative_offset_frames;
    new_clip->duration_frames = segment_length_frames;
    new_clip->gain = source_clip->gain;
    new_clip->active = source_clip->active;
    new_clip->selected = false;
    new_clip->fade_in_frames = source_clip->fade_in_frames;
    new_clip->fade_out_frames = source_clip->fade_out_frames;

    if (source_clip->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s segment", source_clip->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip segment");
    }
    strncpy(new_clip->media_path, source_clip->media_path, sizeof(new_clip->media_path) - 1);
    new_clip->media_path[sizeof(new_clip->media_path) - 1] = '\0';

    engine_clip_refresh_sampler(new_clip);
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == new_clip->sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_rebuild_sources(engine);
    return true;
}

bool engine_track_set_muted(Engine* engine, int track_index, bool muted) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->muted = muted;
    engine_rebuild_sources(engine);
    return true;
}

bool engine_track_set_solo(Engine* engine, int track_index, bool solo) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->solo = solo;
    engine_rebuild_sources(engine);
    return true;
}

bool engine_track_set_gain(Engine* engine, int track_index, float gain) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->gain = gain;
    engine_rebuild_sources(engine);
    return true;
}

void engine_set_logging(Engine* engine, bool engine_logs, bool cache_logs, bool timing_logs) {
    if (!engine) {
        return;
    }
    engine->config.enable_engine_logs = engine_logs;
    engine->config.enable_cache_logs = cache_logs;
    engine->config.enable_timing_logs = timing_logs;
    audio_media_cache_set_verbose(&engine->media_cache, cache_logs);
    SDL_Log("engine logging flags: engine=%s cache=%s timing=%s",
            engine_logs ? "on" : "off",
            cache_logs ? "on" : "off",
            timing_logs ? "on" : "off");
}

bool engine_duplicate_clip(Engine* engine, int track_index, int clip_index, uint64_t start_frame_offset, int* out_clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* original = &track->clips[clip_index];
    if (!original || !original->media) {
        return false;
    }

    AudioMediaClip* media = (AudioMediaClip*)malloc(sizeof(AudioMediaClip));
    if (!media) {
        return false;
    }
    size_t samples = (size_t)original->media->frame_count * (size_t)original->media->channels;
    float* buffer = (float*)malloc(samples * sizeof(float));
    if (!buffer) {
        free(media);
        return false;
    }
    memcpy(buffer, original->media->samples, samples * sizeof(float));
    media->samples = buffer;
    media->frame_count = original->media->frame_count;
    media->channels = original->media->channels;
    media->sample_rate = original->media->sample_rate;

    EngineClip* new_clip = engine_track_append_clip(engine, track);
    if (!new_clip) {
        audio_media_clip_free(media);
        free(media);
        return false;
    }

    new_clip->sampler = engine_sampler_source_create();
    if (!new_clip->sampler) {
        track->clip_count--;
        audio_media_clip_free(media);
        free(media);
        return false;
    }

    new_clip->media = media;
    uint64_t offset = start_frame_offset;
    uint64_t new_start = original->timeline_start_frames + original->duration_frames + offset;
    new_clip->timeline_start_frames = new_start;
    new_clip->offset_frames = original->offset_frames;
    new_clip->duration_frames = original->duration_frames;
    new_clip->gain = original->gain;
    new_clip->active = original->active;
    new_clip->selected = false;
    new_clip->fade_in_frames = original->fade_in_frames;
    new_clip->fade_out_frames = original->fade_out_frames;

    if (original->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s copy", original->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip copy");
    }
    strncpy(new_clip->media_path, original->media_path, sizeof(new_clip->media_path) - 1);
    new_clip->media_path[sizeof(new_clip->media_path) - 1] = '\0';

    engine_clip_refresh_sampler(new_clip);
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == new_clip->sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_rebuild_sources(engine);
    return true;
}
