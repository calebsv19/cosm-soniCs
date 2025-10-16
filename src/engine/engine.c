#include "engine.h"

#include "audio/media_clip.h"
#include "audio_device.h"
#include "audio_queue.h"
#include "engine/graph.h"
#include "engine/sampler.h"
#include "engine/sources.h"
#include "ringbuf.h"

#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    ENGINE_CMD_PLAY = 1,
    ENGINE_CMD_STOP = 2,
    ENGINE_CMD_GRAPH_SWAP = 3,
} EngineCommandType;

typedef struct {
    EngineCommandType type;
    union {
        struct {
            EngineGraph* new_graph;
        } graph_swap;
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
    EngineGraph* graph;
    EngineToneSource* tone_source;
    EngineGraphSourceOps tone_ops;
    EngineGraphSourceOps sampler_ops;
    EngineTrack* tracks;
    int track_count;
    int track_capacity;
    uint64_t transport_frame;
};

static void engine_clip_destroy(EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->sampler) {
        engine_sampler_source_destroy(clip->sampler);
        clip->sampler = NULL;
    }
    if (clip->media) {
        audio_media_clip_free(clip->media);
        free(clip->media);
        clip->media = NULL;
    }
    clip->gain = 0.0f;
    clip->active = false;
    clip->name[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
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
    track->active = true;
}

static void engine_track_clear(EngineTrack* track) {
    if (!track) {
        return;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        engine_clip_destroy(&track->clips[i]);
    }
    free(track->clips);
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->active = true;
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

static EngineClip* engine_track_append_clip(EngineTrack* track) {
    if (!track) {
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
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
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
    return 0;
}

static void engine_track_sort_clips(EngineTrack* track) {
    if (!track || track->clip_count <= 1 || !track->clips) {
        return;
    }
    qsort(track->clips, (size_t)track->clip_count, sizeof(EngineClip), engine_clip_compare_timeline);
}

static void engine_clip_refresh_sampler(EngineClip* clip) {
    if (!clip || !clip->sampler || !clip->media) {
        return;
    }
    engine_sampler_source_set_clip(clip->sampler, clip->media,
                                   clip->timeline_start_frames,
                                   clip->offset_frames,
                                   clip->duration_frames);
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
    engine_graph_clear_sources(engine->graph);
    bool added = false;
    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (!track->active || track->clip_count == 0) {
            continue;
        }
        float track_gain = track->gain != 0.0f ? track->gain : 1.0f;
        for (int c = 0; c < track->clip_count; ++c) {
            EngineClip* clip = &track->clips[c];
            if (!clip->active || !clip->sampler) {
                continue;
            }
            float clip_gain = clip->gain != 0.0f ? clip->gain : 1.0f;
            if (engine_graph_add_source(engine->graph, &engine->sampler_ops, clip->sampler, track_gain * clip_gain)) {
                added = true;
            }
        }
    }
    if (!added) {
        engine_graph_add_source(engine->graph, &engine->tone_ops, engine->tone_source, 1.0f);
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
                engine->transport_frame = 0;
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
            default:
                break;
        }
    }
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
    if (!block_buffer) {
        return -1;
    }

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

        engine_graph_render(engine->graph, block_buffer, block, engine->transport_frame);

        audio_queue_write(&engine->output_queue, block_buffer, (size_t)block);
        engine->transport_frame += (uint64_t)block;
    }

    free(block_buffer);
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
    if (!ringbuf_init(&engine->command_queue, sizeof(EngineCommand) * 64)) {
        free(engine);
        return NULL;
    }
    engine->graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
    if (!engine->graph) {
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine_tone_source_ops(&engine->tone_ops);
    engine_sampler_source_ops(&engine->sampler_ops);
    engine->tone_source = engine_tone_source_create();
    if (!engine->tone_source) {
        engine_graph_destroy(engine->graph);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }

    engine->track_capacity = 4;
    engine->tracks = (EngineTrack*)calloc((size_t)engine->track_capacity, sizeof(EngineTrack));
    if (!engine->tracks) {
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_init(&engine->tracks[i]);
    }
    engine->track_count = 0;
    engine->transport_frame = 0;

    engine_graph_clear_sources(engine->graph);
    engine_graph_add_source(engine->graph, &engine->tone_ops, engine->tone_source, 1.0f);
    engine_graph_reset(engine->graph);

    engine_add_track(engine);
    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_stop(engine);
    audio_device_close(&engine->device);
    if (engine->graph) {
        engine_graph_destroy(engine->graph);
        engine->graph = NULL;
    }
    if (engine->tone_source) {
        engine_tone_source_destroy(engine->tone_source);
        engine->tone_source = NULL;
    }
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_clear(&engine->tracks[i]);
    }
    free(engine->tracks);
    engine->tracks = NULL;
    engine->track_count = 0;
    engine->track_capacity = 0;
    ringbuf_free(&engine->command_queue);
    audio_queue_free(&engine->output_queue);
    free(engine);
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
    if (!engine->device.is_open) {
        if (!audio_device_open(&engine->device, &want, engine_audio_callback, engine)) {
            SDL_Log("engine_start: failed to open audio device");
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

    engine_transport_play(engine);
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
    return true;
}

bool engine_transport_is_playing(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
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
    return index;
}

bool engine_remove_track(Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }

    EngineTrack* track = &engine->tracks[track_index];
    engine_track_clear(track);

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

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    if (!engine || !filepath) {
        return false;
    }

    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

    AudioMediaClip loaded = {0};
    if (!audio_media_clip_load_wav(filepath, engine->config.sample_rate, &loaded)) {
        SDL_Log("engine_add_clip_to_track: failed to load %s", filepath);
        return false;
    }

    if (loaded.channels <= 0) {
        audio_media_clip_free(&loaded);
        return false;
    }

    EngineClip* clip_slot = engine_track_append_clip(track);
    if (!clip_slot) {
        audio_media_clip_free(&loaded);
        return false;
    }

    clip_slot->sampler = engine_sampler_source_create();
    if (!clip_slot->sampler) {
        track->clip_count--;
        audio_media_clip_free(&loaded);
        return false;
    }

    AudioMediaClip* media = (AudioMediaClip*)malloc(sizeof(AudioMediaClip));
    if (!media) {
        engine_sampler_source_destroy(clip_slot->sampler);
        clip_slot->sampler = NULL;
        track->clip_count--;
        audio_media_clip_free(&loaded);
        return false;
    }

    *media = loaded;
    clip_slot->media = media;
    clip_slot->timeline_start_frames = start_frame;
    clip_slot->offset_frames = 0;
    clip_slot->duration_frames = media->frame_count;
    clip_slot->selected = false;
    engine_clip_set_name_from_path(clip_slot, filepath);
    clip_slot->gain = 1.0f;
    clip_slot->active = true;
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
    engine_clip_destroy(clip);
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

    EngineClip* new_clip = engine_track_append_clip(track);
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

    if (source_clip->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s segment", source_clip->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip segment");
    }

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

    EngineClip* new_clip = engine_track_append_clip(track);
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

    if (original->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s copy", original->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip copy");
    }

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
