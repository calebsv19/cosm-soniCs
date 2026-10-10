#include "ui/preset_panel.h"
#include "ui/text_edit.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "daw/preset_actions.h"
#include "app_state.h"
#include <string.h>
// Releases only this panel's text ownership and restores the surviving native field.
static void close_overlay(AppState* state) {
    DawPresetUI* ui = &state->preset_ui;
    ui->open = ui->save_open = ui->editing = false;
    kit_ui_text_cancel_composition(&ui->text_edit); daw_text_resume_input(state);
}
// Resolves current selection into the existing generation-bound application target.
static bool current(AppState* state, DawInstrumentSnapshot* out) {
    MidiEditorSelection selection; DawActionTarget target;
    if (!midi_editor_get_selection(state, &selection) ||
        daw_instrument_target_capture(state, selection.track_index, selection.clip_index, &target) != DAW_ACTION_UNCHANGED) return false;
    DawActionResult result = daw_action_get_instrument(state, target);
    if (!result.has_snapshot) return false;
    *out = result.snapshot; return true;
}
// Commits the captured sound only after the common action rechecks its project state and library revision.
static void save(AppState* state) {
    DawPresetUI* ui = &state->preset_ui;
    DawPresetSaveResult result = daw_action_save_preset(state, &ui->captured, ui->name,
        ui->update ? ui->loaded.id : NULL, ui->update ? ui->loaded.revision : NULL);
    SDL_strlcpy(ui->message, result.status == DAW_PRESET_OK ?
        (result.durable ? "Preset saved to My Presets." : "Preset published; directory sync unavailable.") : daw_preset_status_message(result.status), sizeof(ui->message));
    if (result.status == DAW_PRESET_OK) close_overlay(state);
}
// Opens caller-owned shared UTF-8 editing for a search or captured sound name.
static void begin_text(AppState* state) {
    DawPresetUI* ui = &state->preset_ui;
    daw_text_cancel_composition(state); ui->editing = true; ui->cursor = 0;
    char* buffer = ui->save_open ? ui->name : ui->search;
    ui->cursor = (int)strlen(buffer);
    daw_text_edit_begin(&ui->text_edit, buffer, 96, &ui->cursor, KIT_UI_TEXT_SINGLE_LINE);
    SDL_StartTextInput();
}
// Runs every mouse and keyboard activation through the same preset action owner.
bool daw_preset_panel_activate(AppState* state, int x, int y) {
    if (!state || !midi_instrument_panel_should_render(state)) return false;
    MidiInstrumentPanelLayout panel; midi_instrument_panel_compute_layout(state, &panel);
    DawPresetPanelLayout layout; daw_preset_panel_layout(state, panel.panel_rect, panel.header_rect, &layout);
    DawPresetUI* ui = &state->preset_ui; SDL_Point point = {x, y}; DawInstrumentSnapshot sound;
    if (!current(state, &sound)) { close_overlay(state); return false; }
    if (ui->save_open) {
        if (SDL_PointInRect(&point, &layout.confirm)) { save(state); return true; }
        if (SDL_PointInRect(&point, &layout.cancel)) { close_overlay(state); return true; }
        if (SDL_PointInRect(&point, &layout.field)) { begin_text(state); return true; }
        return SDL_PointInRect(&point, &layout.overlay);
    }
    if (SDL_PointInRect(&point, &layout.library_button)) {
        bool open = !ui->open; close_overlay(state); ui->open = open;
        state->midi_editor_ui.instrument_menu_open = false;
        if (open) { ui->scroll = 0; ui->selected_row = 0; ui->message[0] = 0; daw_preset_panel_refresh(state); begin_text(state); }
        return true;
    }
    if (SDL_PointInRect(&point, &layout.save_button) || SDL_PointInRect(&point, &layout.update_button)) {
        if (state->undo.active_drag_valid) { SDL_strlcpy(ui->message, "Finish the instrument edit first.", sizeof(ui->message)); return true; }
        bool update = SDL_PointInRect(&point, &layout.update_button);
        if (update && (!ui->has_loaded || ui->loaded_target.project_generation != sound.target.project_generation ||
            ui->loaded_target.track_id != sound.target.track_id || ui->loaded_target.clip_id != sound.target.clip_id ||
            ui->loaded_target.kind != sound.target.kind || ui->loaded.base != sound.effective_preset)) {
            SDL_strlcpy(ui->message, "Load a user preset before updating it.", sizeof(ui->message)); return true;
        }
        close_overlay(state); ui->save_open = true; ui->update = update; ui->captured = sound; ui->message[0] = 0;
        SDL_strlcpy(ui->name, update ? ui->loaded.name : engine_instrument_preset_display_name(sound.effective_preset), sizeof(ui->name));
        state->midi_editor_ui.instrument_menu_open = false; begin_text(state); return true;
    }
    if (SDL_PointInRect(&point, &layout.note_button) || SDL_PointInRect(&point, &layout.chord_button)) {
        DawActionResult result = daw_action_audition_instrument(state, sound.target, SDL_PointInRect(&point, &layout.chord_button));
        SDL_strlcpy(ui->message, result.message, sizeof(ui->message)); return true;
    }
    if (ui->open) {
        if (SDL_PointInRect(&point, &layout.field)) { begin_text(state); return true; }
        if (SDL_PointInRect(&point, &layout.refresh)) { daw_preset_panel_refresh(state); return true; }
        for (int i = 0; i < layout.row_count; ++i) if (SDL_PointInRect(&point, &layout.rows[i])) {
            DawUserPreset preset = ui->library.items[layout.item_indices[i]];
            DawActionResult result = daw_action_apply_preset(state, sound.target, preset.id, preset.revision);
            SDL_strlcpy(ui->message, result.message, sizeof(ui->message));
            if (result.status <= DAW_ACTION_UNCHANGED) close_overlay(state);
            return true;
        }
        if (SDL_PointInRect(&point, &layout.overlay)) return true;
        close_overlay(state);
    }
    return false;
}
// Captures text and browser input ahead of global shortcuts, preventing keyboard notes while naming a sound.
bool daw_preset_panel_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) return false;
    DawPresetUI* ui = &state->preset_ui;
    if (!midi_instrument_panel_should_render(state)) {
        if (ui->open || ui->save_open || ui->editing) close_overlay(state);
        return false;
    }
    if ((ui->open || ui->save_open) && event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
        return daw_preset_panel_activate(state, event->button.x, event->button.y);
    if (!ui->open && !ui->save_open) return false;
    if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST || event->window.event == SDL_WINDOWEVENT_HIDDEN)) {
        kit_ui_text_cancel_composition(&ui->text_edit); return false;
    }
    if (event->type == SDL_MOUSEWHEEL && ui->open) {
        ui->scroll -= event->wheel.y; if (ui->scroll < 0) ui->scroll = 0;
        if (ui->scroll > ui->library.count - 1) ui->scroll = ui->library.count ? ui->library.count - 1 : 0;
        return true;
    }
    if (ui->open && event->type == SDL_KEYDOWN &&
        (event->key.keysym.sym == SDLK_DOWN || event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_RETURN)) {
        MidiInstrumentPanelLayout panel; midi_instrument_panel_compute_layout(state, &panel);
        DawPresetPanelLayout layout; daw_preset_panel_layout(state, panel.panel_rect, panel.header_rect, &layout);
        if (ui->selected_row >= layout.row_count) ui->selected_row = layout.row_count ? layout.row_count - 1 : 0;
        if (event->key.keysym.sym == SDLK_RETURN && !event->key.repeat && layout.row_count) {
            SDL_Rect row = layout.rows[ui->selected_row];
            return daw_preset_panel_activate(state, row.x + 1, row.y + 1);
        }
        if (event->key.keysym.sym == SDLK_DOWN) {
            if (ui->selected_row + 1 < layout.row_count) ++ui->selected_row;
            else if (layout.row_count == 8 && ui->scroll + layout.row_count < ui->library.count) ++ui->scroll;
        } else if (event->key.keysym.sym == SDLK_UP) {
            if (ui->selected_row) --ui->selected_row; else if (ui->scroll) --ui->scroll;
        }
        return true;
    }
    if (ui->editing) {
        char* buffer = ui->save_open ? ui->name : ui->search;
        KitUiTextEventResult result = daw_text_edit_event(&ui->text_edit, buffer, 96, &ui->cursor, KIT_UI_TEXT_SINGLE_LINE, event);
        if (result.changed && !ui->save_open) { ui->scroll = 0; ui->selected_row = 0; }
        if (result.cancel) close_overlay(state);
        else if (result.submit && ui->save_open) save(state);
        if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT || event->type == SDL_TEXTEDITING) return true;
        return result.consumed;
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) { close_overlay(state); return true; }
    return false;
}
