#include "engine/engine_internal.h"

#include "engine/sampler.h"

#include "audio/media_clip.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void engine_clip_destroy(Engine* engine, EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->automation_lanes) {
        for (int i = 0; i < clip->automation_lane_count; ++i) {
            engine_automation_lane_free(&clip->automation_lanes[i]);
        }
        free(clip->automation_lanes);
        clip->automation_lanes = NULL;
    }
    clip->automation_lane_count = 0;
    clip->automation_lane_capacity = 0;
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
    clip->source = NULL;
    clip->gain = 0.0f;
    clip->active = false;
    clip->name[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->creation_index = 0;
    clip->selected = false;
    clip->media = NULL;
    clip->source = NULL;
}

// Initializes a clip automation lane list with a default volume lane.
static void engine_clip_init_automation(EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->automation_lanes) {
        for (int i = 0; i < clip->automation_lane_count; ++i) {
            engine_automation_lane_free(&clip->automation_lanes[i]);
        }
        free(clip->automation_lanes);
    }
    clip->automation_lanes = (EngineAutomationLane*)calloc(1, sizeof(EngineAutomationLane));
    if (!clip->automation_lanes) {
        clip->automation_lane_count = 0;
        clip->automation_lane_capacity = 0;
        return;
    }
    engine_automation_lane_init(&clip->automation_lanes[0], ENGINE_AUTOMATION_TARGET_VOLUME);
    clip->automation_lane_count = 1;
    clip->automation_lane_capacity = 1;
}

// Copies automation lanes from one clip to another clip.
static bool engine_clip_copy_automation(const EngineClip* src, EngineClip* dst) {
    if (!src || !dst) {
        return false;
    }
    if (dst->automation_lanes) {
        for (int i = 0; i < dst->automation_lane_count; ++i) {
            engine_automation_lane_free(&dst->automation_lanes[i]);
        }
        free(dst->automation_lanes);
        dst->automation_lanes = NULL;
    }
    dst->automation_lane_count = 0;
    dst->automation_lane_capacity = 0;
    if (!src->automation_lanes || src->automation_lane_count <= 0) {
        return true;
    }
    dst->automation_lanes = (EngineAutomationLane*)calloc((size_t)src->automation_lane_count,
                                                          sizeof(EngineAutomationLane));
    if (!dst->automation_lanes) {
        return false;
    }
    dst->automation_lane_capacity = src->automation_lane_count;
    dst->automation_lane_count = src->automation_lane_count;
    for (int i = 0; i < src->automation_lane_count; ++i) {
        engine_automation_lane_init(&dst->automation_lanes[i], src->automation_lanes[i].target);
        engine_automation_lane_copy(&src->automation_lanes[i], &dst->automation_lanes[i]);
    }
    return true;
}

// Copies automation lanes from a raw array into the clip.
static bool engine_clip_set_automation_lanes_internal(EngineClip* clip,
                                                      const EngineAutomationLane* lanes,
                                                      int lane_count) {
    if (!clip) {
        return false;
    }
    if (clip->automation_lanes) {
        for (int i = 0; i < clip->automation_lane_count; ++i) {
            engine_automation_lane_free(&clip->automation_lanes[i]);
        }
        free(clip->automation_lanes);
        clip->automation_lanes = NULL;
    }
    clip->automation_lane_count = 0;
    clip->automation_lane_capacity = 0;
    if (!lanes || lane_count <= 0) {
        return true;
    }
    clip->automation_lanes = (EngineAutomationLane*)calloc((size_t)lane_count, sizeof(EngineAutomationLane));
    if (!clip->automation_lanes) {
        return false;
    }
    clip->automation_lane_capacity = lane_count;
    clip->automation_lane_count = lane_count;
    for (int i = 0; i < lane_count; ++i) {
        engine_automation_lane_init(&clip->automation_lanes[i], lanes[i].target);
        engine_automation_lane_copy(&lanes[i], &clip->automation_lanes[i]);
    }
    return true;
}

// Copies automation lanes into a standalone array for later spawning.
static bool engine_clip_snapshot_automation(const EngineClip* clip,
                                            EngineAutomationLane** out_lanes,
                                            int* out_lane_count) {
    if (!out_lanes || !out_lane_count) {
        return false;
    }
    *out_lanes = NULL;
    *out_lane_count = 0;
    if (!clip || !clip->automation_lanes || clip->automation_lane_count <= 0) {
        return true;
    }
    EngineAutomationLane* lanes = (EngineAutomationLane*)calloc((size_t)clip->automation_lane_count,
                                                                sizeof(EngineAutomationLane));
    if (!lanes) {
        return false;
    }
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        engine_automation_lane_init(&lanes[i], clip->automation_lanes[i].target);
        engine_automation_lane_copy(&clip->automation_lanes[i], &lanes[i]);
    }
    *out_lanes = lanes;
    *out_lane_count = clip->automation_lane_count;
    return true;
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
    clip->source = NULL;
    clip->gain = 1.0f;
    clip->active = true;
    clip->name[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->automation_lanes = NULL;
    clip->automation_lane_count = 0;
    clip->automation_lane_capacity = 0;
    clip->creation_index = engine->next_clip_id++;
    clip->selected = false;
    engine_clip_init_automation(clip);
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

static bool engine_clip_resolve_media(Engine* engine, EngineClip* clip) {
    if (!engine || !clip) {
        return false;
    }
    if (clip->media) {
        return true;
    }
    if (!clip->source || clip->source->path[0] == '\0') {
        return false;
    }
    AudioMediaClip* cached_media = NULL;
    if (!audio_media_cache_acquire(&engine->media_cache,
                                   clip->source->media_id,
                                   clip->source->path,
                                   engine->config.sample_rate,
                                   &cached_media)) {
        SDL_Log("engine_clip_resolve_media: failed to load %s", clip->source->path);
        return false;
    }
    if (!cached_media || cached_media->channels <= 0) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return false;
    }
    clip->media = cached_media;
    if (clip->source) {
        clip->source->clip = cached_media;
        clip->source->sample_rate = cached_media->sample_rate;
        clip->source->channels = cached_media->channels;
        clip->source->frame_count = cached_media->frame_count;
    }
    return true;
}

static void engine_clip_refresh_sampler(Engine* engine, EngineClip* clip) {
    if (!clip || !clip->sampler) {
        return;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        engine_sampler_source_set_clip(clip->sampler, NULL, 0, 0, 0, 0, 0);
        return;
    }
    engine_sampler_source_set_clip(clip->sampler, clip->media,
                                   clip->timeline_start_frames,
                                   clip->offset_frames,
                                   clip->duration_frames,
                                   clip->fade_in_frames,
                                   clip->fade_out_frames);
    engine_sampler_source_set_automation(clip->sampler,
                                         clip->automation_lanes,
                                         clip->automation_lane_count);
}

static EngineClip* engine_clip_create_with_source(Engine* engine,
                                                  EngineTrack* track,
                                                  EngineAudioSource* source,
                                                  const char* filepath,
                                                  const char* media_id,
                                                  uint64_t start_frame,
                                                  uint64_t offset_frames,
                                                  uint64_t duration_frames,
                                                  float gain,
                                                  uint64_t fade_in_frames,
                                                  uint64_t fade_out_frames,
                                                  bool use_default_fades) {
    if (!engine || !track || !filepath) {
        return NULL;
    }

    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, filepath);
    }

    const char* cache_id = source ? source->media_id : media_id;
    const char* cache_path = (source && source->path[0] != '\0') ? source->path : filepath;
    AudioMediaClip* cached_media = NULL;
    if (!audio_media_cache_acquire(&engine->media_cache,
                                   cache_id,
                                   cache_path,
                                   engine->config.sample_rate,
                                   &cached_media)) {
        SDL_Log("engine_clip_create_with_source: failed to load %s", cache_path);
        return NULL;
    }

    if (!cached_media || cached_media->channels <= 0) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    EngineClip* clip_slot = engine_track_append_clip(engine, track);
    if (!clip_slot) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    clip_slot->sampler = engine_sampler_source_create();
    if (!clip_slot->sampler) {
        track->clip_count--;
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    clip_slot->media = cached_media;
    clip_slot->source = source;
    clip_slot->timeline_start_frames = start_frame;
    clip_slot->offset_frames = offset_frames;
    clip_slot->duration_frames = duration_frames > 0 ? duration_frames : cached_media->frame_count;
    clip_slot->selected = false;
    engine_clip_set_name_from_path(clip_slot, filepath);
    clip_slot->gain = gain;
    clip_slot->active = true;
    if (use_default_fades) {
        uint64_t default_fade_in = 0;
        uint64_t default_fade_out = 0;
        uint64_t clip_length = clip_slot->duration_frames > 0 ? clip_slot->duration_frames : cached_media->frame_count;
        engine_compute_default_fades(&engine->config, clip_length, &default_fade_in, &default_fade_out);
        clip_slot->fade_in_frames = default_fade_in;
        clip_slot->fade_out_frames = default_fade_out;
    } else {
        clip_slot->fade_in_frames = fade_in_frames;
        clip_slot->fade_out_frames = fade_out_frames;
    }
    clip_slot->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip_slot->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;

    engine_clip_refresh_sampler(engine, clip_slot);
    return clip_slot;
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

// Captures clip fields needed to spawn a trimmed overlap replacement.
typedef struct {
    char media_id[ENGINE_MEDIA_ID_MAX];
    char media_path[ENGINE_CLIP_PATH_MAX];
    char name[ENGINE_CLIP_NAME_MAX];
    float gain;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    EngineAutomationLane* automation_lanes;
    int automation_lane_count;
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

// Resolves a mutable clip pointer for the given track/clip indices.
static EngineClip* engine_get_clip_mutable(Engine* engine, int track_index, int clip_index) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[clip_index];
}

// Ensures a clip has capacity for additional automation lanes.
static bool engine_clip_ensure_automation_capacity(EngineClip* clip, int needed) {
    if (!clip) {
        return false;
    }
    if (clip->automation_lane_capacity >= needed) {
        return true;
    }
    int new_cap = clip->automation_lane_capacity == 0 ? 2 : clip->automation_lane_capacity * 2;
    if (new_cap < needed) {
        new_cap = needed;
    }
    EngineAutomationLane* lanes = (EngineAutomationLane*)realloc(clip->automation_lanes,
                                                                 sizeof(EngineAutomationLane) * (size_t)new_cap);
    if (!lanes) {
        return false;
    }
    clip->automation_lanes = lanes;
    clip->automation_lane_capacity = new_cap;
    return true;
}

bool engine_clip_get_automation_lane(const Engine* engine,
                                     int track_index,
                                     int clip_index,
                                     EngineAutomationTarget target,
                                     const EngineAutomationLane** out_lane) {
    if (out_lane) {
        *out_lane = NULL;
    }
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return false;
    }
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        if (clip->automation_lanes[i].target == target) {
            if (out_lane) {
                *out_lane = &clip->automation_lanes[i];
            }
            return true;
        }
    }
    return false;
}

bool engine_clip_ensure_automation_lane(Engine* engine,
                                        int track_index,
                                        int clip_index,
                                        EngineAutomationTarget target,
                                        EngineAutomationLane** out_lane) {
    if (out_lane) {
        *out_lane = NULL;
    }
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
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        if (clip->automation_lanes[i].target == target) {
            if (out_lane) {
                *out_lane = &clip->automation_lanes[i];
            }
            return true;
        }
    }
    if (!engine_clip_ensure_automation_capacity(clip, clip->automation_lane_count + 1)) {
        return false;
    }
    EngineAutomationLane* lane = &clip->automation_lanes[clip->automation_lane_count++];
    engine_automation_lane_init(lane, target);
    if (out_lane) {
        *out_lane = lane;
    }
    engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    return true;
}

bool engine_clip_add_automation_point(Engine* engine,
                                      int track_index,
                                      int clip_index,
                                      EngineAutomationTarget target,
                                      uint64_t frame,
                                      float value,
                                      int* out_index) {
    if (!engine) {
        return false;
    }
    EngineAutomationLane* lane = NULL;
    if (!engine_clip_ensure_automation_lane(engine, track_index, clip_index, target, &lane) || !lane) {
        return false;
    }
    if (!engine_automation_lane_insert_point(lane, frame, value, out_index)) {
        return false;
    }
    EngineClip* clip = engine_get_clip_mutable(engine, track_index, clip_index);
    if (clip && clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    return true;
}

bool engine_clip_update_automation_point(Engine* engine,
                                         int track_index,
                                         int clip_index,
                                         EngineAutomationTarget target,
                                         int point_index,
                                         uint64_t frame,
                                         float value,
                                         int* out_index) {
    if (!engine) {
        return false;
    }
    EngineAutomationLane* lane = NULL;
    if (!engine_clip_ensure_automation_lane(engine, track_index, clip_index, target, &lane) || !lane) {
        return false;
    }
    if (!engine_automation_lane_update_point(lane, point_index, frame, value, out_index)) {
        return false;
    }
    EngineClip* clip = engine_get_clip_mutable(engine, track_index, clip_index);
    if (clip && clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    return true;
}

bool engine_clip_remove_automation_point(Engine* engine,
                                         int track_index,
                                         int clip_index,
                                         EngineAutomationTarget target,
                                         int point_index) {
    if (!engine) {
        return false;
    }
    EngineAutomationLane* lane = NULL;
    if (!engine_clip_ensure_automation_lane(engine, track_index, clip_index, target, &lane) || !lane) {
        return false;
    }
    if (!engine_automation_lane_remove_point(lane, point_index)) {
        return false;
    }
    EngineClip* clip = engine_get_clip_mutable(engine, track_index, clip_index);
    if (clip && clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    return true;
}

bool engine_clip_set_automation_lane_points(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineAutomationTarget target,
                                            const EngineAutomationPoint* points,
                                            int count) {
    if (!engine) {
        return false;
    }
    EngineAutomationLane* lane = NULL;
    if (!engine_clip_ensure_automation_lane(engine, track_index, clip_index, target, &lane) || !lane) {
        return false;
    }
    if (!engine_automation_lane_set_points(lane, points, count)) {
        return false;
    }
    EngineClip* clip = engine_get_clip_mutable(engine, track_index, clip_index);
    if (clip && clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    return true;
}

bool engine_clip_set_automation_lanes(Engine* engine,
                                      int track_index,
                                      int clip_index,
                                      const EngineAutomationLane* lanes,
                                      int lane_count) {
    if (!engine) {
        return false;
    }
    EngineClip* clip = engine_get_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (!engine_clip_set_automation_lanes_internal(clip, lanes, lane_count)) {
        return false;
    }
    if (clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    return true;
}

bool engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame) {
    return engine_add_clip_to_track_with_id(engine, 0, filepath, NULL, start_frame, NULL);
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    return engine_add_clip_to_track_with_id(engine, track_index, filepath, NULL, start_frame, out_clip_index);
}

bool engine_add_clip_to_track_with_id(Engine* engine,
                                      int track_index,
                                      const char* filepath,
                                      const char* media_id,
                                      uint64_t start_frame,
                                      int* out_clip_index) {
    if (!engine || !filepath) {
        return false;
    }

    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

    EngineAudioSource* source = engine_audio_source_get_or_create(engine, media_id, filepath);
    EngineClip* clip_slot = engine_clip_create_with_source(engine,
                                                           track,
                                                           source,
                                                           filepath,
                                                           media_id,
                                                           start_frame,
                                                           0,
                                                           0,
                                                           1.0f,
                                                           0,
                                                           0,
                                                           true);
    if (!clip_slot) {
        return false;
    }

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

    engine_request_rebuild_sources(engine);
    return true;
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
    if (!clip || !clip->sampler) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        return false;
    }

    clip->timeline_start_frames = start_frame;
    engine_clip_refresh_sampler(engine, clip);

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

    engine_request_rebuild_sources(engine);
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
    if (!clip || !clip->sampler) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
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
    engine_clip_refresh_sampler(engine, clip);

    engine_request_rebuild_sources(engine);
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
    engine_request_rebuild_sources(engine);
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
    if (name && name[0] != '\0') {
        strncpy(clip->name, name, sizeof(clip->name) - 1);
        clip->name[sizeof(clip->name) - 1] = '\0';
    } else {
        clip->name[0] = '\0';
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
    engine_request_rebuild_sources(engine);
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
    if (!engine_clip_resolve_media(engine, clip)) {
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
    engine_clip_refresh_sampler(engine, clip);
    engine_trace(engine, "clip fades track=%d clip=%d in=%llu out=%llu",
                 track_index,
                 clip_index,
                 (unsigned long long)fade_in_frames,
                 (unsigned long long)fade_out_frames);
    return true;
}

bool engine_clip_set_fade_curves(Engine* engine,
                                 int track_index,
                                 int clip_index,
                                 EngineFadeCurve fade_in_curve,
                                 EngineFadeCurve fade_out_curve) {
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
    if (fade_in_curve < 0 || fade_in_curve >= ENGINE_FADE_CURVE_COUNT) {
        fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    }
    if (fade_out_curve < 0 || fade_out_curve >= ENGINE_FADE_CURVE_COUNT) {
        fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    }
    clip->fade_in_curve = fade_in_curve;
    clip->fade_out_curve = fade_out_curve;
    return true;
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
    if (UINT64_MAX - anchor_start < anchor_duration) {
        if (out_anchor_index) {
            *out_anchor_index = anchor_index;
        }
        return true;
    }
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
                const char* media_id = engine_clip_get_media_id(clip);
                const char* media_path = engine_clip_get_media_path(clip);
                strncpy(spawn->media_id, media_id ? media_id : "", sizeof(spawn->media_id) - 1);
                spawn->media_id[sizeof(spawn->media_id) - 1] = '\0';
                strncpy(spawn->media_path, media_path ? media_path : "", sizeof(spawn->media_path) - 1);
                strncpy(spawn->name, clip->name, sizeof(spawn->name) - 1);
                spawn->gain = clip->gain;
                spawn->fade_in_frames = clip->fade_in_frames;
                spawn->fade_out_frames = clip->fade_out_frames;
                spawn->fade_in_curve = clip->fade_in_curve;
                spawn->fade_out_curve = clip->fade_out_curve;
                engine_clip_snapshot_automation(clip, &spawn->automation_lanes, &spawn->automation_lane_count);
                spawn->start_frames = anchor_end;
                spawn->offset_frames = clip->offset_frames + left_duration;
                spawn->duration_frames = right_duration;
                if (spawn->offset_frames >= total_frames) {
                    spawn->offset_frames = total_frames - 1;
                }
                if (spawn->offset_frames + spawn->duration_frames > total_frames) {
                    spawn->duration_frames = total_frames - spawn->offset_frames;
                }
            }
        } else if (overlaps_left) {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_TRIM_END;
            op_count++;
        } else if (overlaps_right) {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_SHIFT_START;
            op_count++;
        } else {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
            op_count++;
        }
    }

    bool changed = false;
    for (int i = 0; i < op_count; ++i) {
        EngineNoOverlapOp* op = &ops[i];
        if (!op->sampler) {
            continue;
        }
        int clip_index = engine_track_find_clip_by_sampler(track, op->sampler);
        if (clip_index < 0 || clip_index >= track->clip_count) {
            continue;
        }
        EngineClip* clip = &track->clips[clip_index];
        if (!clip) {
            continue;
        }
        switch (op->action) {
            case ENGINE_NO_OVERLAP_REMOVE:
                if (engine_remove_clip(engine, track_index, clip_index)) {
                    changed = true;
                }
                break;
            case ENGINE_NO_OVERLAP_TRIM_END: {
                uint64_t new_duration = anchor_start - clip->timeline_start_frames;
                if (new_duration > 0) {
                    if (engine_clip_set_region(engine, track_index, clip_index, clip->offset_frames, new_duration)) {
                        changed = true;
                        engine_clip_clamp_fades(engine, track_index, clip_index);
                    }
                } else {
                    if (engine_remove_clip(engine, track_index, clip_index)) {
                        changed = true;
                    }
                }
                break;
            }
            case ENGINE_NO_OVERLAP_SHIFT_START: {
                uint64_t clip_duration = engine_clip_effective_duration(clip);
                if (clip_duration == 0) {
                    break;
                }
                uint64_t shift = anchor_end - clip->timeline_start_frames;
                uint64_t new_offset = clip->offset_frames + shift;
                uint64_t total_frames = engine_clip_get_total_frames(engine, track_index, clip_index);
                if (new_offset >= total_frames) {
                    break;
                }
                uint64_t max_duration = total_frames - new_offset;
                if (clip_duration > max_duration) {
                    clip_duration = max_duration;
                }
                if (engine_clip_set_region(engine, track_index, clip_index, new_offset, clip_duration)) {
                    if (engine_clip_set_timeline_start(engine, track_index, clip_index, anchor_end, NULL)) {
                        changed = true;
                        engine_clip_clamp_fades(engine, track_index, clip_index);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    for (int i = 0; i < spawn_count; ++i) {
        EngineNoOverlapSpawn* spawn = &spawns[i];
        if (spawn->duration_frames == 0) {
            continue;
        }
        int new_index = -1;
        bool ok = engine_add_clip_to_track_with_id(engine,
                                                   track_index,
                                                   spawn->media_path,
                                                   spawn->media_id,
                                                   spawn->start_frames,
                                                   &new_index);
        if (!ok || new_index < 0) {
            continue;
        }
        if (spawn->offset_frames > 0 || spawn->duration_frames > 0) {
            if (!engine_clip_set_region(engine, track_index, new_index, spawn->offset_frames, spawn->duration_frames)) {
                continue;
            }
        }
        if (spawn->gain != 0.0f) {
            engine_clip_set_gain(engine, track_index, new_index, spawn->gain);
        }
        if (spawn->name[0] != '\0') {
            engine_clip_set_name(engine, track_index, new_index, spawn->name);
        }
        if (spawn->fade_in_frames != 0 || spawn->fade_out_frames != 0) {
            engine_clip_set_fades(engine, track_index, new_index, spawn->fade_in_frames, spawn->fade_out_frames);
        }
        engine_clip_set_fade_curves(engine, track_index, new_index, spawn->fade_in_curve, spawn->fade_out_curve);
        if (spawn->automation_lanes && spawn->automation_lane_count > 0) {
            EngineClip* new_clip = engine_get_clip_mutable(engine, track_index, new_index);
            if (new_clip) {
                engine_clip_set_automation_lanes_internal(new_clip, spawn->automation_lanes, spawn->automation_lane_count);
                engine_sampler_source_set_automation(new_clip->sampler,
                                                     new_clip->automation_lanes,
                                                     new_clip->automation_lane_count);
            }
        }
        changed = true;
    }

    if (ops) {
        SDL_free(ops);
    }
    if (spawns) {
        for (int i = 0; i < spawn_count; ++i) {
            EngineNoOverlapSpawn* spawn = &spawns[i];
            if (spawn->automation_lanes) {
                for (int j = 0; j < spawn->automation_lane_count; ++j) {
                    engine_automation_lane_free(&spawn->automation_lanes[j]);
                }
                free(spawn->automation_lanes);
                spawn->automation_lanes = NULL;
                spawn->automation_lane_count = 0;
            }
        }
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
    if (!engine || !source_clip || segment_length_frames == 0) {
        return false;
    }
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

    EngineClip* mutable_source = (EngineClip*)source_clip;
    if (!engine_clip_resolve_media(engine, mutable_source)) {
        return false;
    }
    const AudioMediaClip* media_src = mutable_source->media;
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

    const char* media_id = engine_clip_get_media_id(source_clip);
    const char* media_path = engine_clip_get_media_path(source_clip);
    EngineAudioSource* source = source_clip->source;
    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, media_path);
    }
    EngineClip* new_clip = engine_clip_create_with_source(engine,
                                                          track,
                                                          source,
                                                          media_path ? media_path : "",
                                                          media_id && media_id[0] != '\0' ? media_id : NULL,
                                                          start_frame,
                                                          source_clip->offset_frames + source_relative_offset_frames,
                                                          segment_length_frames,
                                                          source_clip->gain,
                                                          source_clip->fade_in_frames,
                                                          source_clip->fade_out_frames,
                                                          false);
    if (!new_clip) {
        return false;
    }

    if (source_clip->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s segment", source_clip->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip segment");
    }
    new_clip->fade_in_curve = source_clip->fade_in_curve;
    new_clip->fade_out_curve = source_clip->fade_out_curve;
    engine_clip_copy_automation(source_clip, new_clip);
    engine_sampler_source_set_automation(new_clip->sampler,
                                         new_clip->automation_lanes,
                                         new_clip->automation_lane_count);

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

    engine_request_rebuild_sources(engine);
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
    if (!original) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, original)) {
        return false;
    }

    uint64_t offset = start_frame_offset;
    uint64_t new_start = original->timeline_start_frames + original->duration_frames + offset;
    const char* media_id = engine_clip_get_media_id(original);
    const char* media_path = engine_clip_get_media_path(original);
    EngineAudioSource* source = original->source;
    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, media_path);
    }
    EngineClip* new_clip = engine_clip_create_with_source(engine,
                                                          track,
                                                          source,
                                                          media_path ? media_path : "",
                                                          media_id && media_id[0] != '\0' ? media_id : NULL,
                                                          new_start,
                                                          original->offset_frames,
                                                          original->duration_frames,
                                                          original->gain,
                                                          original->fade_in_frames,
                                                          original->fade_out_frames,
                                                          false);
    if (!new_clip) {
        return false;
    }

    if (original->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s copy", original->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip copy");
    }
    new_clip->fade_in_curve = original->fade_in_curve;
    new_clip->fade_out_curve = original->fade_out_curve;
    engine_clip_copy_automation(original, new_clip);
    engine_sampler_source_set_automation(new_clip->sampler,
                                         new_clip->automation_lanes,
                                         new_clip->automation_lane_count);
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

    engine_request_rebuild_sources(engine);
    return true;
}

const char* engine_clip_get_media_id(const EngineClip* clip) {
    if (!clip) {
        return NULL;
    }
    if (clip->source && clip->source->media_id[0] != '\0') {
        return clip->source->media_id;
    }
    return NULL;
}

const char* engine_clip_get_media_path(const EngineClip* clip) {
    if (!clip) {
        return NULL;
    }
    if (clip->source && clip->source->path[0] != '\0') {
        return clip->source->path;
    }
    return NULL;
}
