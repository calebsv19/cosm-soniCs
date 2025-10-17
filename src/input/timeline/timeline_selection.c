#include "input/timeline_selection.h"

#include "input/inspector_input.h"
#include "input/timeline_drag.h"

#include <stddef.h>

bool timeline_selection_contains(const AppState* state, int track_index, int clip_index, int* out_index) {
    if (!state) {
        return false;
    }
    for (int i = 0; i < state->selection_count; ++i) {
        if (state->selection[i].track_index == track_index && state->selection[i].clip_index == clip_index) {
            if (out_index) {
                *out_index = i;
            }
            return true;
        }
    }
    return false;
}

void timeline_selection_clear(AppState* state) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    inspector_input_init(state);
    state->timeline_drop_track_index = 0;
}

void timeline_selection_add(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    if (clip_index < 0) {
        state->selected_track_index = track_index;
        state->selected_clip_index = -1;
        state->active_track_index = track_index;
        state->timeline_drop_track_index = track_index;
        state->selection_count = 0;
        return;
    }
    if (timeline_selection_contains(state, track_index, clip_index, NULL)) {
        state->selected_track_index = track_index;
        state->selected_clip_index = clip_index;
        return;
    }
    if (state->selection_count >= TIMELINE_MAX_SELECTION) {
        return;
    }
    state->selection[state->selection_count].track_index = track_index;
    state->selection[state->selection_count].clip_index = clip_index;
    state->selection_count++;
    state->selected_track_index = track_index;
    state->selected_clip_index = clip_index;
    state->active_track_index = track_index;
    state->timeline_drop_track_index = track_index;
}

void timeline_selection_remove(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    int index = -1;
    if (!timeline_selection_contains(state, track_index, clip_index, &index)) {
        return;
    }
    for (int i = index; i < state->selection_count - 1; ++i) {
        state->selection[i] = state->selection[i + 1];
    }
    if (state->selection_count > 0) {
        state->selection_count--;
    }
    if (state->selection_count <= 0) {
        timeline_selection_clear(state);
    } else {
        TimelineSelectionEntry last = state->selection[state->selection_count - 1];
        state->selected_track_index = last.track_index;
        state->selected_clip_index = last.clip_index;
    }
}

void timeline_selection_set_single(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
    timeline_selection_add(state, track_index, clip_index);
}

void timeline_selection_update_index(AppState* state, int track_index, int old_clip_index, int new_clip_index) {
    if (!state) {
        return;
    }
    for (int i = 0; i < state->selection_count; ++i) {
        if (state->selection[i].track_index == track_index && state->selection[i].clip_index == old_clip_index) {
            state->selection[i].clip_index = new_clip_index;
        }
    }
    if (state->selected_track_index == track_index && state->selected_clip_index == old_clip_index) {
        state->selected_clip_index = new_clip_index;
    }
}

void timeline_selection_delete(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (state->selection_count <= 0) {
        if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
            engine_remove_clip(state->engine, state->selected_track_index, state->selected_clip_index);
            timeline_selection_clear(state);
        }
        return;
    }
    for (int i = state->selection_count - 1; i >= 0; --i) {
        int track_index = state->selection[i].track_index;
        int clip_index = state->selection[i].clip_index;
        if (track_index >= 0 && clip_index >= 0) {
            if (engine_remove_clip(state->engine, track_index, clip_index)) {
                for (int j = 0; j < i; ++j) {
                    if (state->selection[j].track_index == track_index &&
                        state->selection[j].clip_index > clip_index) {
                        state->selection[j].clip_index -= 1;
                    }
                }
            }
        }
    }
    timeline_selection_clear(state);
}
