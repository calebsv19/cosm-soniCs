#pragma once

#include <stdbool.h>

#include "app_state.h"

bool timeline_selection_contains(const AppState* state, int track_index, int clip_index, int* out_index);
void timeline_selection_clear(AppState* state);
void timeline_selection_add(AppState* state, int track_index, int clip_index);
void timeline_selection_remove(AppState* state, int track_index, int clip_index);
void timeline_selection_set_single(AppState* state, int track_index, int clip_index);
void timeline_selection_update_index(AppState* state, int track_index, int old_clip_index, int new_clip_index);
void timeline_selection_delete(AppState* state);
