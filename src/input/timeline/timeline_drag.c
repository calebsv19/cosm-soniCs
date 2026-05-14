#include "input/timeline_drag.h"

#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/timeline/timeline_clip_helpers.h"

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
    if (!clip) {
        return -1;
    }
    if (engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI) {
        if (dst_track == src_track) {
            int updated_index = clip_index;
            if (engine_clip_set_timeline_start(state->engine, src_track, clip_index, start_frame, &updated_index)) {
                return updated_index;
            }
            return -1;
        }
        SessionClip snapshot = {0};
        if (!timeline_session_clip_from_engine(clip, &snapshot)) {
            return -1;
        }
        int new_clip_index = -1;
        bool ok = engine_add_midi_clip_to_track(state->engine,
                                                dst_track,
                                                start_frame,
                                                snapshot.duration_frames,
                                                &new_clip_index);
        if (ok) {
            engine_clip_set_name(state->engine, dst_track, new_clip_index, snapshot.name);
            engine_clip_set_gain(state->engine, dst_track, new_clip_index, snapshot.gain == 0.0f ? 1.0f : snapshot.gain);
            engine_clip_set_region(state->engine, dst_track, new_clip_index, snapshot.offset_frames, snapshot.duration_frames);
            engine_clip_set_fades(state->engine, dst_track, new_clip_index,
                                  snapshot.fade_in_frames,
                                  snapshot.fade_out_frames);
            engine_clip_set_fade_curves(state->engine, dst_track, new_clip_index,
                                        snapshot.fade_in_curve,
                                        snapshot.fade_out_curve);
            engine_clip_midi_set_instrument_preset(state->engine,
                                                   dst_track,
                                                   new_clip_index,
                                                   snapshot.instrument_preset);
            engine_clip_midi_set_instrument_params(state->engine,
                                                   dst_track,
                                                   new_clip_index,
                                                   snapshot.instrument_params);
            for (int n = 0; n < snapshot.midi_note_count; ++n) {
                engine_clip_midi_add_note(state->engine, dst_track, new_clip_index, snapshot.midi_notes[n], NULL);
            }
            for (int l = 0; l < snapshot.automation_lane_count; ++l) {
                SessionAutomationLane* lane = &snapshot.automation_lanes[l];
                engine_clip_set_automation_lane_points(state->engine,
                                                       dst_track,
                                                       new_clip_index,
                                                       lane->target,
                                                       (const EngineAutomationPoint*)lane->points,
                                                       lane->point_count);
            }
            engine_remove_clip(state->engine, src_track, clip_index);
        }
        timeline_session_clip_clear(&snapshot);
        return ok ? new_clip_index : -1;
    }

    const char* media_path = engine_clip_get_media_path(clip);
    const char* media_id = engine_clip_get_media_id(clip);
    if (!media_path || media_path[0] == '\0') {
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
