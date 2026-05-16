#include "input/timeline/timeline_clip_helpers.h"

#include <stdlib.h>
#include <string.h>

static void timeline_copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

uint64_t timeline_clip_frame_count(const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    if (clip->duration_frames > 0) {
        return clip->duration_frames;
    }
    if (clip->sampler) {
        return engine_sampler_get_frame_count(clip->sampler);
    }
    if (clip->media && clip->media->frame_count > 0) {
        return clip->media->frame_count;
    }
    return 0;
}

uint64_t timeline_clip_midi_content_end_frame(const EngineClip* clip) {
    if (!clip || engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return 0;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    int note_count = engine_clip_midi_note_count(clip);
    uint64_t end_frame = 0;
    for (int i = 0; notes && i < note_count; ++i) {
        uint64_t note_end = UINT64_MAX;
        if (UINT64_MAX - notes[i].start_frame >= notes[i].duration_frames) {
            note_end = notes[i].start_frame + notes[i].duration_frames;
        }
        if (note_end > end_frame) {
            end_frame = note_end;
        }
    }
    return end_frame;
}

uint64_t timeline_clip_midi_min_duration_frames(const EngineClip* clip) {
    uint64_t min_duration = timeline_clip_midi_content_end_frame(clip);
    return min_duration > 0 ? min_duration : 1u;
}

bool timeline_clip_is_timeline_region(const EngineClip* clip) {
    return clip && clip->active && timeline_clip_frame_count(clip) > 0;
}

void timeline_session_clip_clear(SessionClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->automation_lanes) {
        for (int l = 0; l < clip->automation_lane_count; ++l) {
            free(clip->automation_lanes[l].points);
            clip->automation_lanes[l].points = NULL;
            clip->automation_lanes[l].point_count = 0;
        }
        free(clip->automation_lanes);
        clip->automation_lanes = NULL;
    }
    clip->automation_lane_count = 0;
    free(clip->midi_notes);
    clip->midi_notes = NULL;
    clip->midi_note_count = 0;
}

bool timeline_session_clip_from_engine(const EngineClip* clip, SessionClip* out_clip) {
    if (!clip || !out_clip) {
        return false;
    }
    memset(out_clip, 0, sizeof(*out_clip));
    out_clip->kind = engine_clip_get_kind(clip);
    timeline_copy_string(out_clip->name, sizeof(out_clip->name), clip->name);
    timeline_copy_string(out_clip->media_id, sizeof(out_clip->media_id), engine_clip_get_media_id(clip));
    timeline_copy_string(out_clip->media_path, sizeof(out_clip->media_path), engine_clip_get_media_path(clip));
    out_clip->start_frame = clip->timeline_start_frames;
    out_clip->duration_frames = timeline_clip_frame_count(clip);
    out_clip->offset_frames = clip->offset_frames;
    out_clip->fade_in_frames = clip->fade_in_frames;
    out_clip->fade_out_frames = clip->fade_out_frames;
    out_clip->fade_in_curve = clip->fade_in_curve;
    out_clip->fade_out_curve = clip->fade_out_curve;
    out_clip->gain = clip->gain;
    out_clip->selected = false;
    out_clip->instrument_preset = engine_clip_midi_instrument_preset(clip);
    out_clip->instrument_params = engine_clip_midi_instrument_params(clip);
    out_clip->instrument_inherits_track = engine_clip_midi_inherits_track_instrument(clip);

    if (clip->automation_lane_count > 0 && clip->automation_lanes) {
        out_clip->automation_lanes = (SessionAutomationLane*)calloc((size_t)clip->automation_lane_count,
                                                                    sizeof(SessionAutomationLane));
        if (!out_clip->automation_lanes) {
            return false;
        }
        out_clip->automation_lane_count = clip->automation_lane_count;
        for (int l = 0; l < clip->automation_lane_count; ++l) {
            const EngineAutomationLane* src = &clip->automation_lanes[l];
            SessionAutomationLane* dst = &out_clip->automation_lanes[l];
            dst->target = src->target;
            dst->point_count = src->point_count;
            if (src->point_count > 0 && src->points) {
                dst->points = (SessionAutomationPoint*)calloc((size_t)src->point_count,
                                                              sizeof(SessionAutomationPoint));
                if (!dst->points) {
                    timeline_session_clip_clear(out_clip);
                    return false;
                }
                for (int p = 0; p < src->point_count; ++p) {
                    dst->points[p].frame = src->points[p].frame;
                    dst->points[p].value = src->points[p].value;
                }
            }
        }
    }

    if (engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI) {
        int note_count = engine_clip_midi_note_count(clip);
        if (note_count > 0) {
            const EngineMidiNote* notes = engine_clip_midi_notes(clip);
            if (!notes) {
                timeline_session_clip_clear(out_clip);
                return false;
            }
            out_clip->midi_notes = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
            if (!out_clip->midi_notes) {
                timeline_session_clip_clear(out_clip);
                return false;
            }
            memcpy(out_clip->midi_notes, notes, sizeof(EngineMidiNote) * (size_t)note_count);
            out_clip->midi_note_count = note_count;
        }
    }
    return true;
}
