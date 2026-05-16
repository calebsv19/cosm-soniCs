#include "input/midi_editor_input_internal.h"

#include "app_state.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#define MIDI_EDITOR_VELOCITY_DRAG_PIXELS 120.0f

void midi_editor_select_note(AppState* state,
                                    const MidiEditorSelection* selection,
                                    int note_index) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.selected_note_index = note_index;
    memset(state->midi_editor_ui.selected_note_indices,
           0,
           sizeof(state->midi_editor_ui.selected_note_indices));
    if (note_index >= 0 && note_index < ENGINE_MIDI_NOTE_CAP) {
        state->midi_editor_ui.selected_note_indices[note_index] = true;
    }
    state->midi_editor_ui.hover_note_valid = note_index >= 0;
    state->midi_editor_ui.hover_track_index = selection->track_index;
    state->midi_editor_ui.hover_clip_index = selection->clip_index;
    state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.hover_note_index = note_index;
}

void midi_editor_toggle_note_selection(AppState* state,
                                              const MidiEditorSelection* selection,
                                              int note_index) {
    if (!state || !selection || !selection->clip ||
        note_index < 0 || note_index >= ENGINE_MIDI_NOTE_CAP) {
        return;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    if (note_index >= note_count) {
        return;
    }
    if (!midi_editor_selection_matches_ui(state, selection)) {
        state->midi_editor_ui.selected_track_index = selection->track_index;
        state->midi_editor_ui.selected_clip_index = selection->clip_index;
        state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
        midi_editor_clear_note_selection(state);
    } else if (midi_editor_effective_selected_note_count(state, selection, note_count) == 1 &&
               state->midi_editor_ui.selected_note_index >= 0 &&
               state->midi_editor_ui.selected_note_index < ENGINE_MIDI_NOTE_CAP &&
               !state->midi_editor_ui.selected_note_indices[state->midi_editor_ui.selected_note_index]) {
        state->midi_editor_ui.selected_note_indices[state->midi_editor_ui.selected_note_index] = true;
    }

    state->midi_editor_ui.selected_note_indices[note_index] =
        !state->midi_editor_ui.selected_note_indices[note_index];
    if (state->midi_editor_ui.selected_note_indices[note_index]) {
        state->midi_editor_ui.selected_note_index = note_index;
    } else if (state->midi_editor_ui.selected_note_index == note_index) {
        midi_editor_focus_first_selected_note(state, note_count);
    }
    if (midi_editor_effective_selected_note_count(state, selection, note_count) == 0) {
        state->midi_editor_ui.selected_note_index = -1;
    }
    state->midi_editor_ui.hover_note_valid = note_index >= 0;
    state->midi_editor_ui.hover_track_index = selection->track_index;
    state->midi_editor_ui.hover_clip_index = selection->clip_index;
    state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.hover_note_index = note_index;
}

bool midi_editor_note_is_selected(const AppState* state,
                                         const MidiEditorSelection* selection,
                                         int note_index) {
    return midi_editor_note_index_selected(state, selection, note_index);
}

void midi_editor_clear_hover_note(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.hover_note_valid = false;
    state->midi_editor_ui.hover_note_index = -1;
}

void midi_editor_update_hover_note(AppState* state,
                                          const MidiEditorSelection* selection,
                                          const MidiEditorLayout* layout,
                                          int x,
                                          int y) {
    if (!state || !selection || !layout) {
        return;
    }
    MidiEditorNoteHit hit = {0};
    if (midi_editor_hit_test_note(layout, selection->clip, x, y, &hit)) {
        state->midi_editor_ui.hover_note_valid = true;
        state->midi_editor_ui.hover_track_index = selection->track_index;
        state->midi_editor_ui.hover_clip_index = selection->clip_index;
        state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
        state->midi_editor_ui.hover_note_index = hit.note_index;
        return;
    }
    midi_editor_clear_hover_note(state);
}

SDL_Rect midi_editor_rect_from_points(int x0, int y0, int x1, int y1) {
    SDL_Rect rect = {x0, y0, x1 - x0, y1 - y0};
    if (rect.w < 0) {
        rect.x += rect.w;
        rect.w = -rect.w;
    }
    if (rect.h < 0) {
        rect.y += rect.h;
        rect.h = -rect.h;
    }
    return rect;
}

void midi_editor_update_marquee_preview(AppState* state,
                                               const MidiEditorSelection* selection,
                                               const MidiEditorLayout* layout,
                                               int x,
                                               int y) {
    if (!state || !selection || !selection->clip || !layout ||
        !state->midi_editor_ui.marquee_active) {
        return;
    }
    state->midi_editor_ui.marquee_current_x = x;
    state->midi_editor_ui.marquee_current_y = y;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
    SDL_Rect marquee = midi_editor_rect_from_points(state->midi_editor_ui.marquee_start_x,
                                                    state->midi_editor_ui.marquee_start_y,
                                                    x,
                                                    y);
    if (marquee.w < 2 && marquee.h < 2) {
        return;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    int note_count = engine_clip_midi_note_count(selection->clip);
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    for (int i = 0; notes && i < note_count && i < ENGINE_MIDI_NOTE_CAP; ++i) {
        SDL_Rect note_rect = {0, 0, 0, 0};
        if (midi_editor_note_rect(layout, &notes[i], clip_frames, &note_rect) &&
            SDL_HasIntersection(&marquee, &note_rect)) {
            state->midi_editor_ui.marquee_preview_note_indices[i] = true;
        }
    }
}

bool midi_editor_begin_marquee(AppState* state,
                                      const MidiEditorSelection* selection,
                                      int x,
                                      int y,
                                      bool additive) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.marquee_active = true;
    state->midi_editor_ui.marquee_additive = additive;
    state->midi_editor_ui.marquee_start_x = x;
    state->midi_editor_ui.marquee_start_y = y;
    state->midi_editor_ui.marquee_current_x = x;
    state->midi_editor_ui.marquee_current_y = y;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
    return true;
}

bool midi_editor_commit_marquee(AppState* state) {
    if (!state || !state->midi_editor_ui.marquee_active) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout) ||
        !midi_editor_selection_matches_ui(state, &selection)) {
        midi_editor_clear_marquee_state(state);
        return false;
    }
    midi_editor_update_marquee_preview(state,
                                       &selection,
                                       &layout,
                                       state->midi_editor_ui.marquee_current_x,
                                       state->midi_editor_ui.marquee_current_y);
    int note_count = engine_clip_midi_note_count(selection.clip);
    if (note_count > ENGINE_MIDI_NOTE_CAP) {
        note_count = ENGINE_MIDI_NOTE_CAP;
    }
    if (!state->midi_editor_ui.marquee_additive) {
        midi_editor_clear_note_selection(state);
    }
    for (int i = 0; i < note_count; ++i) {
        if (state->midi_editor_ui.marquee_preview_note_indices[i]) {
            state->midi_editor_ui.selected_note_indices[i] = true;
        }
    }
    midi_editor_focus_first_selected_note(state, note_count);
    midi_editor_clear_marquee_state(state);
    return true;
}

bool midi_editor_set_drag_state(AppState* state,
                                       const MidiEditorSelection* selection,
                                       MidiEditorDragMode mode,
                                       int note_index,
                                       int x,
                                       int y,
                                       EngineMidiNote note) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.drag_active = true;
    state->midi_editor_ui.drag_mode = mode;
    state->midi_editor_ui.drag_track_index = selection->track_index;
    state->midi_editor_ui.drag_clip_index = selection->clip_index;
    state->midi_editor_ui.drag_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.drag_note_index = note_index;
    state->midi_editor_ui.drag_original_note = note;
    state->midi_editor_ui.drag_start_x = x;
    state->midi_editor_ui.drag_start_y = y;
    state->midi_editor_ui.drag_anchor_frame = note.start_frame;
    state->midi_editor_ui.drag_anchor_note = note.note;
    state->midi_editor_ui.drag_anchor_velocity = note.velocity;
    state->midi_editor_ui.drag_velocity_group = false;
    midi_editor_clear_move_group_drag(state);
    memset(state->midi_editor_ui.drag_velocity_original_values,
           0,
           sizeof(state->midi_editor_ui.drag_velocity_original_values));
    if (mode == MIDI_EDITOR_DRAG_VELOCITY) {
        int note_count = engine_clip_midi_note_count(selection->clip);
        const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
        if (notes && note_count > 0 && note_count <= ENGINE_MIDI_NOTE_CAP &&
            midi_editor_note_index_selected(state, selection, note_index)) {
            bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
            midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
            for (int i = 0; i < note_count; ++i) {
                state->midi_editor_ui.drag_velocity_original_values[i] = notes[i].velocity;
                if (selected_mask[i]) {
                    state->midi_editor_ui.drag_velocity_group = true;
                }
            }
        } else if (notes && note_index >= 0 && note_index < note_count && note_index < ENGINE_MIDI_NOTE_CAP) {
            state->midi_editor_ui.drag_velocity_original_values[note_index] = notes[note_index].velocity;
        }
    } else if (mode == MIDI_EDITOR_DRAG_MOVE) {
        (void)midi_editor_configure_move_group_drag(state, selection, note_index);
    }
    state->midi_editor_ui.drag_mutated = (mode == MIDI_EDITOR_DRAG_CREATE);
    return true;
}

bool midi_editor_begin_drag(AppState* state,
                                   const MidiEditorSelection* selection,
                                   MidiEditorDragMode mode,
                                   int note_index,
                                   int x,
                                   int y,
                                   EngineMidiNote note) {
    if (!midi_editor_begin_note_undo(state, selection)) {
        return false;
    }
    return midi_editor_set_drag_state(state, selection, mode, note_index, x, y, note);
}

bool midi_editor_note_equal(EngineMidiNote a, EngineMidiNote b) {
    return a.start_frame == b.start_frame &&
           a.duration_frames == b.duration_frames &&
           a.note == b.note &&
           a.velocity == b.velocity;
}

void midi_editor_clear_move_group_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.drag_move_group = false;
    state->midi_editor_ui.drag_move_original_note_count = 0;
    memset(state->midi_editor_ui.drag_move_selected_indices,
           0,
           sizeof(state->midi_editor_ui.drag_move_selected_indices));
    memset(state->midi_editor_ui.drag_move_original_notes,
           0,
           sizeof(state->midi_editor_ui.drag_move_original_notes));
}

bool midi_editor_configure_move_group_drag(AppState* state,
                                                  const MidiEditorSelection* selection,
                                                  int note_index) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP ||
        !midi_editor_note_index_selected(state, selection, note_index) ||
        midi_editor_effective_selected_note_count(state, selection, note_count) <= 1) {
        return false;
    }
    midi_editor_clear_move_group_drag(state);
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    memcpy(state->midi_editor_ui.drag_move_original_notes,
           notes,
           sizeof(EngineMidiNote) * (size_t)note_count);
    state->midi_editor_ui.drag_move_original_note_count = note_count;
    bool any_selected = false;
    for (int i = 0; i < note_count; ++i) {
        state->midi_editor_ui.drag_move_selected_indices[i] = selected_mask[i];
        if (selected_mask[i]) {
            any_selected = true;
        }
    }
    state->midi_editor_ui.drag_move_group = any_selected;
    return any_selected;
}

bool midi_editor_update_velocity_group_drag(AppState* state,
                                                   const MidiEditorSelection* selection,
                                                   int mouse_y) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    float delta = (float)(state->midi_editor_ui.drag_start_y - mouse_y) / MIDI_EDITOR_VELOCITY_DRAG_PIXELS;
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    if (!replacement) {
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    bool changed = false;
    float anchor_velocity = state->midi_editor_ui.drag_anchor_velocity;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        float original = state->midi_editor_ui.drag_velocity_original_values[i];
        float velocity = midi_editor_clamp_velocity(original + delta);
        if (replacement[i].velocity != velocity) {
            changed = true;
        }
        replacement[i].velocity = velocity;
        if (i == state->midi_editor_ui.drag_note_index) {
            anchor_velocity = velocity;
        }
    }
    bool ok = true;
    if (changed) {
        ok = engine_clip_midi_set_notes(state->engine,
                                        selection->track_index,
                                        selection->clip_index,
                                        replacement,
                                        note_count);
        if (ok) {
            state->midi_editor_ui.drag_mutated = true;
        }
    }
    midi_editor_set_default_velocity(state, anchor_velocity);
    free(replacement);
    return ok;
}

bool midi_editor_update_move_group_drag(AppState* state,
                                               const MidiEditorSelection* selection,
                                               uint64_t pointer_frame,
                                               uint8_t pointer_note,
                                               uint64_t clip_frames) {
    if (!state || !selection || !selection->clip ||
        !state->midi_editor_ui.drag_move_group ||
        state->midi_editor_ui.drag_move_original_note_count <= 0 ||
        state->midi_editor_ui.drag_move_original_note_count > ENGINE_MIDI_NOTE_CAP ||
        clip_frames == 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* current_notes = engine_clip_midi_notes(selection->clip);
    int original_count = state->midi_editor_ui.drag_move_original_note_count;
    if (!current_notes || note_count != original_count) {
        return false;
    }

    const EngineMidiNote* original_notes = state->midi_editor_ui.drag_move_original_notes;
    int64_t raw_delta = 0;
    if (pointer_frame >= state->midi_editor_ui.drag_anchor_frame) {
        raw_delta = (int64_t)(pointer_frame - state->midi_editor_ui.drag_anchor_frame);
    } else {
        raw_delta = -(int64_t)(state->midi_editor_ui.drag_anchor_frame - pointer_frame);
    }
    uint64_t anchor_start = state->midi_editor_ui.drag_original_note.start_frame;
    uint64_t raw_anchor_start = anchor_start;
    if (raw_delta >= 0) {
        uint64_t delta = (uint64_t)raw_delta;
        raw_anchor_start = anchor_start + delta;
        if (raw_anchor_start > clip_frames) {
            raw_anchor_start = clip_frames;
        }
    } else {
        uint64_t delta = (uint64_t)(-raw_delta);
        raw_anchor_start = delta > anchor_start ? 0 : anchor_start - delta;
    }
    uint64_t snapped_anchor_start = midi_editor_snap_relative_frame(state, selection->clip, raw_anchor_start);
    int64_t frame_delta = 0;
    if (snapped_anchor_start >= anchor_start) {
        frame_delta = (int64_t)(snapped_anchor_start - anchor_start);
    } else {
        frame_delta = -(int64_t)(anchor_start - snapped_anchor_start);
    }

    uint64_t min_start = clip_frames;
    uint64_t max_end = 0;
    int min_note = ENGINE_MIDI_NOTE_MAX;
    int max_note = ENGINE_MIDI_NOTE_MIN;
    for (int i = 0; i < original_count; ++i) {
        if (!state->midi_editor_ui.drag_move_selected_indices[i]) {
            continue;
        }
        if (original_notes[i].start_frame < min_start) {
            min_start = original_notes[i].start_frame;
        }
        uint64_t end = original_notes[i].start_frame + original_notes[i].duration_frames;
        if (end > max_end) {
            max_end = end;
        }
        if ((int)original_notes[i].note < min_note) {
            min_note = (int)original_notes[i].note;
        }
        if ((int)original_notes[i].note > max_note) {
            max_note = (int)original_notes[i].note;
        }
    }
    if (min_start == clip_frames && max_end == 0) {
        return false;
    }
    if (frame_delta < 0 && (uint64_t)(-frame_delta) > min_start) {
        frame_delta = -(int64_t)min_start;
    }
    if (frame_delta > 0 && max_end + (uint64_t)frame_delta > clip_frames) {
        frame_delta = clip_frames > max_end ? (int64_t)(clip_frames - max_end) : 0;
    }

    int pitch_delta = (int)pointer_note - (int)state->midi_editor_ui.drag_anchor_note;
    if (min_note + pitch_delta < ENGINE_MIDI_NOTE_MIN) {
        pitch_delta = ENGINE_MIDI_NOTE_MIN - min_note;
    }
    if (max_note + pitch_delta > ENGINE_MIDI_NOTE_MAX) {
        pitch_delta = ENGINE_MIDI_NOTE_MAX - max_note;
    }

    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)original_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)original_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        return false;
    }
    memcpy(replacement, original_notes, sizeof(EngineMidiNote) * (size_t)original_count);
    int selected_after_count = 0;
    bool changed = false;
    for (int i = 0; i < original_count; ++i) {
        if (!state->midi_editor_ui.drag_move_selected_indices[i]) {
            continue;
        }
        EngineMidiNote note = original_notes[i];
        if (frame_delta >= 0) {
            note.start_frame += (uint64_t)frame_delta;
        } else {
            note.start_frame -= (uint64_t)(-frame_delta);
        }
        int moved_note = (int)note.note + pitch_delta;
        if (moved_note < ENGINE_MIDI_NOTE_MIN) moved_note = ENGINE_MIDI_NOTE_MIN;
        if (moved_note > ENGINE_MIDI_NOTE_MAX) moved_note = ENGINE_MIDI_NOTE_MAX;
        note.note = (uint8_t)moved_note;
        if (!midi_editor_note_equal(note, original_notes[i])) {
            changed = true;
        }
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }

    bool ok = true;
    if (changed) {
        ok = engine_clip_midi_set_notes(state->engine,
                                        selection->track_index,
                                        selection->clip_index,
                                        replacement,
                                        original_count);
        if (ok) {
            MidiEditorSelection fresh = *selection;
            (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
            midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, selected_after_count);
            state->midi_editor_ui.drag_mutated = true;
        }
    }
    free(replacement);
    free(selected_after);
    return ok;
}

bool midi_editor_update_drag(AppState* state, int mouse_x, int mouse_y) {
    if (!state || !state->midi_editor_ui.drag_active) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout) ||
        selection.track_index != state->midi_editor_ui.drag_track_index ||
        selection.clip_index != state->midi_editor_ui.drag_clip_index ||
        selection.clip->creation_index != state->midi_editor_ui.drag_clip_creation_index) {
        return false;
    }

    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint8_t pointer_note = state->midi_editor_ui.drag_anchor_note;
    uint64_t pointer_frame = state->midi_editor_ui.drag_anchor_frame;
    if (!midi_editor_point_to_note_frame(&layout, clip_frames, mouse_x, mouse_y, &pointer_note, &pointer_frame)) {
        int clamped_x = mouse_x;
        if (clamped_x < layout.grid_rect.x) {
            clamped_x = layout.grid_rect.x;
        }
        if (clamped_x > layout.grid_rect.x + layout.grid_rect.w) {
            clamped_x = layout.grid_rect.x + layout.grid_rect.w;
        }
        int clamped_y = mouse_y;
        if (clamped_y < layout.grid_rect.y) {
            clamped_y = layout.grid_rect.y;
        }
        if (clamped_y >= layout.grid_rect.y + layout.grid_rect.h) {
            clamped_y = layout.grid_rect.y + layout.grid_rect.h - 1;
        }
        if (!midi_editor_point_to_note_frame(&layout, clip_frames, clamped_x, clamped_y, &pointer_note, &pointer_frame)) {
            return false;
        }
    }
    pointer_frame = midi_editor_snap_relative_frame(state, selection.clip, pointer_frame);

    EngineMidiNote note = state->midi_editor_ui.drag_original_note;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    switch (state->midi_editor_ui.drag_mode) {
    case MIDI_EDITOR_DRAG_VELOCITY: {
        if (state->midi_editor_ui.drag_velocity_group) {
            return midi_editor_update_velocity_group_drag(state, &selection, mouse_y);
        }
        float delta = (float)(state->midi_editor_ui.drag_start_y - mouse_y) / MIDI_EDITOR_VELOCITY_DRAG_PIXELS;
        note.velocity = midi_editor_clamp_velocity(state->midi_editor_ui.drag_anchor_velocity + delta);
        midi_editor_set_default_velocity(state, note.velocity);
        break;
    }
    case MIDI_EDITOR_DRAG_CREATE:
    case MIDI_EDITOR_DRAG_RESIZE_RIGHT: {
        uint64_t end_frame = pointer_frame;
        if (end_frame < note.start_frame + min_duration) {
            end_frame = note.start_frame + min_duration;
        }
        if (end_frame > clip_frames) {
            end_frame = clip_frames;
        }
        note.duration_frames = end_frame > note.start_frame ? end_frame - note.start_frame : min_duration;
        break;
    }
    case MIDI_EDITOR_DRAG_RESIZE_LEFT: {
        uint64_t end_frame = note.start_frame + note.duration_frames;
        uint64_t new_start = pointer_frame;
        if (new_start + min_duration > end_frame) {
            new_start = end_frame > min_duration ? end_frame - min_duration : 0;
        }
        note.start_frame = new_start;
        note.duration_frames = end_frame > note.start_frame ? end_frame - note.start_frame : min_duration;
        break;
    }
    case MIDI_EDITOR_DRAG_MOVE: {
        if (state->midi_editor_ui.drag_move_group) {
            return midi_editor_update_move_group_drag(state,
                                                      &selection,
                                                      pointer_frame,
                                                      pointer_note,
                                                      clip_frames);
        }
        uint64_t start_pointer = state->midi_editor_ui.drag_anchor_frame;
        uint64_t raw_start = note.start_frame;
        if (pointer_frame >= start_pointer) {
            uint64_t delta = pointer_frame - start_pointer;
            raw_start = note.start_frame + delta;
        } else {
            uint64_t delta = start_pointer - pointer_frame;
            raw_start = delta > note.start_frame ? 0 : note.start_frame - delta;
        }
        if (raw_start + note.duration_frames > clip_frames) {
            raw_start = clip_frames > note.duration_frames ? clip_frames - note.duration_frames : 0;
        }
        note.start_frame = midi_editor_snap_relative_frame(state, selection.clip, raw_start);
        if (note.start_frame + note.duration_frames > clip_frames) {
            note.start_frame = clip_frames > note.duration_frames ? clip_frames - note.duration_frames : 0;
        }
        int pitch_delta = (int)pointer_note - (int)state->midi_editor_ui.drag_anchor_note;
        int moved_note = (int)note.note + pitch_delta;
        if (moved_note < ENGINE_MIDI_NOTE_MIN) moved_note = ENGINE_MIDI_NOTE_MIN;
        if (moved_note > ENGINE_MIDI_NOTE_MAX) moved_note = ENGINE_MIDI_NOTE_MAX;
        note.note = (uint8_t)moved_note;
        break;
    }
    case MIDI_EDITOR_DRAG_NONE:
    default:
        return false;
    }

    if (note.start_frame + note.duration_frames > clip_frames) {
        note.duration_frames = clip_frames > note.start_frame ? clip_frames - note.start_frame : min_duration;
    }
    if (note.duration_frames < min_duration) {
        note.duration_frames = min_duration;
    }
    if (midi_editor_note_equal(note, state->midi_editor_ui.drag_original_note) &&
        !state->midi_editor_ui.drag_mutated) {
        return true;
    }
    int new_index = -1;
    if (!engine_clip_midi_update_note(state->engine,
                                      selection.track_index,
                                      selection.clip_index,
                                      state->midi_editor_ui.drag_note_index,
                                      note,
                                      &new_index)) {
        return false;
    }
    if (new_index >= 0) {
        state->midi_editor_ui.drag_note_index = new_index;
        midi_editor_select_note(state, &selection, new_index);
    }
    state->midi_editor_ui.drag_mutated = true;
    return true;
}

void midi_editor_end_drag(AppState* state) {
    if (!state || !state->midi_editor_ui.drag_active) {
        return;
    }
    if (state->midi_editor_ui.drag_mutated) {
        midi_editor_commit_note_undo(state);
    } else if (state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_MIDI_NOTE_EDIT) {
        undo_manager_cancel_drag(&state->undo);
    }
    state->midi_editor_ui.drag_active = false;
    state->midi_editor_ui.drag_mode = MIDI_EDITOR_DRAG_NONE;
    state->midi_editor_ui.drag_note_index = -1;
    state->midi_editor_ui.drag_mutated = false;
    midi_editor_clear_move_group_drag(state);
}

void midi_editor_clear_shift_note_pending(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.shift_note_pending = false;
    state->midi_editor_ui.shift_note_pending_track_index = -1;
    state->midi_editor_ui.shift_note_pending_clip_index = -1;
    state->midi_editor_ui.shift_note_pending_clip_creation_index = 0;
    state->midi_editor_ui.shift_note_pending_note_index = -1;
    state->midi_editor_ui.shift_note_pending_x = 0;
    state->midi_editor_ui.shift_note_pending_y = 0;
}

bool midi_editor_begin_shift_note_pending(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 int note_index,
                                                 int x,
                                                 int y) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.shift_note_pending = true;
    state->midi_editor_ui.shift_note_pending_track_index = selection->track_index;
    state->midi_editor_ui.shift_note_pending_clip_index = selection->clip_index;
    state->midi_editor_ui.shift_note_pending_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.shift_note_pending_note_index = note_index;
    state->midi_editor_ui.shift_note_pending_x = x;
    state->midi_editor_ui.shift_note_pending_y = y;
    return true;
}

bool midi_editor_shift_note_pending_matches(const AppState* state,
                                                   const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.shift_note_pending &&
           state->midi_editor_ui.shift_note_pending_track_index == selection->track_index &&
           state->midi_editor_ui.shift_note_pending_clip_index == selection->clip_index &&
           state->midi_editor_ui.shift_note_pending_clip_creation_index == selection->clip->creation_index;
}

bool midi_editor_update_shift_note_pending(AppState* state, int x, int y) {
    if (!state || !state->midi_editor_ui.shift_note_pending) {
        return false;
    }
    int dx = abs(x - state->midi_editor_ui.shift_note_pending_x);
    int dy = abs(y - state->midi_editor_ui.shift_note_pending_y);
    if (dx < 3 && dy < 3) {
        return true;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL) ||
        !midi_editor_shift_note_pending_matches(state, &selection)) {
        midi_editor_clear_shift_note_pending(state);
        return false;
    }
    int note_index = state->midi_editor_ui.shift_note_pending_note_index;
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_index < 0 || note_index >= note_count) {
        midi_editor_clear_shift_note_pending(state);
        return false;
    }
    if (!midi_editor_note_index_selected(state, &selection, note_index)) {
        midi_editor_select_note(state, &selection, note_index);
    }
    int start_x = state->midi_editor_ui.shift_note_pending_x;
    int start_y = state->midi_editor_ui.shift_note_pending_y;
    midi_editor_clear_shift_note_pending(state);
    return midi_editor_begin_drag(state,
                                  &selection,
                                  MIDI_EDITOR_DRAG_VELOCITY,
                                  note_index,
                                  start_x,
                                  start_y,
                                  notes[note_index]) &&
           midi_editor_update_drag(state, x, y);
}

bool midi_editor_commit_shift_note_pending(AppState* state) {
    if (!state || !state->midi_editor_ui.shift_note_pending) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (midi_editor_get_fresh_selection(state, &selection, NULL) &&
        midi_editor_shift_note_pending_matches(state, &selection)) {
        midi_editor_toggle_note_selection(state,
                                          &selection,
                                          state->midi_editor_ui.shift_note_pending_note_index);
    }
    midi_editor_clear_shift_note_pending(state);
    return true;
}

bool midi_editor_begin_note_press_pending(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 int note_index,
                                                 MidiEditorNoteHitPart part,
                                                 int x,
                                                 int y,
                                                 bool group_candidate) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.note_press_pending = true;
    state->midi_editor_ui.note_press_pending_track_index = selection->track_index;
    state->midi_editor_ui.note_press_pending_clip_index = selection->clip_index;
    state->midi_editor_ui.note_press_pending_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.note_press_pending_note_index = note_index;
    state->midi_editor_ui.note_press_pending_x = x;
    state->midi_editor_ui.note_press_pending_y = y;
    state->midi_editor_ui.note_press_pending_part = (int)part;
    state->midi_editor_ui.note_press_group_candidate = group_candidate;
    return true;
}

bool midi_editor_note_press_pending_matches(const AppState* state,
                                                   const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.note_press_pending &&
           state->midi_editor_ui.note_press_pending_track_index == selection->track_index &&
           state->midi_editor_ui.note_press_pending_clip_index == selection->clip_index &&
           state->midi_editor_ui.note_press_pending_clip_creation_index == selection->clip->creation_index;
}

MidiEditorDragMode midi_editor_drag_mode_for_hit_part(MidiEditorNoteHitPart part) {
    if (part == MIDI_EDITOR_NOTE_HIT_LEFT_EDGE) {
        return MIDI_EDITOR_DRAG_RESIZE_LEFT;
    }
    if (part == MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE) {
        return MIDI_EDITOR_DRAG_RESIZE_RIGHT;
    }
    return MIDI_EDITOR_DRAG_MOVE;
}

bool midi_editor_update_note_press_pending(AppState* state, int x, int y) {
    if (!state || !state->midi_editor_ui.note_press_pending) {
        return false;
    }
    int dx = abs(x - state->midi_editor_ui.note_press_pending_x);
    int dy = abs(y - state->midi_editor_ui.note_press_pending_y);
    if (dx < 3 && dy < 3) {
        return true;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL) ||
        !midi_editor_note_press_pending_matches(state, &selection)) {
        midi_editor_clear_note_press_pending(state);
        return false;
    }
    int note_index = state->midi_editor_ui.note_press_pending_note_index;
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_index < 0 || note_index >= note_count) {
        midi_editor_clear_note_press_pending(state);
        return false;
    }
    MidiEditorNoteHitPart part = (MidiEditorNoteHitPart)state->midi_editor_ui.note_press_pending_part;
    bool group_move = state->midi_editor_ui.note_press_group_candidate &&
                      part == MIDI_EDITOR_NOTE_HIT_BODY &&
                      midi_editor_note_index_selected(state, &selection, note_index) &&
                      midi_editor_effective_selected_note_count(state, &selection, note_count) > 1;
    MidiEditorDragMode mode = group_move
        ? MIDI_EDITOR_DRAG_MOVE
        : midi_editor_drag_mode_for_hit_part(part);
    int start_x = state->midi_editor_ui.note_press_pending_x;
    int start_y = state->midi_editor_ui.note_press_pending_y;
    midi_editor_clear_note_press_pending(state);
    if (!group_move) {
        midi_editor_select_note(state, &selection, note_index);
    }
    return midi_editor_begin_drag(state, &selection, mode, note_index, start_x, start_y, notes[note_index]) &&
           midi_editor_update_drag(state, x, y);
}

bool midi_editor_commit_note_press_pending(AppState* state) {
    if (!state || !state->midi_editor_ui.note_press_pending) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (midi_editor_get_fresh_selection(state, &selection, NULL) &&
        midi_editor_note_press_pending_matches(state, &selection)) {
        midi_editor_select_note(state,
                                &selection,
                                state->midi_editor_ui.note_press_pending_note_index);
    }
    midi_editor_clear_note_press_pending(state);
    return true;
}
