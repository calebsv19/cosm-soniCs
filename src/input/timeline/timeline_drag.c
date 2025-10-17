#include "input/timeline_drag.h"

#include "engine/engine.h"
#include "engine/sampler.h"

#include <SDL2/SDL.h>
#include <string.h>

#define TIMELINE_MAX_OVERLAP_OPS 32

typedef enum {
    CLIP_OP_REMOVE,
    CLIP_OP_TRIM_LEFT,
    CLIP_OP_TRIM_RIGHT,
    CLIP_OP_SPLIT
} TimelineClipOpType;

typedef struct TimelineClipSegment {
    bool valid;
    uint64_t start_frames;
    uint64_t offset_frames;
    uint64_t duration_frames;
} TimelineClipSegment;

typedef struct {
    TimelineClipOpType type;
    uint64_t creation_index;
    char media_path[ENGINE_CLIP_PATH_MAX];
    char name[ENGINE_CLIP_NAME_MAX];
    float gain;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    TimelineClipSegment left;
    TimelineClipSegment right;
} TimelineClipOp;

static int find_clip_index_by_creation(const EngineTrack* track, uint64_t creation_index) {
    if (!track) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].creation_index == creation_index) {
            return i;
        }
    }
    return -1;
}

static uint64_t timeline_clip_effective_duration(const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    if (clip->duration_frames > 0) {
        return clip->duration_frames;
    }
    if (clip->media) {
        return clip->media->frame_count;
    }
    return 0;
}

static int timeline_find_clip_index_by_sampler(const EngineTrack* track, const EngineSamplerSource* sampler) {
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


static void apply_clip_name(AppState* state, int track_index, int clip_index, const char* base_name, const char* suffix) {
    if (!state || !state->engine || !base_name) {
        return;
    }
    if (base_name[0] == '\0') {
        return;
    }
    char label[ENGINE_CLIP_NAME_MAX];
    if (suffix && suffix[0] != '\0') {
        snprintf(label, sizeof(label), "%s %s", base_name, suffix);
    } else {
        snprintf(label, sizeof(label), "%s", base_name);
    }
    engine_clip_set_name(state->engine, track_index, clip_index, label);
}

static void apply_segment_metadata(AppState* state,
                                   int track_index,
                                   int clip_index,
                                   const TimelineClipOp* op,
                                   uint64_t segment_duration,
                                   const char* suffix) {
    if (!state || !state->engine || clip_index < 0) {
        return;
    }
    engine_clip_set_gain(state->engine, track_index, clip_index, op->gain);
    uint64_t fade_in = op->fade_in_frames > segment_duration ? segment_duration : op->fade_in_frames;
    uint64_t fade_out = op->fade_out_frames > segment_duration ? segment_duration : op->fade_out_frames;
    engine_clip_set_fades(state->engine, track_index, clip_index, fade_in, fade_out);
    apply_clip_name(state, track_index, clip_index, op->name, suffix);
}

int timeline_resolve_overlapping_clips(AppState* state, int track_index, EngineSamplerSource* new_sampler) {
    if (!state || !state->engine || track_index < 0 || !new_sampler) {
        return -1;
    }

    for (;;) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (!tracks) {
            return -1;
        }
        const EngineTrack* track = &tracks[track_index];
        if (!track) {
            return -1;
        }

        int new_clip_index = timeline_find_clip_index_by_sampler(track, new_sampler);
        if (new_clip_index < 0 || new_clip_index >= track->clip_count) {
            return -1;
        }
        const EngineClip* new_clip = &track->clips[new_clip_index];
        uint64_t new_duration = timeline_clip_effective_duration(new_clip);
        uint64_t new_start = new_clip->timeline_start_frames;
        uint64_t new_end = new_start + new_duration;

        int max_ops = track->clip_count > 0 ? track->clip_count : 1;
        if (max_ops > TIMELINE_MAX_OVERLAP_OPS) {
            max_ops = TIMELINE_MAX_OVERLAP_OPS;
        }
        TimelineClipOp ops[max_ops];
        int op_count = 0;
        bool adjusted = false;

        for (int i = 0; i < track->clip_count; ++i) {
            if (i == new_clip_index) {
                continue;
            }
            const EngineClip* clip = &track->clips[i];
            if (!clip || !clip->media) {
                continue;
            }
            uint64_t clip_start = clip->timeline_start_frames;
            uint64_t clip_duration = timeline_clip_effective_duration(clip);
            uint64_t clip_end = clip_start + clip_duration;

            if (clip_end <= new_start || clip_start >= new_end) {
                continue;
            }

            adjusted = true;
            if (op_count >= max_ops) {
                SDL_Log("timeline_resolve_overlapping_clips: too many overlaps");
                break;
            }

            TimelineClipOp* op = &ops[op_count++];
            memset(op, 0, sizeof(*op));
            op->creation_index = clip->creation_index;
            op->gain = clip->gain;
            op->fade_in_frames = clip->fade_in_frames;
            op->fade_out_frames = clip->fade_out_frames;
            strncpy(op->media_path, clip->media_path, sizeof(op->media_path) - 1);
            op->media_path[sizeof(op->media_path) - 1] = '\0';
            strncpy(op->name, clip->name, sizeof(op->name) - 1);
            op->name[sizeof(op->name) - 1] = '\0';

            uint64_t left_frames = new_start > clip_start ? (new_start - clip_start) : 0;
            uint64_t right_frames = clip_end > new_end ? (clip_end - new_end) : 0;

            if (left_frames == 0 && right_frames == 0) {
                op->type = CLIP_OP_REMOVE;
            } else if (left_frames > 0 && right_frames == 0) {
                op->type = CLIP_OP_TRIM_LEFT;
                op->left.valid = true;
                op->left.start_frames = clip_start;
                op->left.offset_frames = clip->offset_frames;
                op->left.duration_frames = left_frames;
            } else if (left_frames == 0 && right_frames > 0) {
                op->type = CLIP_OP_TRIM_RIGHT;
                op->right.valid = true;
                op->right.start_frames = new_end;
                op->right.offset_frames = clip->offset_frames + (new_end - clip_start);
                op->right.duration_frames = right_frames;
            } else {
                op->type = CLIP_OP_SPLIT;
                op->left.valid = true;
                op->left.start_frames = clip_start;
                op->left.offset_frames = clip->offset_frames;
                op->left.duration_frames = left_frames;
                op->right.valid = true;
                op->right.start_frames = new_end;
                op->right.offset_frames = clip->offset_frames + (new_end - clip_start);
                op->right.duration_frames = right_frames;
            }
        }

        for (int op_index = 0; op_index < op_count; ++op_index) {
            TimelineClipOp* op = &ops[op_index];
            SDL_Log("op index=%d type=%d left_valid=%d left_start=%llu left_dur=%llu right_valid=%d right_start=%llu right_dur=%llu",
                    op_index, op->type, op->left.valid, (unsigned long long)op->left.start_frames,
                    (unsigned long long)op->left.duration_frames, op->right.valid,
                    (unsigned long long)op->right.start_frames, (unsigned long long)op->right.duration_frames);
        }
        if (!adjusted || op_count == 0) {
            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
            if (!refreshed_tracks) {
                return -1;
            }
            const EngineTrack* refreshed_track = &refreshed_tracks[track_index];
            return timeline_find_clip_index_by_sampler(refreshed_track, new_sampler);
        }

        for (int op_index = 0; op_index < op_count; ++op_index) {
            TimelineClipOp* op = &ops[op_index];
            const EngineTrack* cur_tracks = engine_get_tracks(state->engine);
            const EngineTrack* cur_track = cur_tracks ? &cur_tracks[track_index] : NULL;
            int idx = cur_track ? find_clip_index_by_creation(cur_track, op->creation_index) : -1;
            if (idx >= 0) {
                engine_remove_clip(state->engine, track_index, idx);
            }
        }

        for (int op_index = 0; op_index < op_count; ++op_index) {
            TimelineClipOp* op = &ops[op_index];
            const char* media_path = op->media_path[0] ? op->media_path : NULL;
            if (!media_path) {
                continue;
            }

            const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
            (void)cfg;

            switch (op->type) {
                case CLIP_OP_REMOVE:
                    break;
                case CLIP_OP_TRIM_LEFT: {
                    if (!op->left.valid) {
                        break;
                    }
                    int new_idx = -1;
                    if (engine_add_clip_to_track(state->engine, track_index, media_path, op->left.start_frames, &new_idx)) {
                        const EngineTrack* before_tracks = engine_get_tracks(state->engine);
                        const EngineClip* before_clip = before_tracks ? &before_tracks[track_index].clips[new_idx] : NULL;
                        uint64_t new_creation = before_clip ? before_clip->creation_index : 0;
                        if (!engine_clip_set_region(state->engine, track_index, new_idx, op->left.offset_frames, op->left.duration_frames)) {
                            SDL_Log("timeline_resolve_overlapping_clips: failed to set region for left segment");
                        }
                        const EngineTrack* after_tracks = engine_get_tracks(state->engine);
                        const EngineTrack* after_track = after_tracks ? &after_tracks[track_index] : NULL;
                        int applied_idx = after_track ? find_clip_index_by_creation(after_track, new_creation) : -1;
                        if (applied_idx >= 0) {
                            apply_segment_metadata(state, track_index, applied_idx, op, op->left.duration_frames, NULL);
                        }
                    }
                    break;
                }
                case CLIP_OP_TRIM_RIGHT: {
                    if (!op->right.valid) {
                        break;
                    }
                    int new_idx = -1;
                    if (engine_add_clip_to_track(state->engine, track_index, media_path, op->right.start_frames, &new_idx)) {
                        const EngineTrack* before_tracks = engine_get_tracks(state->engine);
                        const EngineClip* before_clip = before_tracks ? &before_tracks[track_index].clips[new_idx] : NULL;
                        uint64_t new_creation = before_clip ? before_clip->creation_index : 0;
                        if (!engine_clip_set_region(state->engine, track_index, new_idx, op->right.offset_frames, op->right.duration_frames)) {
                            SDL_Log("timeline_resolve_overlapping_clips: failed to set region for right segment");
                        }
                        const EngineTrack* after_tracks = engine_get_tracks(state->engine);
                        const EngineTrack* after_track = after_tracks ? &after_tracks[track_index] : NULL;
                        int applied_idx = after_track ? find_clip_index_by_creation(after_track, new_creation) : -1;
                        if (applied_idx >= 0) {
                            apply_segment_metadata(state, track_index, applied_idx, op, op->right.duration_frames, NULL);
                        }
                    }
                    break;
                }
                case CLIP_OP_SPLIT: {
                    if (op->left.valid) {
                        int left_idx = -1;
                        if (engine_add_clip_to_track(state->engine, track_index, media_path, op->left.start_frames, &left_idx)) {
                            const EngineTrack* before_tracks = engine_get_tracks(state->engine);
                            const EngineClip* before_clip = before_tracks ? &before_tracks[track_index].clips[left_idx] : NULL;
                            uint64_t left_creation = before_clip ? before_clip->creation_index : 0;
                            if (!engine_clip_set_region(state->engine, track_index, left_idx, op->left.offset_frames, op->left.duration_frames)) {
                                SDL_Log("timeline_resolve_overlapping_clips: failed to set region for split left");
                            }
                            const EngineTrack* after_tracks = engine_get_tracks(state->engine);
                            const EngineTrack* after_track = after_tracks ? &after_tracks[track_index] : NULL;
                            int applied_idx = after_track ? find_clip_index_by_creation(after_track, left_creation) : -1;
                            if (applied_idx >= 0) {
                                apply_segment_metadata(state, track_index, applied_idx, op, op->left.duration_frames, NULL);
                            }
                        }
                    }
                    if (op->right.valid) {
                        int right_idx = -1;
                        if (engine_add_clip_to_track(state->engine, track_index, media_path, op->right.start_frames, &right_idx)) {
                            const EngineTrack* before_tracks = engine_get_tracks(state->engine);
                            const EngineClip* before_clip = before_tracks ? &before_tracks[track_index].clips[right_idx] : NULL;
                            uint64_t right_creation = before_clip ? before_clip->creation_index : 0;
                            if (!engine_clip_set_region(state->engine, track_index, right_idx, op->right.offset_frames, op->right.duration_frames)) {
                                SDL_Log("timeline_resolve_overlapping_clips: failed to set region for split right");
                            }
                            const EngineTrack* after_tracks = engine_get_tracks(state->engine);
                            const EngineTrack* after_track = after_tracks ? &after_tracks[track_index] : NULL;
                            int applied_idx = after_track ? find_clip_index_by_creation(after_track, right_creation) : -1;
                            if (applied_idx >= 0) {
                                apply_segment_metadata(state, track_index, applied_idx, op, op->right.duration_frames, NULL);
                            }
                        }
                    }
                    break;
                }
            }
        }

        const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
        if (!refreshed_tracks) {
            return -1;
        }
        const EngineTrack* refreshed_track = &refreshed_tracks[track_index];
        int resolved_index = timeline_find_clip_index_by_sampler(refreshed_track, new_sampler);
        if (resolved_index >= 0) {
            return resolved_index;
        }
    }
}

bool timeline_find_clip_by_sampler(const AppState* state, EngineSamplerSource* sampler, int* out_track, int* out_clip) {
    if (!state || !state->engine || !sampler) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks) {
        return false;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) {
            continue;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            if (track->clips[c].sampler == sampler) {
                if (out_track) *out_track = t;
                if (out_clip) *out_clip = c;
                return true;
            }
        }
    }
    return false;
}

int timeline_move_clip_to_track(AppState* state, int src_track, int clip_index, int dst_track, uint64_t start_frame) {
    if (!state || !state->engine) {
        return -1;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || src_track < 0 || src_track >= track_count) {
        return -1;
    }
    if (dst_track < 0) {
        return -1;
    }
    const EngineTrack* source_track = &tracks[src_track];
    if (!source_track || clip_index < 0 || clip_index >= source_track->clip_count) {
        return -1;
    }
    const EngineClip* clip = &source_track->clips[clip_index];
    if (!clip || clip->media_path[0] == '\0') {
        return -1;
    }

    uint64_t total_frames = engine_clip_get_total_frames(state->engine, src_track, clip_index);
    uint64_t duration_frames = clip->duration_frames;
    if (duration_frames == 0 && total_frames > clip->offset_frames) {
        duration_frames = total_frames - clip->offset_frames;
    }
    if (duration_frames == 0) {
        duration_frames = total_frames;
    }

    int new_clip_index = -1;
    if (!engine_add_clip_to_track(state->engine, dst_track, clip->media_path, start_frame, &new_clip_index)) {
        SDL_Log("timeline_move_clip_to_track: failed to add clip to track %d", dst_track);
        return -1;
    }

    const EngineTrack* after_add_tracks = engine_get_tracks(state->engine);
    int after_add_count = engine_get_track_count(state->engine);
    if (!after_add_tracks || dst_track < 0 || dst_track >= after_add_count) {
        return -1;
    }
    const EngineTrack* dst_track_ptr = &after_add_tracks[dst_track];
    if (!dst_track_ptr || dst_track_ptr->clip_count <= 0) {
        return -1;
    }
    if (new_clip_index < 0 || new_clip_index >= dst_track_ptr->clip_count) {
        new_clip_index = dst_track_ptr->clip_count - 1;
    }

    engine_clip_set_name(state->engine, dst_track, new_clip_index, clip->name);
    engine_clip_set_gain(state->engine, dst_track, new_clip_index, clip->gain);
    engine_clip_set_region(state->engine, dst_track, new_clip_index, clip->offset_frames, duration_frames);
    engine_clip_set_fades(state->engine, dst_track, new_clip_index, clip->fade_in_frames, clip->fade_out_frames);

    const EngineTrack* after_config_tracks = engine_get_tracks(state->engine);
    int after_config_count = engine_get_track_count(state->engine);
    const EngineTrack* dest_track_after = NULL;
    if (after_config_tracks && dst_track >= 0 && dst_track < after_config_count) {
        dest_track_after = &after_config_tracks[dst_track];
    }
    if (dest_track_after && dest_track_after->clip_count > 0) {
        if (new_clip_index < 0 || new_clip_index >= dest_track_after->clip_count) {
            new_clip_index = dest_track_after->clip_count - 1;
        }
        EngineSamplerSource* sampler = dest_track_after->clips[new_clip_index].sampler;
        if (sampler) {
            int resolved = timeline_resolve_overlapping_clips(state, dst_track, sampler);
            if (resolved >= 0) {
                new_clip_index = resolved;
            }
        }
    }

    engine_remove_clip(state->engine, src_track, clip_index);
    return new_clip_index;
}
