#include "input/timeline_selection.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline_drag.h"
#include "undo/undo_manager.h"

#include <stddef.h>
#include <string.h>

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
    state->active_track_index = -1;
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

void timeline_selection_set_primary(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->selected_track_index = track_index;
    state->selected_clip_index = clip_index;
    state->active_track_index = track_index;
    state->timeline_drop_track_index = track_index;
}

void timeline_selection_set_selected_clip(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->selected_track_index = track_index;
    state->selected_clip_index = clip_index;
}

void timeline_selection_set_track_focus(AppState* state, int track_index) {
    if (!state) {
        return;
    }
    state->active_track_index = track_index;
    state->selected_track_index = track_index;
    state->timeline_drop_track_index = track_index;
}

void timeline_selection_restore_clear(AppState* state) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    state->active_track_index = -1;
}

void timeline_selection_restore_clear_entries(AppState* state) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
}

void timeline_selection_restore_primary(AppState* state,
                                        int selected_track_index,
                                        int selected_clip_index,
                                        int active_track_index) {
    if (!state) {
        return;
    }
    state->selected_track_index = selected_track_index;
    state->selected_clip_index = selected_clip_index;
    state->active_track_index = active_track_index;
}

bool timeline_selection_restore_append_entry(AppState* state, int track_index, int clip_index) {
    if (!state || state->selection_count >= TIMELINE_MAX_SELECTION) {
        return false;
    }
    state->selection[state->selection_count].track_index = track_index;
    state->selection[state->selection_count].clip_index = clip_index;
    state->selection_count++;
    return true;
}

// Maps a selected index through one clip's removal and reinsertion in a sorted track.
static int selection_index_after_move(int index, int old_index, int new_index) {
    if (index == old_index) return new_index;
    if (old_index < new_index && index > old_index && index <= new_index) return index - 1;
    if (new_index < old_index && index >= new_index && index < old_index) return index + 1;
    return index;
}

// Preserves all selected clip identities after a single clip is sorted within its track.
void timeline_selection_update_index(AppState* state, int track_index, int old_clip_index, int new_clip_index) {
    if (!state || old_clip_index < 0 || new_clip_index < 0 || old_clip_index == new_clip_index) {
        return;
    }
    for (int i = 0; i < state->selection_count; ++i) {
        if (state->selection[i].track_index == track_index) {
            state->selection[i].clip_index = selection_index_after_move(state->selection[i].clip_index,
                                                                       old_clip_index, new_clip_index);
        }
    }
    if (state->selected_track_index == track_index) {
        state->selected_clip_index = selection_index_after_move(state->selected_clip_index,
                                                               old_clip_index, new_clip_index);
    }
}

// Reserves history before publishing all selected duplicates or removals as one application action.
static bool timeline_selection_edit_content(AppState* state, bool duplicate) {
    if (!state || !state->engine || state->undo.active_drag_valid || state->selection_count < 0 ||
        state->selection_count > TIMELINE_MAX_SELECTION) return false;
    uint64_t identities[TIMELINE_MAX_SELECTION] = {0}, outputs[TIMELINE_MAX_SELECTION] = {0};
    int count = state->selection_count;
    bool fallback = count == 0;
    if (fallback) count = 1;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int anchor = 0;
    for (int i = 0; i < count; ++i) {
        TimelineSelectionEntry entry = fallback ? (TimelineSelectionEntry){state->selected_track_index, state->selected_clip_index} : state->selection[i];
        if (entry.track_index < 0 || entry.track_index >= engine_get_track_count(state->engine) ||
            entry.clip_index < 0 || entry.clip_index >= tracks[entry.track_index].clip_count) return false;
        identities[i] = tracks[entry.track_index].clips[entry.clip_index].creation_index;
        if (entry.track_index == state->selected_track_index && entry.clip_index == state->selected_clip_index) anchor = i;
    }
    uint64_t first = identities[0]; identities[0] = identities[anchor]; identities[anchor] = first;
    UndoCommand command = {.type = UNDO_CMD_CLIP_CONTENT};
    command.data.clip_content_selection = (UndoClipContentSelection){.count = count, .before = identities, .after = outputs};
    if (!undo_manager_begin_drag(&state->undo, &command)) return false;
    UndoCommand* pending = &state->undo.active_drag;
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    uint64_t gap = cfg && cfg->block_size > 0 ? (uint64_t)cfg->block_size : 0;
    if (!engine_clip_content_edit(state->engine, identities, count, duplicate, gap,
        pending->data.clip_content_selection.after, &pending->clip_content_before, &pending->clip_content_after)) {
        undo_manager_cancel_drag(&state->undo); return false;
    }
    memcpy(outputs, pending->data.clip_content_selection.after, (size_t)count * sizeof(*outputs));
    // Begin reserved this exact slot; transfer requires no allocation after engine publication.
    if (!undo_manager_commit_drag(&state->undo, pending)) return false;
    timeline_selection_clear(state);
    tracks = engine_get_tracks(state->engine);
    if (duplicate) for (int i = 0; i < count; ++i)
        for (int t = 0; t < engine_get_track_count(state->engine); ++t)
            for (int c = 0; c < tracks[t].clip_count; ++c)
                if (tracks[t].clips[c].creation_index == outputs[i]) timeline_selection_add(state, t, c);
    if (state->selection_count) {
        timeline_selection_set_primary(state, state->selection[0].track_index, state->selection[0].clip_index);
        inspector_input_set_clip(state, state->selection[0].track_index, state->selection[0].clip_index);
    }
    return true;
}

// Removes the whole selection with one retained-content history entry.
void timeline_selection_delete(AppState* state) {
    timeline_selection_edit_content(state, false);
}

// Duplicates the whole audio/MIDI selection using the existing per-clip end-plus-block placement rule.
bool timeline_selection_duplicate(AppState* state) {
    return timeline_selection_edit_content(state, true);
}
