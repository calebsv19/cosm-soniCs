#pragma once

#include <stdbool.h>
#include <stdint.h>

struct AppState;

uint64_t timeline_midi_region_default_duration_frames(const struct AppState* state, uint64_t start_frame);
bool timeline_midi_region_create_on_active_track(struct AppState* state,
                                                 int* out_track_index,
                                                 int* out_clip_index);
