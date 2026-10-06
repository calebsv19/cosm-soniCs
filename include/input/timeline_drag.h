#pragma once

#include <stdint.h>

#include "app_state.h"

struct EngineSamplerSource;

int timeline_move_clip_to_track(AppState* state, int src_track, int clip_index, int dst_track, uint64_t start_frame);
bool timeline_find_clip_by_sampler(const AppState* state, struct EngineSamplerSource* sampler, int* out_track, int* out_clip);

// Publishes the complete live multi-clip move or slip preview and preserves selection on rejection.
bool timeline_apply_compound_preview(AppState* state, int64_t delta_frames, bool slip);

// Applies the complete cross-track placement, including required track growth, in one publication.
bool timeline_apply_compound_drop(AppState* state, int64_t delta_frames, int track_offset);

// Publishes audio trim bounds and timeline position together, updating selection only after acceptance.
bool timeline_apply_audio_trim(AppState* state, uint64_t start, uint64_t offset, uint64_t duration);
