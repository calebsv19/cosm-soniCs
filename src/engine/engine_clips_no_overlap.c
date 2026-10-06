#include "engine/engine_internal.h"
#include "engine/engine_clips_automation_internal.h"
#include "engine/sampler.h"
#include "engine/timeline_contract.h"

#include <SDL2/SDL.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// Returns the playable stored duration, falling back to remaining media frames.
static uint64_t engine_clip_effective_duration(const EngineClip* clip) {
    if (clip->duration_frames) return clip->duration_frames;
    if (!clip->media || clip->offset_frames >= clip->media->frame_count) return 0;
    return clip->media->frame_count - clip->offset_frames;
}

// Finds the unchanged anchor source after descriptor sorting.
static int engine_track_find_clip_by_sampler(const EngineTrack* track, EngineSamplerSource* sampler) {
    for (int i = 0; i < track->clip_count; ++i)
        if (track->clips[i].sampler == sampler) return i;
    return -1;
}

// Prepares an independently owned audio region without mutating the original source or automation.
static bool overlap_prepare_region(Engine* engine, const EngineClip* source, EngineClip* result,
                                    uint64_t start, uint64_t offset, uint64_t duration, bool spawned) {
    if (!source->media || !source->sampler || offset >= source->media->frame_count || !duration) return false;
    uint64_t available = source->media->frame_count - offset;
    if (duration > available) duration = available;
    if (spawned && engine->next_clip_id == UINT64_MAX) return false;
    *result = *source;
    result->media = NULL;
    result->sampler = NULL;
    result->instrument = NULL;
    result->midi_notes = (EngineMidiNoteList){0};
    result->automation_lanes = NULL;
    result->automation_lane_count = result->automation_lane_capacity = 0;
    if (!audio_media_cache_retain(&engine->media_cache, source->media)) return false;
    result->media = source->media;
    result->sampler = engine_sampler_source_clone(source->sampler);
    if (!result->sampler || !engine_clip_copy_automation(source, result)) {
        engine_clip_destroy(engine, result);
        return false;
    }
    result->timeline_start_frames = start;
    result->offset_frames = offset;
    result->duration_frames = duration;
    if (result->fade_in_frames > duration) result->fade_in_frames = duration;
    if (result->fade_out_frames > duration - result->fade_in_frames)
        result->fade_out_frames = duration - result->fade_in_frames;
    if (spawned) {
        result->creation_index = engine->next_clip_id++;
        result->selected = false;
    }
    engine_sampler_source_set_clip(result->sampler, result->media, start, offset, duration,
                                    result->fade_in_frames, result->fade_out_frames);
    return true;
}

// Resolves all overlap mutations in one candidate and publishes either the complete result or nothing.
static bool engine_track_overlap_edit(Engine* engine, EngineTrack* track,
                                       EngineSamplerSource* anchor_sampler, int* out_anchor_index, bool publish) {
    if (!engine || !track || !anchor_sampler) return false;
    int anchor_index = engine_track_find_clip_by_sampler(track, anchor_sampler);
    if (anchor_index < 0) return false;
    const EngineClip* anchor = &track->clips[anchor_index];
    uint64_t anchor_duration = engine_clip_effective_duration(anchor);
    if (!anchor_duration || anchor_duration > UINT64_MAX - anchor->timeline_start_frames || track->clip_count <= 1) {
        if (out_anchor_index) *out_anchor_index = anchor_index;
        return true;
    }
    int count = track->clip_count;
    if (count > INT_MAX / 2) return false;
    EngineClip* clips = calloc((size_t)count * 2, sizeof(*clips));
    EngineClip* owned = calloc((size_t)count * 2, sizeof(*owned));
    bool* replaced = calloc((size_t)count, sizeof(*replaced));
    int written = 0, owned_count = 0;
    bool changed = false, accepted = false;
    if (!clips || !owned || !replaced) goto cleanup;
    for (int i = 0; i < count; ++i) {
        const EngineClip* clip = &track->clips[i];
        if (!clip->sampler || clip->sampler == anchor_sampler) {
            clips[written++] = *clip;
            continue;
        }
        uint64_t duration = engine_clip_effective_duration(clip);
        DawTimelineOverlapPlan plan = {0};
        if (!duration) plan.kind = DAW_TIMELINE_OVERLAP_REMOVE;
        else if (clip->creation_index <= anchor->creation_index)
            plan = daw_timeline_analyze_overlap(daw_timeline_frame_range(clip->timeline_start_frames, duration),
                daw_timeline_frame_range(anchor->timeline_start_frames, anchor_duration));
        if (plan.kind == DAW_TIMELINE_OVERLAP_NONE) {
            clips[written++] = *clip;
            continue;
        }
        replaced[i] = changed = true;
        if (plan.kind == DAW_TIMELINE_OVERLAP_TRIM_END || plan.kind == DAW_TIMELINE_OVERLAP_SPLIT) {
            if (!overlap_prepare_region(engine, clip, &owned[owned_count], clip->timeline_start_frames,
                                        clip->offset_frames, plan.left_duration_frames, false)) goto cleanup;
            clips[written++] = owned[owned_count++];
        }
        if (plan.kind == DAW_TIMELINE_OVERLAP_SHIFT_START || plan.kind == DAW_TIMELINE_OVERLAP_SPLIT) {
            if (plan.source_offset_delta_frames > UINT64_MAX - clip->offset_frames) goto cleanup;
            if (!overlap_prepare_region(engine, clip, &owned[owned_count], plan.right_start_frame,
                                        clip->offset_frames + plan.source_offset_delta_frames,
                                        plan.right_duration_frames, plan.kind == DAW_TIMELINE_OVERLAP_SPLIT)) goto cleanup;
            clips[written++] = owned[owned_count++];
        }
    }
    if (!changed) {
        accepted = true;
        if (out_anchor_index) *out_anchor_index = anchor_index;
        goto cleanup;
    }
    EngineTrack previous = *track;
    track->clips = clips;
    track->clip_count = written;
    track->clip_capacity = count * 2;
    engine_track_sort_clips(track);
    if (publish && !engine_request_rebuild_sources(engine)) {
        *track = previous;
        goto cleanup;
    }
    for (int i = 0; i < count; ++i)
        if (replaced[i]) engine_clip_destroy(engine, &previous.clips[i]);
    free(previous.clips);
    if (out_anchor_index) *out_anchor_index = engine_track_find_clip_by_sampler(track, anchor_sampler);
    clips = NULL;
    owned_count = 0;
    accepted = true;
cleanup:
    for (int i = 0; i < owned_count; ++i) engine_clip_destroy(engine, &owned[i]);
    free(clips);
    free(owned);
    free(replaced);
    return accepted;
}

// Publishes overlap changes to an existing editable track in one revision.
bool engine_track_apply_no_overlap(Engine* engine, int track_index,
                                   EngineSamplerSource* anchor_sampler, int* out_anchor_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count) return false;
    return engine_track_overlap_edit(engine, &engine->tracks[track_index], anchor_sampler, out_anchor_index, true);
}

// Resolves overlap on exclusively owned candidate clips without publishing an intermediate revision.
bool engine_track_prepare_no_overlap(Engine* engine, EngineTrack* candidate, EngineSamplerSource* anchor_sampler) {
    return engine_track_overlap_edit(engine, candidate, anchor_sampler, NULL, false);
}
