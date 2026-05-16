#include "input/midi_editor_input_internal.h"

#include "app_state.h"
#include "ui/layout.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool midi_editor_point_in_panel(const AppState* state, int x, int y) {
    if (!midi_editor_should_render(state)) {
        return false;
    }
    const Pane* pane = ui_layout_get_pane(state, 2);
    if (!pane) {
        return false;
    }
    SDL_Point p = {x, y};
    return SDL_PointInRect(&p, &pane->rect);
}

bool midi_editor_get_fresh_selection(const AppState* state,
                                            MidiEditorSelection* selection,
                                            MidiEditorLayout* layout) {
    if (!midi_editor_get_selection(state, selection)) {
        return false;
    }
    if (layout) {
        midi_editor_compute_layout(state, layout);
        if (layout->grid_rect.w <= 0 || layout->grid_rect.h <= 0) {
            return false;
        }
    }
    return true;
}

const EngineClip* midi_editor_clip_from_indices(const AppState* state,
                                                       int track_index,
                                                       int clip_index) {
    if (!state || !state->engine) {
        return NULL;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return NULL;
    }
    const EngineTrack* track = &tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip;
}

bool midi_editor_snapshot_notes(const EngineClip* clip, EngineMidiNote** out_notes, int* out_count) {
    if (!out_notes || !out_count) {
        return false;
    }
    *out_notes = NULL;
    *out_count = 0;
    if (!clip || engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }
    int count = engine_clip_midi_note_count(clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    if (count <= 0) {
        return true;
    }
    if (!notes) {
        return false;
    }
    EngineMidiNote* copy = (EngineMidiNote*)calloc((size_t)count, sizeof(EngineMidiNote));
    if (!copy) {
        return false;
    }
    memcpy(copy, notes, sizeof(EngineMidiNote) * (size_t)count);
    *out_notes = copy;
    *out_count = count;
    return true;
}

void midi_editor_free_notes(EngineMidiNote** notes, int* count) {
    if (!notes || !count) {
        return;
    }
    free(*notes);
    *notes = NULL;
    *count = 0;
}

bool midi_editor_selection_matches_ui(const AppState* state,
                                             const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.selected_track_index == selection->track_index &&
           state->midi_editor_ui.selected_clip_index == selection->clip_index &&
           state->midi_editor_ui.selected_clip_creation_index == selection->clip->creation_index;
}

void midi_editor_clear_note_selection(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.selected_note_index = -1;
    memset(state->midi_editor_ui.selected_note_indices,
           0,
           sizeof(state->midi_editor_ui.selected_note_indices));
}

void midi_editor_clear_marquee_state(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.marquee_active = false;
    state->midi_editor_ui.marquee_additive = false;
    state->midi_editor_ui.marquee_start_x = 0;
    state->midi_editor_ui.marquee_start_y = 0;
    state->midi_editor_ui.marquee_current_x = 0;
    state->midi_editor_ui.marquee_current_y = 0;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
}

void midi_editor_clear_note_press_pending(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.note_press_pending = false;
    state->midi_editor_ui.note_press_pending_track_index = -1;
    state->midi_editor_ui.note_press_pending_clip_index = -1;
    state->midi_editor_ui.note_press_pending_clip_creation_index = 0;
    state->midi_editor_ui.note_press_pending_note_index = -1;
    state->midi_editor_ui.note_press_pending_x = 0;
    state->midi_editor_ui.note_press_pending_y = 0;
    state->midi_editor_ui.note_press_pending_part = MIDI_EDITOR_NOTE_HIT_NONE;
    state->midi_editor_ui.note_press_group_candidate = false;
}

int midi_editor_effective_selected_note_count(const AppState* state,
                                                     const MidiEditorSelection* selection,
                                                     int note_count) {
    if (!midi_editor_selection_matches_ui(state, selection) || note_count <= 0) {
        return 0;
    }
    if (note_count > ENGINE_MIDI_NOTE_CAP) {
        note_count = ENGINE_MIDI_NOTE_CAP;
    }
    int count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            ++count;
        }
    }
    if (count == 0 &&
        state->midi_editor_ui.selected_note_index >= 0 &&
        state->midi_editor_ui.selected_note_index < note_count) {
        count = 1;
    }
    return count;
}

bool midi_editor_note_index_selected(const AppState* state,
                                            const MidiEditorSelection* selection,
                                            int note_index) {
    if (!midi_editor_selection_matches_ui(state, selection) ||
        note_index < 0 ||
        note_index >= ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (state->midi_editor_ui.selected_note_indices[note_index]) {
        return true;
    }
    return state->midi_editor_ui.selected_note_index == note_index;
}

void midi_editor_build_effective_selection_mask(const AppState* state,
                                                       const MidiEditorSelection* selection,
                                                       int note_count,
                                                       bool* out_mask) {
    if (!out_mask || note_count <= 0) {
        return;
    }
    memset(out_mask, 0, sizeof(bool) * (size_t)note_count);
    if (!midi_editor_selection_matches_ui(state, selection)) {
        return;
    }
    int bounded = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    int count = 0;
    for (int i = 0; i < bounded; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            out_mask[i] = true;
            ++count;
        }
    }
    if (count == 0 &&
        state->midi_editor_ui.selected_note_index >= 0 &&
        state->midi_editor_ui.selected_note_index < bounded) {
        out_mask[state->midi_editor_ui.selected_note_index] = true;
    }
}

void midi_editor_focus_first_selected_note(AppState* state, int note_count) {
    if (!state) {
        return;
    }
    int bounded = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    state->midi_editor_ui.selected_note_index = -1;
    for (int i = 0; i < bounded; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            state->midi_editor_ui.selected_note_index = i;
            return;
        }
    }
}

bool midi_editor_begin_note_undo(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = selection->track_index;
    cmd.data.midi_note_edit.clip_creation_index = selection->clip->creation_index;
    if (!midi_editor_snapshot_notes(selection->clip,
                                    &cmd.data.midi_note_edit.before_notes,
                                    &cmd.data.midi_note_edit.before_note_count)) {
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    midi_editor_free_notes(&cmd.data.midi_note_edit.before_notes,
                           &cmd.data.midi_note_edit.before_note_count);
    return ok;
}

bool midi_editor_commit_note_undo(AppState* state) {
    if (!state || !state->undo.active_drag_valid ||
        state->undo.active_drag.type != UNDO_CMD_MIDI_NOTE_EDIT) {
        return false;
    }
    UndoCommand cmd = {0};
    const UndoMidiNoteEdit* active = &state->undo.active_drag.data.midi_note_edit;
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = active->track_index;
    cmd.data.midi_note_edit.clip_creation_index = active->clip_creation_index;
    cmd.data.midi_note_edit.before_notes = active->before_notes;
    cmd.data.midi_note_edit.before_note_count = active->before_note_count;

    const EngineClip* clip = midi_editor_clip_from_indices(state, active->track_index, state->midi_editor_ui.drag_clip_index);
    if (!clip || clip->creation_index != active->clip_creation_index ||
        !midi_editor_snapshot_notes(clip,
                                    &cmd.data.midi_note_edit.after_notes,
                                    &cmd.data.midi_note_edit.after_note_count)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    bool ok = undo_manager_commit_drag(&state->undo, &cmd);
    midi_editor_free_notes(&cmd.data.midi_note_edit.after_notes,
                           &cmd.data.midi_note_edit.after_note_count);
    return ok;
}

bool midi_editor_push_note_undo(AppState* state,
                                       const MidiEditorSelection* selection,
                                       EngineMidiNote* before_notes,
                                       int before_count) {
    if (!state || !selection || !selection->clip) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = selection->track_index;
    cmd.data.midi_note_edit.clip_creation_index = selection->clip->creation_index;
    cmd.data.midi_note_edit.before_notes = before_notes;
    cmd.data.midi_note_edit.before_note_count = before_count;
    bool ok = midi_editor_snapshot_notes(selection->clip,
                                         &cmd.data.midi_note_edit.after_notes,
                                         &cmd.data.midi_note_edit.after_note_count);
    if (ok) {
        ok = undo_manager_push(&state->undo, &cmd);
    }
    midi_editor_free_notes(&cmd.data.midi_note_edit.before_notes,
                           &cmd.data.midi_note_edit.before_note_count);
    midi_editor_free_notes(&cmd.data.midi_note_edit.after_notes,
                           &cmd.data.midi_note_edit.after_note_count);
    return ok;
}

static bool midi_editor_instrument_params_equal(EngineInstrumentParams a,
                                                EngineInstrumentParams b) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamId param = (EngineInstrumentParamId)i;
        if (fabsf(engine_instrument_params_get(a, param) -
                  engine_instrument_params_get(b, param)) >= 0.0001f) {
            return false;
        }
    }
    return true;
}

static bool midi_editor_clip_state_equal(const UndoClipState* a, const UndoClipState* b) {
    if (!a || !b) {
        return true;
    }
    if (a->track_index != b->track_index ||
        a->start_frame != b->start_frame ||
        a->offset_frames != b->offset_frames ||
        a->duration_frames != b->duration_frames ||
        a->fade_in_frames != b->fade_in_frames ||
        a->fade_out_frames != b->fade_out_frames ||
        a->fade_in_curve != b->fade_in_curve ||
        a->fade_out_curve != b->fade_out_curve ||
        fabsf(a->gain - b->gain) >= 0.0001f ||
        a->instrument_preset != b->instrument_preset ||
        !midi_editor_instrument_params_equal(a->instrument_params, b->instrument_params) ||
        a->instrument_inherits_track != b->instrument_inherits_track ||
        a->midi_note_count != b->midi_note_count) {
        return false;
    }
    if (a->midi_note_count <= 0) {
        return true;
    }
    if (!a->midi_notes || !b->midi_notes) {
        return false;
    }
    return memcmp(a->midi_notes,
                  b->midi_notes,
                  sizeof(EngineMidiNote) * (size_t)a->midi_note_count) == 0;
}

bool midi_editor_apply_instrument_preset(AppState* state,
                                         const MidiEditorSelection* selection,
                                         EngineInstrumentPresetId preset) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    if (!undo_clip_state_from_engine_clip(selection->clip,
                                          selection->track_index,
                                          &cmd.data.clip_transform.before)) {
        return false;
    }
    bool ok = engine_clip_midi_set_instrument_preset(state->engine,
                                                     selection->track_index,
                                                     selection->clip_index,
                                                     preset);
    const EngineClip* clip = midi_editor_clip_from_indices(state,
                                                           selection->track_index,
                                                           selection->clip_index);
    if (ok && clip) {
        ok = undo_clip_state_from_engine_clip(clip,
                                              selection->track_index,
                                              &cmd.data.clip_transform.after);
    } else {
        ok = false;
    }
    if (ok && !midi_editor_clip_state_equal(&cmd.data.clip_transform.before,
                                            &cmd.data.clip_transform.after)) {
        ok = undo_manager_push(&state->undo, &cmd);
    }
    undo_clip_state_clear(&cmd.data.clip_transform.before);
    undo_clip_state_clear(&cmd.data.clip_transform.after);
    return ok;
}

bool midi_editor_begin_instrument_undo(AppState* state,
                                       const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    if (!undo_clip_state_from_engine_clip(selection->clip,
                                          selection->track_index,
                                          &cmd.data.clip_transform.before)) {
        return false;
    }
    if (!undo_clip_state_clone(&cmd.data.clip_transform.after,
                               &cmd.data.clip_transform.before)) {
        undo_clip_state_clear(&cmd.data.clip_transform.before);
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    undo_clip_state_clear(&cmd.data.clip_transform.before);
    undo_clip_state_clear(&cmd.data.clip_transform.after);
    return ok;
}

bool midi_editor_commit_instrument_undo(AppState* state) {
    if (!state || !state->undo.active_drag_valid ||
        state->undo.active_drag.type != UNDO_CMD_CLIP_TRANSFORM) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_selection(state, &selection) ||
        selection.clip->creation_index != state->undo.active_drag.data.clip_transform.before.creation_index) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    UndoCommand* cmd = &state->undo.active_drag;
    UndoClipState after = {0};
    if (!undo_clip_state_from_engine_clip(selection.clip, selection.track_index, &after)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    undo_clip_state_clear(&cmd->data.clip_transform.after);
    cmd->data.clip_transform.after = after;
    if (!midi_editor_clip_state_equal(&cmd->data.clip_transform.before,
                                      &cmd->data.clip_transform.after)) {
        return undo_manager_commit_drag(&state->undo, cmd);
    }
    undo_manager_cancel_drag(&state->undo);
    return true;
}

uint64_t midi_editor_min_note_duration(uint64_t clip_frames) {
    uint64_t min_duration = clip_frames / 64u;
    if (min_duration == 0) {
        min_duration = 1;
    }
    return min_duration;
}
