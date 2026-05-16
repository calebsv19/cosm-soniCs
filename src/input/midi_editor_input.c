#include "input/midi_editor_input.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "input/midi_editor_input_internal.h"
#include "ui/layout.h"
#include "ui/midi_editor.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>


static bool midi_editor_handle_instrument_button(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
    state->midi_editor_ui.instrument_menu_open = !state->midi_editor_ui.instrument_menu_open;
    if (state->midi_editor_ui.instrument_menu_open) {
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
    }
    return true;
}

static bool midi_editor_open_instrument_panel(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_record_armed) {
        midi_editor_complete_all_qwerty_notes(state);
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;
    state->midi_editor_ui.qwerty_record_armed = false;
    state->midi_editor_ui.qwerty_test_enabled = false;
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_menu_scroll_row = 0;
    state->midi_editor_ui.instrument_menu_expanded_category = -1;
    state->midi_editor_ui.instrument_param_drag_active = false;
    state->midi_editor_ui.instrument_param_drag_index = -1;
    midi_editor_clear_qwerty_active_notes(state);
    midi_editor_clear_shift_note_pending(state);
    midi_editor_clear_note_press_pending(state);
    midi_editor_clear_marquee_state(state);
    midi_editor_end_drag(state);
    return true;
}

static bool midi_editor_handle_instrument_menu_click(AppState* state,
                                                     const MidiEditorSelection* selection,
                                                     const MidiEditorLayout* layout,
                                                     int x,
                                                     int y) {
    if (!state || !selection || !selection->clip || !layout ||
        !state->midi_editor_ui.instrument_menu_open) {
        return false;
    }
    SDL_Point point = {x, y};
    EngineInstrumentPresetId preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    if (midi_preset_browser_preset_at(&layout->instrument_browser, x, y, &preset)) {
        (void)midi_editor_apply_instrument_preset(state, selection, preset);
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        return true;
    }
    EngineInstrumentPresetCategoryId category = ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
    if (midi_preset_browser_category_at(&layout->instrument_browser, x, y, &category)) {
        int current = state->midi_editor_ui.instrument_menu_expanded_category;
        state->midi_editor_ui.instrument_menu_expanded_category =
            current == (int)category ? -1 : (int)category;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        return true;
    }
    if (SDL_PointInRect(&point, &layout->instrument_menu_rect)) {
        return true;
    }
    if (!SDL_PointInRect(&point, &layout->instrument_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        if (SDL_PointInRect(&point, &layout->test_button_rect)) {
            return false;
        }
        return midi_editor_point_in_panel(state, x, y);
    }
    return false;
}

static bool midi_editor_handle_scroll(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_MOUSEWHEEL) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    SDL_Point point = {state->mouse_x, state->mouse_y};
    if (state->midi_editor_ui.instrument_menu_open &&
        SDL_PointInRect(&point, &layout.instrument_menu_rect)) {
        int delta = event->wheel.y != 0 ? event->wheel.y : event->wheel.x;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            delta = -delta;
        }
        state->midi_editor_ui.instrument_menu_scroll_row =
            midi_preset_browser_scroll_delta(&layout.instrument_browser,
                                             state->midi_editor_ui.instrument_menu_scroll_row,
                                             delta);
        return true;
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y) &&
        !midi_editor_pointer_over_pitch_area(&layout, state->mouse_x, state->mouse_y)) {
        return midi_editor_point_in_panel(state, state->mouse_x, state->mouse_y);
    }
    int delta = event->wheel.y != 0 ? event->wheel.y : event->wheel.x;
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    if (delta == 0) {
        return true;
    }
    SDL_Keymod mods = SDL_GetModState();
    if ((mods & (KMOD_CTRL | KMOD_GUI)) != 0 &&
        midi_editor_pointer_over_pitch_area(&layout, state->mouse_x, state->mouse_y)) {
        if (mods & KMOD_ALT) {
            int row_delta = delta > 0 ? -2 : 2;
            return midi_editor_zoom_pitch_viewport(state, &selection, &layout, state->mouse_y, row_delta);
        }
        return midi_editor_pan_pitch_viewport(state, &selection, &layout, delta);
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y)) {
        return midi_editor_point_in_panel(state, state->mouse_x, state->mouse_y);
    }
    if (mods & KMOD_ALT) {
        float zoom_factor = 1.0f - (float)delta * 0.1f;
        if (zoom_factor < 0.1f) {
            zoom_factor = 0.1f;
        }
        return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, zoom_factor);
    }
    if (mods & KMOD_SHIFT) {
        uint64_t frame = 0;
        uint64_t clip_frames = midi_editor_clip_frames(selection.clip);
        if (midi_editor_point_to_frame(&layout, clip_frames, state->mouse_x, &frame)) {
            uint64_t span = layout.view_span_frames > 0 ? layout.view_span_frames : clip_frames;
            uint64_t step = span / 16u;
            if (step < 1u) {
                step = 1u;
            }
            if (delta > 0) {
                frame = step > frame ? 0 : frame - step;
            } else {
                frame = frame + step > clip_frames ? clip_frames : frame + step;
            }
            uint64_t absolute_frame = selection.clip->timeline_start_frames + frame;
            bool was_playing = engine_transport_is_playing(state->engine);
            input_manager_reset_meter_history_on_seek(state);
            engine_transport_seek(state->engine, absolute_frame);
            if (was_playing) {
                engine_transport_play(state->engine);
            }
        }
        return true;
    }
    return midi_editor_pan_viewport(state, &selection, &layout, -delta);
}

static bool midi_editor_handle_viewport_keydown(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y) &&
        !midi_editor_pointer_over_pitch_area(&layout, state->mouse_x, state->mouse_y)) {
        return false;
    }
    SDL_Keymod mods = SDL_GetModState();
    SDL_Keycode key = event->key.keysym.sym;
    if ((mods & (KMOD_CTRL | KMOD_GUI)) != 0 &&
        midi_editor_pointer_over_pitch_area(&layout, state->mouse_x, state->mouse_y) &&
        (key == SDLK_0 || key == SDLK_KP_0)) {
        return midi_editor_fit_pitch_viewport_to_selected_notes(state, &selection, &layout);
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y)) {
        return false;
    }
    if (mods & KMOD_ALT) {
        if (key == SDLK_LEFT) {
            return midi_editor_pan_viewport(state, &selection, &layout, -1);
        }
        if (key == SDLK_RIGHT) {
            return midi_editor_pan_viewport(state, &selection, &layout, 1);
        }
        if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
            return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, 0.8f);
        }
        if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
            return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, 1.25f);
        }
        if (key == SDLK_0 || key == SDLK_KP_0) {
            midi_editor_fit_viewport(state, &selection);
            return true;
        }
    }
    return false;
}

static bool midi_editor_handle_left_down(AppState* state, int x, int y) {
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    SDL_Point point = {x, y};
    if (midi_editor_handle_instrument_menu_click(state, &selection, &layout, x, y)) {
        return true;
    }
    if (SDL_PointInRect(&point, &layout.instrument_button_rect)) {
        return midi_editor_handle_instrument_button(state, &selection);
    }
    if (SDL_PointInRect(&point, &layout.instrument_panel_button_rect)) {
        return midi_editor_open_instrument_panel(state);
    }
    if (SDL_PointInRect(&point, &layout.test_button_rect)) {
        return midi_editor_toggle_qwerty_test(state);
    }
    if (SDL_PointInRect(&point, &layout.quantize_button_rect)) {
        return midi_editor_quantize_selected_note(state) || midi_editor_point_in_panel(state, x, y);
    }
    if (SDL_PointInRect(&point, &layout.quantize_down_button_rect)) {
        return midi_editor_adjust_quantize_division(state, -1);
    }
    if (SDL_PointInRect(&point, &layout.quantize_up_button_rect)) {
        return midi_editor_adjust_quantize_division(state, 1);
    }
    if (SDL_PointInRect(&point, &layout.octave_down_button_rect)) {
        return midi_editor_adjust_octave(state, -1);
    }
    if (SDL_PointInRect(&point, &layout.octave_up_button_rect)) {
        return midi_editor_adjust_octave(state, 1);
    }
    if (SDL_PointInRect(&point, &layout.velocity_down_button_rect)) {
        return midi_editor_adjust_default_velocity(state, -0.05f);
    }
    if (SDL_PointInRect(&point, &layout.velocity_up_button_rect)) {
        return midi_editor_adjust_default_velocity(state, 0.05f);
    }
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_menu_scroll_row = 0;
    state->midi_editor_ui.instrument_menu_expanded_category = -1;
    if (SDL_PointInRect(&point, &layout.time_ruler_rect)) {
        midi_editor_clear_hover_note(state);
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        return midi_editor_seek_to_editor_x(state, &selection, &layout, x);
    }
    if (!SDL_PointInRect(&point, &layout.grid_rect)) {
        return midi_editor_point_in_panel(state, x, y);
    }

    MidiEditorNoteHit hit = {0};
    if (midi_editor_hit_test_note(&layout, selection.clip, x, y, &hit)) {
        const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
        if (!notes || hit.note_index < 0 || hit.note_index >= engine_clip_midi_note_count(selection.clip)) {
            return true;
        }
        SDL_Keymod mods = SDL_GetModState();
        bool shift_held = (mods & KMOD_SHIFT) != 0;
        if (shift_held) {
            midi_editor_clear_marquee_state(state);
            midi_editor_clear_note_press_pending(state);
            return midi_editor_begin_shift_note_pending(state, &selection, hit.note_index, x, y);
        }
        bool note_was_selected = midi_editor_note_is_selected(state, &selection, hit.note_index);
        int selected_count = midi_editor_effective_selected_note_count(
            state,
            &selection,
            engine_clip_midi_note_count(selection.clip));
        if (note_was_selected) {
            midi_editor_clear_shift_note_pending(state);
            midi_editor_clear_marquee_state(state);
            return midi_editor_begin_note_press_pending(state,
                                                        &selection,
                                                        hit.note_index,
                                                        hit.part,
                                                        x,
                                                        y,
                                                        selected_count > 1);
        }
        midi_editor_select_note(state, &selection, hit.note_index);
        return true;
    }
    midi_editor_clear_hover_note(state);

    SDL_Keymod mods = SDL_GetModState();
    if ((mods & KMOD_SHIFT) != 0) {
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        return midi_editor_begin_marquee(state, &selection, x, y, true);
    }

    uint8_t note = 60;
    uint64_t frame = 0;
    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    if (!midi_editor_point_to_note_frame(&layout, clip_frames, x, y, &note, &frame)) {
        return true;
    }
    frame = midi_editor_snap_relative_frame(state, selection.clip, frame);
    uint64_t duration = midi_editor_default_note_duration(state, selection.clip);
    if (frame + duration > clip_frames) {
        frame = clip_frames > duration ? clip_frames - duration : 0;
    }
    EngineMidiNote new_note = {frame, duration, note, midi_editor_current_default_velocity(state)};
    int new_index = -1;
    if (!midi_editor_begin_note_undo(state, &selection)) {
        return true;
    }
    if (!engine_clip_midi_add_note(state->engine, selection.track_index, selection.clip_index, new_note, &new_index)) {
        undo_manager_cancel_drag(&state->undo);
        return true;
    }
    midi_editor_select_note(state, &selection, new_index);
    midi_editor_set_drag_state(state, &selection, MIDI_EDITOR_DRAG_CREATE, new_index, x, y, new_note);
    if (state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_MIDI_NOTE_EDIT) {
        state->midi_editor_ui.drag_mutated = true;
    }
    return true;
}

bool midi_editor_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event || !midi_editor_should_render(state) ||
        state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT) {
        return false;
    }

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            return midi_editor_handle_left_down(state, event->button.x, event->button.y);
        }
        return midi_editor_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            state->midi_editor_ui.instrument_param_drag_active = false;
            state->midi_editor_ui.instrument_param_drag_index = -1;
            midi_editor_commit_shift_note_pending(state);
            midi_editor_commit_note_press_pending(state);
            midi_editor_commit_marquee(state);
            midi_editor_end_drag(state);
        }
        return midi_editor_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEMOTION:
        if (state->midi_editor_ui.shift_note_pending) {
            midi_editor_update_shift_note_pending(state, event->motion.x, event->motion.y);
        } else if (state->midi_editor_ui.note_press_pending) {
            midi_editor_update_note_press_pending(state, event->motion.x, event->motion.y);
        } else if (state->midi_editor_ui.marquee_active) {
            MidiEditorSelection selection = {0};
            MidiEditorLayout layout = {0};
            if (midi_editor_get_fresh_selection(state, &selection, &layout)) {
                midi_editor_update_marquee_preview(state, &selection, &layout, event->motion.x, event->motion.y);
            } else {
                midi_editor_clear_marquee_state(state);
            }
        } else if (state->midi_editor_ui.drag_active) {
            midi_editor_update_drag(state, event->motion.x, event->motion.y);
        } else {
            MidiEditorSelection selection = {0};
            MidiEditorLayout layout = {0};
            if (midi_editor_get_fresh_selection(state, &selection, &layout) &&
                SDL_PointInRect(&(SDL_Point){event->motion.x, event->motion.y}, &layout.grid_rect)) {
                midi_editor_update_hover_note(state, &selection, &layout, event->motion.x, event->motion.y);
            } else {
                midi_editor_clear_hover_note(state);
            }
        }
        return midi_editor_point_in_panel(state, event->motion.x, event->motion.y);
    case SDL_MOUSEWHEEL:
        return midi_editor_handle_scroll(state, event);
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event->type == SDL_KEYDOWN && midi_editor_handle_clipboard_keydown(state, event)) {
            return true;
        }
        if (event->type == SDL_KEYDOWN && midi_editor_handle_note_command_keydown(state, event)) {
            return true;
        }
        if (event->type == SDL_KEYDOWN && midi_editor_handle_viewport_keydown(state, event)) {
            return true;
        }
        if (midi_editor_handle_qwerty_event(state, event)) {
            return true;
        }
        if (event->type == SDL_KEYUP) {
            break;
        }
        if (event->key.keysym.sym == SDLK_ESCAPE && state->midi_editor_ui.instrument_menu_open) {
            state->midi_editor_ui.instrument_menu_open = false;
            state->midi_editor_ui.instrument_menu_scroll_row = 0;
            state->midi_editor_ui.instrument_menu_expanded_category = -1;
            return true;
        }
        if (event->key.keysym.sym == SDLK_DELETE || event->key.keysym.sym == SDLK_BACKSPACE) {
            return midi_editor_delete_selected_note(state);
        }
        break;
    default:
        break;
    }
    return false;
}

void midi_editor_input_update(InputManager* manager, AppState* state, bool left_was_down, bool left_is_down) {
    (void)manager;
    if (!state) {
        return;
    }
    if (!midi_editor_should_render(state)) {
        if (state->midi_editor_ui.qwerty_record_armed) {
            midi_editor_complete_all_qwerty_notes(state);
            state->midi_editor_ui.qwerty_record_armed = false;
        }
        state->midi_editor_ui.qwerty_test_enabled = false;
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        midi_editor_clear_qwerty_active_notes(state);
        return;
    }
    if (state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT) {
        if (state->midi_editor_ui.qwerty_record_armed) {
            midi_editor_complete_all_qwerty_notes(state);
        }
        state->midi_editor_ui.qwerty_record_armed = false;
        state->midi_editor_ui.qwerty_test_enabled = false;
        midi_editor_clear_qwerty_active_notes(state);
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        midi_editor_end_drag(state);
        return;
    }
    if (state->midi_editor_ui.shift_note_pending && left_is_down) {
        midi_editor_update_shift_note_pending(state, state->mouse_x, state->mouse_y);
    } else if (state->midi_editor_ui.note_press_pending && left_is_down) {
        midi_editor_update_note_press_pending(state, state->mouse_x, state->mouse_y);
    } else if (state->midi_editor_ui.marquee_active && left_is_down) {
        MidiEditorSelection selection = {0};
        MidiEditorLayout layout = {0};
        if (midi_editor_get_fresh_selection(state, &selection, &layout)) {
            midi_editor_update_marquee_preview(state, &selection, &layout, state->mouse_x, state->mouse_y);
        } else {
            midi_editor_clear_marquee_state(state);
        }
    } else if (state->midi_editor_ui.drag_active && left_is_down) {
        midi_editor_update_drag(state, state->mouse_x, state->mouse_y);
    }
    if (left_was_down && !left_is_down) {
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        midi_editor_commit_shift_note_pending(state);
        midi_editor_commit_note_press_pending(state);
        midi_editor_commit_marquee(state);
        midi_editor_end_drag(state);
    }
}
