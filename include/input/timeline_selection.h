#pragma once

#include <stdbool.h>

#include "app_state.h"

bool timeline_selection_contains(const AppState* state, int track_index, int clip_index, int* out_index);
void timeline_selection_clear(AppState* state);
void timeline_selection_add(AppState* state, int track_index, int clip_index);
void timeline_selection_remove(AppState* state, int track_index, int clip_index);
void timeline_selection_set_single(AppState* state, int track_index, int clip_index);
void timeline_selection_set_primary(AppState* state, int track_index, int clip_index);
void timeline_selection_set_selected_clip(AppState* state, int track_index, int clip_index);
void timeline_selection_set_track_focus(AppState* state, int track_index);
void timeline_selection_restore_clear(AppState* state);
void timeline_selection_restore_clear_entries(AppState* state);
void timeline_selection_restore_primary(AppState* state,
                                        int selected_track_index,
                                        int selected_clip_index,
                                        int active_track_index);
bool timeline_selection_restore_append_entry(AppState* state, int track_index, int clip_index);
// Remaps all selection indices after one clip moves within a sorted track; not a batch-reorder API.
void timeline_selection_update_index(AppState* state, int track_index, int old_clip_index, int new_clip_index);
void timeline_selection_delete(AppState* state);

// Duplicates the complete selection atomically with one undo entry.
bool timeline_selection_duplicate(AppState* state);
