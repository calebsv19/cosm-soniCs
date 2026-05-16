#include "engine/engine_clips_automation_internal.h"

#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "engine/sampler.h"

#include <stdlib.h>

void engine_clip_init_automation(EngineClip* clip) {
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

bool engine_clip_copy_automation(const EngineClip* src, EngineClip* dst) {
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

bool engine_clip_set_automation_lanes_internal(EngineClip* clip,
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

bool engine_clip_snapshot_automation(const EngineClip* clip,
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

static EngineClip* engine_get_clip_mutable(Engine* engine, int track_index, int clip_index) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[clip_index];
}

static void engine_clip_refresh_automation_sources(Engine* engine, int track_index, int clip_index, EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->sampler) {
        engine_sampler_source_set_automation(clip->sampler, clip->automation_lanes, clip->automation_lane_count);
    }
    if (clip->instrument) {
        EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(engine, track_index, clip_index);
        EngineInstrumentParams params = engine_clip_midi_effective_instrument_params(engine, track_index, clip_index);
        const EngineAutomationLane* track_lanes = NULL;
        int track_lane_count = 0;
        if (engine_clip_midi_inherits_track_instrument(clip)) {
            engine_track_midi_get_instrument_automation_lanes(engine, track_index, &track_lanes, &track_lane_count);
        }
        engine_instrument_source_set_midi_clip(clip->instrument,
                                               clip->timeline_start_frames,
                                               clip->duration_frames,
                                               preset,
                                               params,
                                               clip->midi_notes.notes,
                                               clip->midi_notes.note_count,
                                               track_lanes,
                                               track_lane_count,
                                               clip->automation_lanes,
                                               clip->automation_lane_count);
    }
}

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
    if (engine_automation_target_is_instrument_param(target) && clip->kind != ENGINE_CLIP_KIND_MIDI) {
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
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
    engine_clip_refresh_automation_sources(engine, track_index, clip_index, clip);
    return true;
}
