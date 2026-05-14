#include "engine/timeline_contract.h"

#include <math.h>
#include <stdint.h>

// Builds a transport frame range from a start and duration.
DawTimelineFrameRange daw_timeline_frame_range(uint64_t start_frame, uint64_t duration_frames) {
    DawTimelineFrameRange range = {
        .start_frame = start_frame,
        .duration_frames = duration_frames
    };
    return range;
}

// Returns true when a frame range has playable duration.
bool daw_timeline_frame_range_is_valid(DawTimelineFrameRange range) {
    return range.duration_frames > 0;
}

// Returns the exclusive end frame for a range, saturating on overflow.
uint64_t daw_timeline_frame_range_end(DawTimelineFrameRange range) {
    if (UINT64_MAX - range.start_frame < range.duration_frames) {
        return UINT64_MAX;
    }
    return range.start_frame + range.duration_frames;
}

// Returns true when two half-open frame ranges intersect.
bool daw_timeline_frame_ranges_overlap(DawTimelineFrameRange a, DawTimelineFrameRange b) {
    if (!daw_timeline_frame_range_is_valid(a) || !daw_timeline_frame_range_is_valid(b)) {
        return false;
    }
    uint64_t a_end = daw_timeline_frame_range_end(a);
    uint64_t b_end = daw_timeline_frame_range_end(b);
    return a.start_frame < b_end && b.start_frame < a_end;
}

// Computes the structural overlap mutation for an existing range against an active edit range.
DawTimelineOverlapPlan daw_timeline_analyze_overlap(DawTimelineFrameRange existing,
                                                    DawTimelineFrameRange anchor) {
    DawTimelineOverlapPlan plan = {0};
    if (!daw_timeline_frame_ranges_overlap(existing, anchor)) {
        plan.kind = DAW_TIMELINE_OVERLAP_NONE;
        return plan;
    }

    uint64_t existing_end = daw_timeline_frame_range_end(existing);
    uint64_t anchor_end = daw_timeline_frame_range_end(anchor);
    bool overlaps_left = existing.start_frame < anchor.start_frame;
    bool overlaps_right = existing_end > anchor_end;

    plan.existing_end_frame = existing_end;
    plan.anchor_end_frame = anchor_end;
    plan.left_duration_frames = overlaps_left ? anchor.start_frame - existing.start_frame : 0;
    plan.right_start_frame = anchor_end;
    plan.right_duration_frames = overlaps_right ? existing_end - anchor_end : 0;
    plan.source_offset_delta_frames = anchor_end > existing.start_frame ? anchor_end - existing.start_frame : 0;

    if (overlaps_left && overlaps_right) {
        plan.kind = DAW_TIMELINE_OVERLAP_SPLIT;
    } else if (overlaps_left) {
        plan.kind = DAW_TIMELINE_OVERLAP_TRIM_END;
    } else if (overlaps_right) {
        plan.kind = DAW_TIMELINE_OVERLAP_SHIFT_START;
    } else {
        plan.kind = DAW_TIMELINE_OVERLAP_REMOVE;
    }
    return plan;
}

// Converts seconds to the nearest transport frame at a sample rate.
uint64_t daw_timeline_frames_from_seconds(double seconds, int sample_rate) {
    if (seconds <= 0.0 || sample_rate <= 0) {
        return 0;
    }
    double frames = seconds * (double)sample_rate;
    if (frames <= 0.0) {
        return 0;
    }
    if (frames >= (double)UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t)llround(frames);
}

// Converts transport frames to seconds at a sample rate.
double daw_timeline_seconds_from_frames(uint64_t frames, int sample_rate) {
    if (sample_rate <= 0) {
        return 0.0;
    }
    return (double)frames / (double)sample_rate;
}

// Converts beats to the nearest transport frame through a tempo map.
uint64_t daw_timeline_frames_from_beats(const TempoMap* tempo_map, double beats) {
    if (beats <= 0.0) {
        return 0;
    }
    double frames = tempo_map_beats_to_samples(tempo_map, beats);
    if (frames <= 0.0) {
        return 0;
    }
    if (frames >= (double)UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t)llround(frames);
}

// Converts transport frames to beats through a tempo map.
double daw_timeline_beats_from_frames(const TempoMap* tempo_map, uint64_t frames) {
    if (frames == 0) {
        return 0.0;
    }
    if (!tempo_map || tempo_map->sample_rate <= 0.0) {
        return 0.0;
    }
    if (frames > (uint64_t)INT64_MAX) {
        frames = (uint64_t)INT64_MAX;
    }
    return tempo_map_samples_to_beats(tempo_map, (int64_t)frames);
}
