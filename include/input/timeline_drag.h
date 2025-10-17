#pragma once

#include <stdint.h>

#include "app_state.h"

struct EngineSamplerSource;

int timeline_resolve_overlapping_clips(AppState* state, int track_index, struct EngineSamplerSource* new_sampler);
int timeline_move_clip_to_track(AppState* state, int src_track, int clip_index, int dst_track, uint64_t start_frame);
bool timeline_find_clip_by_sampler(const AppState* state, struct EngineSamplerSource* sampler, int* out_track, int* out_clip);
