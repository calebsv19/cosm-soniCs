#include "engine/engine_internal.h"
#include "engine/engine_clips_automation_internal.h"
#include "engine/sampler.h"
#include "engine/timeline_contract.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

        bool clip_newer = clip->creation_index > anchor_clip->creation_index;
        if (clip_newer) {
            continue;
        }

        DawTimelineFrameRange clip_range = daw_timeline_frame_range(clip_start, clip_duration);
        DawTimelineFrameRange anchor_range = daw_timeline_frame_range(anchor_start, anchor_duration);
        DawTimelineOverlapPlan overlap = daw_timeline_analyze_overlap(clip_range, anchor_range);
        if (overlap.kind == DAW_TIMELINE_OVERLAP_NONE) {
            continue;
        }

        if (overlap.kind == DAW_TIMELINE_OVERLAP_SPLIT) {
            uint64_t total_frames = engine_clip_get_total_frames(engine, track_index, i);

            if (overlap.left_duration_frames == 0) {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_REMOVE;
                op_count++;
            } else {
                ops[op_count].sampler = clip->sampler;
                ops[op_count].action = ENGINE_NO_OVERLAP_TRIM_END;
                op_count++;
            }

            if (overlap.right_duration_frames > 0 && spawn_count < track->clip_count) {
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
                spawn->start_frames = overlap.right_start_frame;
                spawn->offset_frames = clip->offset_frames + overlap.source_offset_delta_frames;
                spawn->duration_frames = overlap.right_duration_frames;
                if (spawn->offset_frames >= total_frames) {
                    spawn->offset_frames = total_frames - 1;
                }
                if (spawn->offset_frames + spawn->duration_frames > total_frames) {
                    spawn->duration_frames = total_frames - spawn->offset_frames;
                }
            }
        } else if (overlap.kind == DAW_TIMELINE_OVERLAP_TRIM_END) {
            ops[op_count].sampler = clip->sampler;
            ops[op_count].action = ENGINE_NO_OVERLAP_TRIM_END;
            op_count++;
        } else if (overlap.kind == DAW_TIMELINE_OVERLAP_SHIFT_START) {
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
                DawTimelineOverlapPlan overlap =
                    daw_timeline_analyze_overlap(daw_timeline_frame_range(clip->timeline_start_frames, clip_duration),
                                                 daw_timeline_frame_range(anchor_start, anchor_duration));
                if (overlap.kind != DAW_TIMELINE_OVERLAP_SHIFT_START &&
                    overlap.kind != DAW_TIMELINE_OVERLAP_SPLIT) {
                    break;
                }
                uint64_t new_offset = clip->offset_frames + overlap.source_offset_delta_frames;
                uint64_t total_frames = engine_clip_get_total_frames(engine, track_index, clip_index);
                if (new_offset >= total_frames) {
                    break;
                }
                uint64_t max_duration = total_frames - new_offset;
                uint64_t new_duration = overlap.right_duration_frames;
                if (new_duration > max_duration) {
                    new_duration = max_duration;
                }
                if (new_duration == 0) {
                    break;
                }
                if (engine_clip_set_region(engine, track_index, clip_index, new_offset, new_duration)) {
                    if (engine_clip_set_timeline_start(engine, track_index, clip_index, overlap.right_start_frame, NULL)) {
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
            EngineTrack* new_track = engine_get_track_mutable(engine, track_index);
            EngineClip* new_clip = (new_track && new_index >= 0 && new_index < new_track->clip_count)
                                       ? &new_track->clips[new_index]
                                       : NULL;
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
