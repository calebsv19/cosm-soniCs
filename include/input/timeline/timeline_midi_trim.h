#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine/engine.h"

typedef struct {
    uint64_t timeline_start_frames;
    uint64_t duration_frames;
    EngineMidiNote* notes;
    int note_count;
} TimelineMidiLeftTrimResult;

void timeline_midi_left_trim_result_clear(TimelineMidiLeftTrimResult* result);
bool timeline_midi_left_trim_build(const EngineClip* clip,
                                   uint64_t new_start_frames,
                                   TimelineMidiLeftTrimResult* out_result);
bool timeline_midi_left_trim_apply(Engine* engine,
                                   int track_index,
                                   int* inout_clip_index,
                                   uint64_t new_start_frames);
bool timeline_midi_left_trim_apply_from_notes(Engine* engine,
                                              int track_index,
                                              int* inout_clip_index,
                                              uint64_t initial_start_frames,
                                              uint64_t initial_duration_frames,
                                              const EngineMidiNote* initial_notes,
                                              int initial_note_count,
                                              uint64_t new_start_frames);
