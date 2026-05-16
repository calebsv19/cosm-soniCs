#include "input/midi_editor_input_internal.h"

#include "app_state.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

bool midi_editor_note_values_match(EngineMidiNote a, EngineMidiNote b) {
    return a.start_frame == b.start_frame &&
           a.duration_frames == b.duration_frames &&
           a.note == b.note &&
           a.velocity == b.velocity;
}

void midi_editor_select_matching_notes_after_set(AppState* state,
                                                        const MidiEditorSelection* selection,
                                                        const EngineMidiNote* selected_notes,
                                                        int selected_count) {
    if (!state || !selection || !selection->clip || !selected_notes || selected_count <= 0) {
        return;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    int note_count = engine_clip_midi_note_count(selection->clip);
    if (!notes || note_count <= 0) {
        midi_editor_select_note(state, selection, -1);
        return;
    }
    bool consumed[ENGINE_MIDI_NOTE_CAP] = {false};
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    midi_editor_clear_note_selection(state);
    int bounded_note_count = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    for (int i = 0; i < selected_count; ++i) {
        for (int j = 0; j < bounded_note_count; ++j) {
            if (!consumed[j] && midi_editor_note_values_match(notes[j], selected_notes[i])) {
                consumed[j] = true;
                state->midi_editor_ui.selected_note_indices[j] = true;
                if (state->midi_editor_ui.selected_note_index < 0) {
                    state->midi_editor_ui.selected_note_index = j;
                }
                break;
            }
        }
    }
}

bool midi_editor_collect_selected_pattern(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 EngineMidiNote* out_notes,
                                                 int out_capacity,
                                                 int* out_count,
                                                 uint64_t* out_span_frames,
                                                 uint64_t* out_min_start,
                                                 uint64_t* out_max_end) {
    if (!state || !selection || !selection->clip || !out_notes || !out_count ||
        !out_span_frames || out_capacity <= 0) {
        return false;
    }
    *out_count = 0;
    *out_span_frames = 0;
    if (out_min_start) {
        *out_min_start = 0;
    }
    if (out_max_end) {
        *out_max_end = 0;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    uint64_t min_start = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    uint64_t max_end = 0;
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        uint64_t end = notes[i].start_frame + notes[i].duration_frames;
        if (notes[i].start_frame < min_start) {
            min_start = notes[i].start_frame;
        }
        if (end > max_end) {
            max_end = end;
        }
        ++selected_count;
    }
    if (selected_count <= 0 || selected_count > out_capacity || max_end <= min_start) {
        return false;
    }
    int out = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = notes[i];
        note.start_frame -= min_start;
        out_notes[out++] = note;
    }
    *out_count = out;
    *out_span_frames = max_end - min_start;
    if (out_min_start) {
        *out_min_start = min_start;
    }
    if (out_max_end) {
        *out_max_end = max_end;
    }
    return out > 0;
}

bool midi_editor_copy_selected_notes(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int count = 0;
    uint64_t span = 0;
    if (!midi_editor_collect_selected_pattern(state,
                                              &selection,
                                              state->midi_editor_ui.clipboard_notes,
                                              ENGINE_MIDI_NOTE_CAP,
                                              &count,
                                              &span,
                                              NULL,
                                              NULL)) {
        state->midi_editor_ui.clipboard_note_count = 0;
        state->midi_editor_ui.clipboard_span_frames = 0;
        return false;
    }
    state->midi_editor_ui.clipboard_note_count = count;
    state->midi_editor_ui.clipboard_span_frames = span;
    state->midi_editor_ui.instrument_menu_open = false;
    return true;
}

bool midi_editor_selected_insert_frame(AppState* state,
                                              const MidiEditorSelection* selection,
                                              uint64_t pattern_span,
                                              uint64_t* out_frame) {
    if (!state || !selection || !selection->clip || !out_frame || pattern_span == 0) {
        return false;
    }
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    if (pattern_span > clip_frames) {
        return false;
    }
    uint64_t relative = 0;
    if (midi_editor_transport_relative_frame(state, selection->clip, false, &relative) &&
        relative + pattern_span <= clip_frames) {
        *out_frame = relative;
        return true;
    }

    EngineMidiNote* scratch = (EngineMidiNote*)calloc((size_t)ENGINE_MIDI_NOTE_CAP, sizeof(EngineMidiNote));
    if (!scratch) {
        return false;
    }
    int count = 0;
    uint64_t span = 0;
    uint64_t max_end = 0;
    bool has_selection = midi_editor_collect_selected_pattern(state,
                                                              selection,
                                                              scratch,
                                                              ENGINE_MIDI_NOTE_CAP,
                                                              &count,
                                                              &span,
                                                              NULL,
                                                              &max_end);
    free(scratch);
    if (has_selection && max_end + pattern_span <= clip_frames) {
        *out_frame = max_end;
        return true;
    }
    *out_frame = 0;
    return pattern_span <= clip_frames;
}

bool midi_editor_insert_pattern(AppState* state,
                                       const MidiEditorSelection* selection,
                                       const EngineMidiNote* pattern,
                                       int pattern_count,
                                       uint64_t pattern_span,
                                       uint64_t insert_frame) {
    if (!state || !selection || !selection->clip || !pattern || pattern_count <= 0 ||
        pattern_count > ENGINE_MIDI_NOTE_CAP || pattern_span == 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (note_count < 0 || note_count > ENGINE_MIDI_NOTE_CAP ||
        note_count + pattern_count > ENGINE_MIDI_NOTE_CAP ||
        (note_count > 0 && !notes)) {
        return false;
    }
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    if (pattern_span > clip_frames || insert_frame + pattern_span > clip_frames) {
        return false;
    }
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection->clip, &before_notes, &before_count)) {
        return false;
    }
    int replacement_count = note_count + pattern_count;
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)replacement_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)pattern_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    if (note_count > 0) {
        memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    }
    for (int i = 0; i < pattern_count; ++i) {
        EngineMidiNote note = pattern[i];
        note.start_frame += insert_frame;
        if (note.duration_frames == 0 || note.start_frame + note.duration_frames > clip_frames) {
            free(replacement);
            free(selected_after);
            midi_editor_free_notes(&before_notes, &before_count);
            return false;
        }
        replacement[note_count + i] = note;
        selected_after[i] = note;
    }

    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection->track_index,
                                         selection->clip_index,
                                         replacement,
                                         replacement_count);
    if (ok) {
        MidiEditorSelection fresh = *selection;
        (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
        midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, pattern_count);
        midi_editor_clear_hover_note(state);
        state->midi_editor_ui.instrument_menu_open = false;
        ok = midi_editor_push_note_undo(state, selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    return ok;
}

bool midi_editor_paste_notes(AppState* state) {
    if (!state || state->midi_editor_ui.clipboard_note_count <= 0 ||
        state->midi_editor_ui.clipboard_note_count > ENGINE_MIDI_NOTE_CAP ||
        state->midi_editor_ui.clipboard_span_frames == 0) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    uint64_t insert_frame = 0;
    if (!midi_editor_selected_insert_frame(state,
                                           &selection,
                                           state->midi_editor_ui.clipboard_span_frames,
                                           &insert_frame)) {
        return false;
    }
    return midi_editor_insert_pattern(state,
                                      &selection,
                                      state->midi_editor_ui.clipboard_notes,
                                      state->midi_editor_ui.clipboard_note_count,
                                      state->midi_editor_ui.clipboard_span_frames,
                                      insert_frame);
}

bool midi_editor_duplicate_selected_notes(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    EngineMidiNote* pattern = (EngineMidiNote*)calloc((size_t)ENGINE_MIDI_NOTE_CAP, sizeof(EngineMidiNote));
    if (!pattern) {
        return false;
    }
    int pattern_count = 0;
    uint64_t pattern_span = 0;
    uint64_t max_end = 0;
    bool collected = midi_editor_collect_selected_pattern(state,
                                                          &selection,
                                                          pattern,
                                                          ENGINE_MIDI_NOTE_CAP,
                                                          &pattern_count,
                                                          &pattern_span,
                                                          NULL,
                                                          &max_end);
    bool ok = false;
    if (collected) {
        ok = midi_editor_insert_pattern(state,
                                        &selection,
                                        pattern,
                                        pattern_count,
                                        pattern_span,
                                        max_end);
    }
    free(pattern);
    return ok;
}

bool midi_editor_quantize_selected_note(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (selected_mask[i]) {
            ++selected_count;
        }
    }
    if (selected_count <= 0) {
        return false;
    }

    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }

    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)selected_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    int selected_after_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = replacement[i];
        uint64_t start = midi_editor_quantize_relative_frame(state, selection.clip, note.start_frame, true);
        uint64_t end = midi_editor_quantize_relative_frame(state,
                                                           selection.clip,
                                                           note.start_frame + note.duration_frames,
                                                           true);
        if (end <= start) {
            end = start + midi_editor_quantize_step_frames_at(state, selection.clip, start);
        }
        if (end > clip_frames) {
            end = clip_frames;
        }
        if (end <= start) {
            start = clip_frames > min_duration ? clip_frames - min_duration : 0;
            end = clip_frames;
        }
        note.start_frame = start;
        note.duration_frames = end > start ? end - start : min_duration;
        if (note.duration_frames < min_duration) {
            note.duration_frames = min_duration;
        }
        if (note.start_frame + note.duration_frames > clip_frames) {
            note.duration_frames = clip_frames > note.start_frame ? clip_frames - note.start_frame : min_duration;
        }
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }

    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection.track_index,
                                         selection.clip_index,
                                         replacement,
                                         note_count);
    if (ok) {
        MidiEditorSelection fresh = selection;
        (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
        midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, selected_after_count);
        ok = midi_editor_push_note_undo(state, &selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    state->midi_editor_ui.instrument_menu_open = false;
    return ok;
}

bool midi_editor_delete_selected_note(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (selected_mask[i]) {
            ++selected_count;
        }
    }
    if (selected_count <= 0) {
        return false;
    }
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }
    int replacement_count = note_count - selected_count;
    EngineMidiNote* replacement = replacement_count > 0
        ? (EngineMidiNote*)calloc((size_t)replacement_count, sizeof(EngineMidiNote))
        : NULL;
    if (replacement_count > 0 && !replacement) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    int out = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            replacement[out++] = notes[i];
        }
    }
    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection.track_index,
                                         selection.clip_index,
                                         replacement,
                                         replacement_count);
    if (ok) {
        midi_editor_clear_note_selection(state);
        midi_editor_clear_hover_note(state);
        ok = midi_editor_push_note_undo(state, &selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    return ok;
}

static int midi_editor_selected_note_count_from_mask(const bool* selected_mask, int note_count) {
    if (!selected_mask || note_count <= 0) {
        return 0;
    }
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (selected_mask[i]) {
            ++selected_count;
        }
    }
    return selected_count;
}

static uint8_t midi_editor_clamp_midi_note_int(int note) {
    if (note < ENGINE_MIDI_NOTE_MIN) {
        return ENGINE_MIDI_NOTE_MIN;
    }
    if (note > ENGINE_MIDI_NOTE_MAX) {
        return ENGINE_MIDI_NOTE_MAX;
    }
    return (uint8_t)note;
}

static bool midi_editor_commit_selected_note_replacement(AppState* state,
                                                         const MidiEditorSelection* selection,
                                                         EngineMidiNote* before_notes,
                                                         int before_count,
                                                         EngineMidiNote* replacement,
                                                         int note_count,
                                                         const EngineMidiNote* selected_after,
                                                         int selected_after_count) {
    if (!state || !selection || !selection->clip || !before_notes || !replacement ||
        note_count <= 0 || selected_after_count <= 0 || !selected_after) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection->track_index,
                                         selection->clip_index,
                                         replacement,
                                         note_count);
    if (ok) {
        MidiEditorSelection fresh = *selection;
        (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
        midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, selected_after_count);
        midi_editor_clear_hover_note(state);
        state->midi_editor_ui.instrument_menu_open = false;
        ok = midi_editor_push_note_undo(state, selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    return ok;
}

bool midi_editor_transpose_selected_notes(AppState* state, int semitones) {
    if (!state || semitones == 0) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = midi_editor_selected_note_count_from_mask(selected_mask, note_count);
    if (selected_count <= 0) {
        return false;
    }
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)selected_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    bool changed = false;
    int selected_after_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = replacement[i];
        uint8_t transposed = midi_editor_clamp_midi_note_int((int)note.note + semitones);
        if (transposed != note.note) {
            changed = true;
        }
        note.note = transposed;
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }
    bool ok = false;
    if (changed) {
        ok = midi_editor_commit_selected_note_replacement(state,
                                                         &selection,
                                                         before_notes,
                                                         before_count,
                                                         replacement,
                                                         note_count,
                                                         selected_after,
                                                         selected_after_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    return ok;
}

bool midi_editor_nudge_selected_notes(AppState* state, int direction) {
    if (!state || direction == 0) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = midi_editor_selected_note_count_from_mask(selected_mask, note_count);
    if (selected_count <= 0) {
        return false;
    }

    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint64_t min_start = clip_frames;
    uint64_t max_end = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        uint64_t end = notes[i].start_frame + notes[i].duration_frames;
        if (notes[i].start_frame < min_start) {
            min_start = notes[i].start_frame;
        }
        if (end > max_end) {
            max_end = end;
        }
    }
    uint64_t step = midi_editor_quantize_step_frames_at(state, selection.clip, min_start);
    if (step == 0) {
        return false;
    }
    int64_t delta = 0;
    if (direction < 0) {
        uint64_t bounded = min_start < step ? min_start : step;
        delta = -(int64_t)bounded;
    } else {
        uint64_t remaining = max_end < clip_frames ? clip_frames - max_end : 0;
        uint64_t bounded = remaining < step ? remaining : step;
        delta = (int64_t)bounded;
    }
    if (delta == 0) {
        return false;
    }

    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)selected_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    int selected_after_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = replacement[i];
        note.start_frame = delta < 0
            ? note.start_frame - (uint64_t)(-delta)
            : note.start_frame + (uint64_t)delta;
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }
    bool ok = midi_editor_commit_selected_note_replacement(state,
                                                           &selection,
                                                           before_notes,
                                                           before_count,
                                                           replacement,
                                                           note_count,
                                                           selected_after,
                                                           selected_after_count);
    free(replacement);
    free(selected_after);
    return ok;
}

bool midi_editor_resize_selected_note_durations(AppState* state, int direction) {
    if (!state || direction == 0) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = midi_editor_selected_note_count_from_mask(selected_mask, note_count);
    if (selected_count <= 0) {
        return false;
    }
    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    uint64_t step = midi_editor_quantize_step_frames_at(state, selection.clip, 0);
    if (step == 0) {
        return false;
    }

    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)selected_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    bool changed = false;
    int selected_after_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = replacement[i];
        uint64_t duration = note.duration_frames;
        if (direction < 0) {
            duration = duration > step ? duration - step : min_duration;
            if (duration < min_duration) {
                duration = min_duration;
            }
        } else {
            uint64_t max_duration = note.start_frame < clip_frames ? clip_frames - note.start_frame : min_duration;
            duration = duration + step;
            if (duration > max_duration) {
                duration = max_duration;
            }
            if (duration < min_duration) {
                duration = min_duration;
            }
        }
        if (duration != note.duration_frames) {
            changed = true;
        }
        note.duration_frames = duration;
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }
    bool ok = false;
    if (changed) {
        ok = midi_editor_commit_selected_note_replacement(state,
                                                         &selection,
                                                         before_notes,
                                                         before_count,
                                                         replacement,
                                                         note_count,
                                                         selected_after,
                                                         selected_after_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    return ok;
}

bool midi_editor_command_modifier_active(const SDL_Event* event) {
    SDL_Keymod mods = SDL_GetModState();
    if (event) {
        mods = (SDL_Keymod)(mods | event->key.keysym.mod);
    }
    return (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
}

bool midi_editor_handle_clipboard_keydown(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN || !midi_editor_command_modifier_active(event)) {
        return false;
    }
    switch (event->key.keysym.sym) {
    case SDLK_c:
        return midi_editor_copy_selected_notes(state);
    case SDLK_v:
        return midi_editor_paste_notes(state);
    case SDLK_d:
        return midi_editor_duplicate_selected_notes(state);
    default:
        break;
    }
    return false;
}

bool midi_editor_handle_note_command_keydown(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN) {
        return false;
    }
    SDL_Keymod mods = SDL_GetModState();
    mods = (SDL_Keymod)(mods | event->key.keysym.mod);
    bool command = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
    bool alt = (mods & KMOD_ALT) != 0;
    bool shift = (mods & KMOD_SHIFT) != 0;
    SDL_Keycode key = event->key.keysym.sym;

    if (alt && !command && (key == SDLK_UP || key == SDLK_DOWN)) {
        int direction = key == SDLK_UP ? 1 : -1;
        return midi_editor_transpose_selected_notes(state, direction * (shift ? 12 : 1));
    }
    if (command && !alt && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
        int direction = key == SDLK_RIGHT ? 1 : -1;
        if (shift) {
            return midi_editor_resize_selected_note_durations(state, direction);
        }
        return midi_editor_nudge_selected_notes(state, direction);
    }
    return false;
}
