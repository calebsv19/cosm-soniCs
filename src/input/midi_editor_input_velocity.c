#include "input/midi_editor_input_internal.h"
#include "undo/undo_manager.h"

#include <stdlib.h>

// Publishes a bounded selected-note velocity change with one reserved undo entry.
bool midi_editor_adjust_velocity(AppState* state, float delta) {
    if (!state || delta == 0.0f || state->midi_editor_ui.drag_active || state->undo.active_drag_valid) return false;
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) return false;
    int count = engine_clip_midi_note_count(selection.clip);
    if (midi_editor_effective_selected_note_count(state, &selection, count) == 0) {
        return midi_editor_adjust_default_velocity(state, delta);
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    UndoMidiNoteEdit* edit = &cmd.data.midi_note_edit;
    edit->track_index = selection.track_index;
    edit->clip_creation_index = selection.clip->creation_index;
    if (!midi_editor_snapshot_notes(selection.clip, &edit->before_notes, &edit->before_note_count) ||
        !midi_editor_snapshot_notes(selection.clip, &edit->after_notes, &edit->after_note_count)) {
        free(edit->before_notes);
        free(edit->after_notes);
        return false;
    }
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        if (!midi_editor_note_index_selected(state, &selection, i)) continue;
        float velocity = midi_editor_clamp_velocity(edit->after_notes[i].velocity + delta);
        if (velocity != edit->after_notes[i].velocity) changed = true;
        edit->after_notes[i].velocity = velocity;
    }
    bool ok = true;
    if (changed) {
        ok = undo_manager_begin_drag(&state->undo, &cmd);
        if (ok) {
            ok = engine_clip_midi_set_notes(state->engine, selection.track_index,
                                           selection.clip_index, edit->after_notes, count);
            if (ok) ok = undo_manager_commit_drag(&state->undo, &state->undo.active_drag);
            else undo_manager_cancel_drag(&state->undo);
        }
    }
    free(edit->before_notes);
    free(edit->after_notes);
    return ok;
}
