#include "input/timeline_drag.h"

#include "engine/engine.h"
#include "engine/sampler.h"

#include <SDL2/SDL.h>

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
    const char* media_path = engine_clip_get_media_path(clip);
    const char* media_id = engine_clip_get_media_id(clip);
    if (!clip || !media_path || media_path[0] == '\0') {
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
    if (!engine_add_clip_to_track_with_id(state->engine,
                                          dst_track,
                                          media_path,
                                          media_id,
                                          start_frame,
                                          &new_clip_index)) {
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
    if (!engine_clip_set_region(state->engine,
                                dst_track,
                                new_clip_index,
                                clip->offset_frames,
                                duration_frames)) {
        SDL_Log("timeline_move_clip_to_track: failed to set region on destination clip");
    }
    engine_clip_set_fades(state->engine,
                          dst_track,
                          new_clip_index,
                          clip->fade_in_frames,
                          clip->fade_out_frames);

    if (dst_track != src_track) {
        engine_remove_clip(state->engine, src_track, clip_index);
    }

    return new_clip_index;
}
