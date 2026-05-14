#pragma once

#include "time/tempo.h"

#include <stdbool.h>
#include <stdint.h>

// Describes a half-open frame range on the DAW transport timeline.
typedef struct {
    uint64_t start_frame;
    uint64_t duration_frames;
} DawTimelineFrameRange;

// Classifies how an existing clip range intersects the active edit range.
typedef enum {
    DAW_TIMELINE_OVERLAP_NONE = 0,
    DAW_TIMELINE_OVERLAP_REMOVE,
    DAW_TIMELINE_OVERLAP_TRIM_END,
    DAW_TIMELINE_OVERLAP_SHIFT_START,
    DAW_TIMELINE_OVERLAP_SPLIT
} DawTimelineOverlapKind;

// Stores the normalized mutation lengths for resolving a clip overlap.
typedef struct {
    DawTimelineOverlapKind kind;
    uint64_t existing_end_frame;
    uint64_t anchor_end_frame;
    uint64_t left_duration_frames;
    uint64_t right_start_frame;
    uint64_t right_duration_frames;
    uint64_t source_offset_delta_frames;
} DawTimelineOverlapPlan;

// Builds a transport frame range from a start and duration.
DawTimelineFrameRange daw_timeline_frame_range(uint64_t start_frame, uint64_t duration_frames);

// Returns true when a frame range has playable duration.
bool daw_timeline_frame_range_is_valid(DawTimelineFrameRange range);

// Returns the exclusive end frame for a range, saturating on overflow.
uint64_t daw_timeline_frame_range_end(DawTimelineFrameRange range);

// Returns true when two half-open frame ranges intersect.
bool daw_timeline_frame_ranges_overlap(DawTimelineFrameRange a, DawTimelineFrameRange b);

// Computes the structural overlap mutation for an existing range against an active edit range.
DawTimelineOverlapPlan daw_timeline_analyze_overlap(DawTimelineFrameRange existing,
                                                    DawTimelineFrameRange anchor);

// Converts seconds to the nearest transport frame at a sample rate.
uint64_t daw_timeline_frames_from_seconds(double seconds, int sample_rate);

// Converts transport frames to seconds at a sample rate.
double daw_timeline_seconds_from_frames(uint64_t frames, int sample_rate);

// Converts beats to the nearest transport frame through a tempo map.
uint64_t daw_timeline_frames_from_beats(const TempoMap* tempo_map, double beats);

// Converts transport frames to beats through a tempo map.
double daw_timeline_beats_from_frames(const TempoMap* tempo_map, uint64_t frames);
