#include "engine/engine_clips_automation_internal.h"
#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "engine/sampler.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

// Releases only a clip's owned automation lanes, leaving its other resources intact.
static void clip_automation_release(EngineClip* clip) {
    for (int i = 0; i < clip->automation_lane_count; ++i)
        engine_automation_lane_free(&clip->automation_lanes[i]);
    free(clip->automation_lanes);
    clip->automation_lanes = NULL;
    clip->automation_lane_count = clip->automation_lane_capacity = 0;
}

// Copies all lanes before replacing destination ownership; aliased inputs remain valid while copying.
bool engine_clip_set_automation_lanes_internal(EngineClip* clip,
                                               const EngineAutomationLane* lanes,
                                               int lane_count) {
    if (!clip || lane_count < 0 || (lane_count > 0 && !lanes)) return false;
    EngineClip prepared = {0};
    if (lane_count > 0) {
        prepared.automation_lanes = calloc((size_t)lane_count, sizeof(*lanes));
        if (!prepared.automation_lanes) return false;
        prepared.automation_lane_count = prepared.automation_lane_capacity = lane_count;
        for (int i = 0; i < lane_count; ++i) {
            if (!engine_automation_lane_copy(&lanes[i], &prepared.automation_lanes[i])) {
                clip_automation_release(&prepared);
                return false;
            }
        }
    }
    clip_automation_release(clip);
    clip->automation_lanes = prepared.automation_lanes;
    clip->automation_lane_count = clip->automation_lane_capacity = lane_count;
    return true;
}

// Initializes the default volume lane without discarding existing data on allocation failure.
void engine_clip_init_automation(EngineClip* clip) {
    EngineAutomationLane lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME};
    (void)engine_clip_set_automation_lanes_internal(clip, &lane, 1);
}

// Deep-copies automation and retains the destination when any allocation fails.
bool engine_clip_copy_automation(const EngineClip* src, EngineClip* dst) {
    return src && engine_clip_set_automation_lanes_internal(dst, src->automation_lanes, src->automation_lane_count);
}

// Returns an independently owned, complete snapshot or empty outputs on failure.
bool engine_clip_snapshot_automation(const EngineClip* clip, EngineAutomationLane** out_lanes, int* out_lane_count) {
    if (!out_lanes || !out_lane_count) return false;
    *out_lanes = NULL;
    *out_lane_count = 0;
    EngineClip copy = {0};
    if (clip && !engine_clip_copy_automation(clip, &copy)) return false;
    *out_lanes = copy.automation_lanes;
    *out_lane_count = copy.automation_lane_count;
    return true;
}

// Looks up an existing control-owned clip without implicitly creating tracks.
static EngineClip* automation_edit_clip(Engine* engine, int track, int clip) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track < 0 || track >= engine->track_count ||
        clip < 0 || clip >= engine->tracks[track].clip_count) return NULL;
    return &engine->tracks[track].clips[clip];
}

// Rejects unknown targets and instrument automation on audio clips.
static bool automation_target_valid(const EngineClip* clip, EngineAutomationTarget target) {
    return target >= 0 && target < ENGINE_AUTOMATION_TARGET_COUNT &&
        (!engine_automation_target_is_instrument_param(target) || clip->kind == ENGINE_CLIP_KIND_MIDI);
}

// Finds or creates a lane in an unpublished candidate without a second publication.
static EngineAutomationLane* automation_candidate_lane(EngineClip* clip, EngineAutomationTarget target, bool create) {
    if (!automation_target_valid(clip, target)) return NULL;
    for (int i = 0; i < clip->automation_lane_count; ++i)
        if (clip->automation_lanes[i].target == target) return &clip->automation_lanes[i];
    if (!create || clip->automation_lane_count == INT_MAX) return NULL;
    int count = clip->automation_lane_count + 1;
    EngineAutomationLane* lanes = realloc(clip->automation_lanes, sizeof(*lanes) * (size_t)count);
    if (!lanes) return NULL;
    clip->automation_lanes = lanes;
    clip->automation_lane_count = clip->automation_lane_capacity = count;
    engine_automation_lane_init(&lanes[count - 1], target);
    return &lanes[count - 1];
}

// Publishes complete replacement automation and sources, retaining old ownership on every failure.
static bool automation_candidate_commit(Engine* engine, int track_index, int clip_index,
                                        EngineClip* clip, EngineClip* candidate) {
    candidate->sampler = NULL;
    candidate->instrument = NULL;
    if (clip->sampler) {
        candidate->sampler = engine_sampler_source_clone(clip->sampler);
        if (!candidate->sampler || !engine_sampler_source_set_automation(candidate->sampler,
                candidate->automation_lanes, candidate->automation_lane_count)) goto fail;
    }
    if (clip->instrument) {
        candidate->instrument = engine_instrument_source_create();
        const EngineAutomationLane* track_lanes = NULL;
        int track_lane_count = 0;
        if (engine_clip_midi_inherits_track_instrument(clip))
            engine_track_midi_get_instrument_automation_lanes(engine, track_index, &track_lanes, &track_lane_count);
        if (!candidate->instrument || !engine_instrument_source_set_midi_clip(candidate->instrument,
                clip->timeline_start_frames, clip->duration_frames,
                engine_clip_midi_effective_instrument_preset(engine, track_index, clip_index),
                engine_clip_midi_effective_instrument_params(engine, track_index, clip_index),
                clip->midi_notes.notes, clip->midi_notes.note_count, track_lanes, track_lane_count,
                candidate->automation_lanes, candidate->automation_lane_count)) goto fail;
    }
    EngineClip previous = *clip;
    *clip = *candidate;
    if (!engine_request_rebuild_sources(engine)) {
        *clip = previous;
        goto fail;
    }
    engine_sampler_source_destroy(previous.sampler);
    engine_instrument_source_destroy(previous.instrument);
    clip_automation_release(&previous);
    return true;
fail:
    engine_sampler_source_destroy(candidate->sampler);
    engine_instrument_source_destroy(candidate->instrument);
    clip_automation_release(candidate);
    return false;
}

// Starts a metadata candidate with independently owned automation and borrowed remaining fields.
static bool automation_candidate_begin(const EngineClip* clip, EngineClip* candidate) {
    *candidate = *clip;
    candidate->automation_lanes = NULL;
    candidate->automation_lane_count = candidate->automation_lane_capacity = 0;
    return engine_clip_copy_automation(clip, candidate);
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


// Ensures a lane exists, exposing a borrowed lane only after successful publication.
bool engine_clip_ensure_automation_lane(Engine* engine, int track_index, int clip_index,
                                        EngineAutomationTarget target, EngineAutomationLane** out_lane) {
    if (out_lane) *out_lane = NULL;
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip) return false;
    EngineAutomationLane* existing = automation_candidate_lane(clip, target, false);
    if (existing) {
        if (out_lane) *out_lane = existing;
        return true;
    }
    EngineClip candidate;
    if (!automation_candidate_begin(clip, &candidate)) return false;
    if (!automation_candidate_lane(&candidate, target, true)) {
        clip_automation_release(&candidate);
        return false;
    }
    if (!automation_candidate_commit(engine, track_index, clip_index, clip, &candidate)) return false;
    if (out_lane) *out_lane = automation_candidate_lane(clip, target, false);
    return true;
}

// Inserts a point in independent storage and returns its index only after publication.
bool engine_clip_add_automation_point(Engine* engine, int track_index, int clip_index,
                                      EngineAutomationTarget target, uint64_t frame, float value, int* out_index) {
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip || !isfinite(value)) return false;
    EngineClip candidate;
    if (!automation_candidate_begin(clip, &candidate)) return false;
    int result = 0;
    EngineAutomationLane* lane = automation_candidate_lane(&candidate, target, true);
    if (!lane || !engine_automation_lane_insert_point(lane, frame, value, &result)) {
        clip_automation_release(&candidate);
        return false;
    }
    if (!automation_candidate_commit(engine, track_index, clip_index, clip, &candidate)) return false;
    if (out_index) *out_index = result;
    return true;
}

// Updates and sorts a point in independent storage before publishing the edit.
bool engine_clip_update_automation_point(Engine* engine, int track_index, int clip_index,
                                      EngineAutomationTarget target, int point_index, uint64_t frame, float value, int* out_index) {
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip || !isfinite(value)) return false;
    EngineClip candidate;
    if (!automation_candidate_begin(clip, &candidate)) return false;
    int result = 0;
    EngineAutomationLane* lane = automation_candidate_lane(&candidate, target, false);
    if (!lane || !engine_automation_lane_update_point(lane, point_index, frame, value, &result)) {
        clip_automation_release(&candidate);
        return false;
    }
    if (!automation_candidate_commit(engine, track_index, clip_index, clip, &candidate)) return false;
    if (out_index) *out_index = result;
    return true;
}

// Removes a point without changing the original lane until the replacement is accepted.
bool engine_clip_remove_automation_point(Engine* engine, int track_index, int clip_index,
                                      EngineAutomationTarget target, int point_index) {
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip) return false;
    EngineClip candidate;
    if (!automation_candidate_begin(clip, &candidate)) return false;
    EngineAutomationLane* lane = automation_candidate_lane(&candidate, target, false);
    if (!lane || !engine_automation_lane_remove_point(lane, point_index)) {
        clip_automation_release(&candidate);
        return false;
    }
    if (!automation_candidate_commit(engine, track_index, clip_index, clip, &candidate)) return false;
    return true;
}

// Replaces a lane with validated points and publishes the entire edit once.
bool engine_clip_set_automation_lane_points(Engine* engine, int track_index, int clip_index,
                                      EngineAutomationTarget target, const EngineAutomationPoint* points, int count) {
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip || count < 0 || (count > 0 && !points)) return false;
    for (int i = 0; i < count; ++i) if (!isfinite(points[i].value)) return false;
    EngineClip candidate;
    if (!automation_candidate_begin(clip, &candidate)) return false;
    EngineAutomationLane* lane = automation_candidate_lane(&candidate, target, true);
    if (!lane || !engine_automation_lane_set_points(lane, points, count)) {
        clip_automation_release(&candidate);
        return false;
    }
    if (!automation_candidate_commit(engine, track_index, clip_index, clip, &candidate)) return false;
    return true;
}

// Replaces all automation lanes only after independent sources and the render revision are ready.
bool engine_clip_set_automation_lanes(Engine* engine, int track_index, int clip_index,
                                      const EngineAutomationLane* lanes, int lane_count) {
    EngineClip* clip = automation_edit_clip(engine, track_index, clip_index);
    if (!clip || lane_count < 0 || (lane_count > 0 && !lanes)) return false;
    for (int i = 0; i < lane_count; ++i) {
        if (!automation_target_valid(clip, lanes[i].target) || lanes[i].point_count < 0 ||
            (lanes[i].point_count > 0 && !lanes[i].points)) return false;
        for (int j = 0; j < lanes[i].point_count; ++j)
            if (!isfinite(lanes[i].points[j].value)) return false;
    }
    EngineClip candidate = *clip;
    candidate.automation_lanes = NULL;
    candidate.automation_lane_count = candidate.automation_lane_capacity = 0;
    if (!engine_clip_set_automation_lanes_internal(&candidate, lanes, lane_count)) return false;
    return automation_candidate_commit(engine, track_index, clip_index, clip, &candidate);
}
