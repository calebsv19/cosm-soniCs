#include "ui/midi_editor.h"

#include "app_state.h"

static int midi_editor_pitch_clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int midi_editor_pitch_default_top_note(int rows) {
    return rows < 12 ? 60 + rows - 1 : 71;
}

static int midi_editor_pitch_clamp_row_count(int row_count, int row_capacity) {
    int capacity = row_capacity;
    if (capacity < 1) {
        capacity = 1;
    }
    if (capacity > MIDI_EDITOR_VISIBLE_KEY_ROWS) {
        capacity = MIDI_EDITOR_VISIBLE_KEY_ROWS;
    }
    int rows = row_count > 0 ? row_count : capacity;
    if (rows < 1) {
        rows = 1;
    }
    if (rows > capacity) {
        rows = capacity;
    }
    return rows;
}

static int midi_editor_pitch_clamp_top_note(int top_note, int rows) {
    if (rows < 1) {
        rows = 1;
    }
    int min_top = ENGINE_MIDI_NOTE_MIN + rows - 1;
    if (min_top > ENGINE_MIDI_NOTE_MAX) {
        min_top = ENGINE_MIDI_NOTE_MAX;
    }
    return midi_editor_pitch_clamp_int(top_note, min_top, ENGINE_MIDI_NOTE_MAX);
}

static bool midi_editor_pitch_viewport_matches_selection(const AppState* state,
                                                         const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.pitch_viewport_row_count > 0 &&
           state->midi_editor_ui.pitch_viewport_track_index == selection->track_index &&
           state->midi_editor_ui.pitch_viewport_clip_index == selection->clip_index &&
           state->midi_editor_ui.pitch_viewport_clip_creation_index == selection->clip->creation_index;
}

void midi_editor_store_pitch_viewport(AppState* state,
                                      const MidiEditorSelection* selection,
                                      int top_note,
                                      int row_count) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    int rows = midi_editor_pitch_clamp_row_count(row_count, MIDI_EDITOR_VISIBLE_KEY_ROWS);
    int top = midi_editor_pitch_clamp_top_note(top_note, rows);
    state->midi_editor_ui.pitch_viewport_track_index = selection->track_index;
    state->midi_editor_ui.pitch_viewport_clip_index = selection->clip_index;
    state->midi_editor_ui.pitch_viewport_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.pitch_viewport_top_note = top;
    state->midi_editor_ui.pitch_viewport_row_count = rows;
}

void midi_editor_fit_pitch_viewport(AppState* state,
                                    const MidiEditorSelection* selection,
                                    int row_count) {
    int rows = midi_editor_pitch_clamp_row_count(row_count, MIDI_EDITOR_VISIBLE_KEY_ROWS);
    midi_editor_store_pitch_viewport(state, selection, midi_editor_pitch_default_top_note(rows), rows);
}

bool midi_editor_resolve_pitch_viewport(const AppState* state,
                                        const MidiEditorSelection* selection,
                                        int row_capacity,
                                        int* out_top_note,
                                        int* out_row_count) {
    int rows = midi_editor_pitch_clamp_row_count(0, row_capacity);
    int top = midi_editor_pitch_default_top_note(rows);
    bool matched = false;
    if (midi_editor_pitch_viewport_matches_selection(state, selection)) {
        rows = midi_editor_pitch_clamp_row_count(state->midi_editor_ui.pitch_viewport_row_count, row_capacity);
        top = midi_editor_pitch_clamp_top_note(state->midi_editor_ui.pitch_viewport_top_note, rows);
        matched = true;
    }
    if (out_top_note) {
        *out_top_note = top;
    }
    if (out_row_count) {
        *out_row_count = rows;
    }
    return matched;
}

bool midi_editor_pitch_note_to_row(const MidiEditorLayout* layout, int note, int* out_row) {
    if (!layout || layout->key_row_count <= 0 ||
        note < layout->lowest_note || note > layout->highest_note) {
        return false;
    }
    int row = layout->highest_note - note;
    if (row < 0 || row >= layout->key_row_count) {
        return false;
    }
    if (out_row) {
        *out_row = row;
    }
    return true;
}

bool midi_editor_pitch_row_to_note(const MidiEditorLayout* layout, int row, uint8_t* out_note) {
    if (!layout || row < 0 || row >= layout->key_row_count) {
        return false;
    }
    int note = layout->highest_note - row;
    if (note < ENGINE_MIDI_NOTE_MIN || note > ENGINE_MIDI_NOTE_MAX ||
        note < layout->lowest_note || note > layout->highest_note) {
        return false;
    }
    if (out_note) {
        *out_note = (uint8_t)note;
    }
    return true;
}
