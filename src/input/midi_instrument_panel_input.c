#include "input/midi_instrument_panel_input.h"

#include "app_state.h"
#include "ui/knob_drag.h"
#include "ui/sound_panel.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "input/midi_editor_input_internal.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>

static bool instrument_panel_point_in_panel(const AppState* state, int x, int y) {
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &layout.panel_rect);
}

static float instrument_panel_param_value_from_drag(AppState* state,
                                                   int param_index,
                                                   int y) {
    if (!state || param_index < 0) {
        return 0.0f;
    }
    EngineInstrumentParamSpec spec = {0};
    if (!engine_instrument_param_spec((EngineInstrumentParamId)param_index, &spec)) {
        return 0.0f;
    }
    float scale = state->midi_editor_ui.instrument_param_drag_scale > 0 ? state->midi_editor_ui.instrument_param_drag_scale : 1;
    float initial = engine_instrument_param_to_normalized((EngineInstrumentParamId)param_index,
        state->midi_editor_ui.instrument_param_drag_start_value);
    return engine_instrument_param_from_normalized((EngineInstrumentParamId)param_index, daw_knob_drag_position(initial, state->midi_editor_ui.instrument_param_drag_start_y, y, scale));
}

static float instrument_panel_current_param_value(const AppState* state,
                                                  const MidiEditorSelection* selection,
                                                  int param_index) {
    if (!state || !selection || !selection->clip || param_index < 0) {
        return 0.0f;
    }
    EngineInstrumentParams params = engine_clip_midi_effective_instrument_params(state->engine,
                                                                                 selection->track_index,
                                                                                 selection->clip_index);
    return engine_instrument_params_get(params, (EngineInstrumentParamId)param_index);
}

static bool instrument_panel_handle_preset_menu_click(AppState* state,
                                                     const MidiEditorSelection* selection,
                                                     const MidiInstrumentPanelLayout* layout,
                                                     int x,
                                                     int y) {
    if (!state || !selection || !selection->clip || !layout ||
        !state->midi_editor_ui.instrument_menu_open) {
        return false;
    }
    SDL_Point point = {x, y};
    EngineInstrumentPresetId preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    if (midi_preset_browser_preset_at(&layout->preset_browser, x, y, &preset)) {
        (void)midi_editor_apply_instrument_preset(state, selection, preset);
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        return true;
    }
    EngineInstrumentPresetCategoryId category = ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
    if (midi_preset_browser_category_at(&layout->preset_browser, x, y, &category)) {
        int current = state->midi_editor_ui.instrument_menu_expanded_category;
        state->midi_editor_ui.instrument_menu_expanded_category =
            current == (int)category ? -1 : (int)category;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        return true;
    }
    if (SDL_PointInRect(&point, &layout->preset_menu_rect)) {
        return true;
    }
    if (!SDL_PointInRect(&point, &layout->preset_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        return instrument_panel_point_in_panel(state, x, y);
    }
    return false;
}

static bool instrument_panel_handle_param_down(AppState* state,
                                               const MidiEditorSelection* selection,
                                               const MidiInstrumentPanelLayout* layout,
                                               int x,
                                               int y) {
    (void)x;
    if (!state || !selection || !layout || layout->instrument_param_count <= 0) {
        return false;
    }
    SDL_Point point = {x, y};
    for (int i = 0; i < layout->instrument_param_count; ++i) {
        if (SDL_PointInRect(&point, &layout->param_widget_rects[i])) {
            EngineInstrumentParamId param = layout->param_ids[i];
            if (!midi_editor_begin_instrument_undo(state, selection)) {
                return false;
            }
            state->midi_editor_ui.instrument_param_drag_active = true;
            state->midi_editor_ui.instrument_param_drag_index = param;
            state->midi_editor_ui.instrument_param_drag_start_y = y;
            state->midi_editor_ui.instrument_param_drag_scale = (SDL_GetModState() & KMOD_SHIFT) ? 0.1f : 1;
            state->midi_editor_ui.instrument_param_drag_start_value =
                instrument_panel_current_param_value(state, selection, param);
            state->midi_editor_ui.instrument_menu_open = false;
            state->midi_editor_ui.instrument_menu_scroll_row = 0;
            state->midi_editor_ui.instrument_menu_expanded_category = -1;
            return true;
        }
    }
    return false;
}

// Publishes only to the gesture's captured region, even if the UI selection changes.
static bool instrument_panel_update_param_drag(AppState* state, int y) {
    if (!state || !state->midi_editor_ui.instrument_param_drag_active) return false;
    int param = state->midi_editor_ui.instrument_param_drag_index;
    if (param < 0 || param >= ENGINE_INSTRUMENT_PARAM_COUNT) return false;
    DawInstrumentGesture* gesture = &state->midi_editor_ui.instrument_gesture;
    DawInstrumentRequest request = {.target = gesture->target, .parameter_mask = UINT32_C(1) << param};
    request.params = engine_instrument_params_set(ENGINE_INSTRUMENT_PRESET_PURE_SINE, request.params,
        (EngineInstrumentParamId)param, instrument_panel_param_value_from_drag(state, param, y));
    DawActionResult result = daw_action_update_instrument(state, gesture, &request);
    if (result.status != DAW_ACTION_APPLIED && result.status != DAW_ACTION_UNCHANGED)
        undo_manager_reject(&state->undo, result.message);
    return result.status == DAW_ACTION_APPLIED || result.status == DAW_ACTION_UNCHANGED;
}

// Finishes or restores a preview before any panel transition can discard gesture flags.
static bool instrument_panel_finish_param_drag(AppState* state, bool commit);

// Executes an accepted control through the original product pointer owner.
bool midi_instrument_panel_input_activate_at(AppState* state, int x, int y) {
    if (!midi_instrument_panel_should_render(state)) {
        return false;
    }
    if (state->midi_editor_ui.instrument_gesture.active) {
        (void)instrument_panel_finish_param_drag(state, true);
        if (state->midi_editor_ui.instrument_gesture.active) return true;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_selection(state, &selection)) {
        return false;
    }
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    SDL_Point point = {x, y};
    if (daw_sound_panel_activate(state, x, y)) return true;
    if (daw_preset_panel_activate(state, x, y)) return true;

    if (instrument_panel_handle_preset_menu_click(state, &selection, &layout, x, y)) {
        return true;
    }
    for (int i = 0; i < layout.group_tab_count; ++i) {
        if (SDL_PointInRect(&point, &layout.group_tab_rects[i])) {
            state->midi_editor_ui.instrument_active_group = (EngineInstrumentParamGroupId)i;
            state->midi_editor_ui.instrument_menu_open = false;
            state->midi_editor_ui.instrument_menu_scroll_row = 0;
            state->midi_editor_ui.instrument_menu_expanded_category = -1;
            state->midi_editor_ui.instrument_param_drag_active = false;
            state->midi_editor_ui.instrument_param_drag_index = -1;
            return true;
        }
    }
    if (SDL_PointInRect(&point, &layout.notes_button_rect)) {
        state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_menu_scroll_row = 0;
        state->midi_editor_ui.instrument_menu_expanded_category = -1;
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        state->midi_editor_ui.instrument_param_drag_start_y = 0;
        state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
        return true;
    }
    if (SDL_PointInRect(&point, &layout.preset_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = !state->midi_editor_ui.instrument_menu_open;
        if (state->midi_editor_ui.instrument_menu_open) {
            state->midi_editor_ui.instrument_menu_scroll_row = 0;
            state->midi_editor_ui.instrument_menu_expanded_category = -1;
        }
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        state->midi_editor_ui.instrument_param_drag_start_y = 0;
        state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
        return true;
    }
    if (instrument_panel_handle_param_down(state, &selection, &layout, x, y)) {
        return true;
    }
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_menu_scroll_row = 0;
    state->midi_editor_ui.instrument_menu_expanded_category = -1;
    return SDL_PointInRect(&point, &layout.panel_rect);
}

static bool instrument_panel_handle_scroll(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_MOUSEWHEEL ||
        !state->midi_editor_ui.instrument_menu_open) {
        return false;
    }
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    SDL_Point point = {state->mouse_x, state->mouse_y};
    if (!SDL_PointInRect(&point, &layout.preset_menu_rect)) {
        return instrument_panel_point_in_panel(state, state->mouse_x, state->mouse_y);
    }
    int delta = event->wheel.y != 0 ? event->wheel.y : event->wheel.x;
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    state->midi_editor_ui.instrument_menu_scroll_row =
        midi_preset_browser_scroll_delta(&layout.preset_browser,
                                         state->midi_editor_ui.instrument_menu_scroll_row,
                                         delta);
    return true;
}

// Releases only this gesture's reservation and retains a failed cancellation for retry.
static bool instrument_panel_finish_param_drag(AppState* state, bool commit) {
    if (!state || !state->midi_editor_ui.instrument_gesture.active) return false;
    DawActionResult result = daw_action_finish_instrument(state, &state->midi_editor_ui.instrument_gesture, commit);
    if (result.status != DAW_ACTION_APPLIED && result.status != DAW_ACTION_UNCHANGED)
        undo_manager_reject(&state->undo, result.message);
    if (state->midi_editor_ui.instrument_gesture.active) return true;
    state->midi_editor_ui.instrument_param_drag_active = false;
    state->midi_editor_ui.instrument_param_drag_index = -1;
    state->midi_editor_ui.instrument_param_drag_start_y = 0;
    state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
    return true;
}

bool midi_instrument_panel_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event || !midi_instrument_panel_should_render(state)) {
        return false;
    }
    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            return midi_instrument_panel_input_activate_at(state, event->button.x, event->button.y);
        }
        return instrument_panel_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (instrument_panel_finish_param_drag(state, true)) {
                return true;
            }
        }
        return instrument_panel_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEMOTION:
        if (state->midi_editor_ui.instrument_param_drag_active) {
            instrument_panel_update_param_drag(state, event->motion.y);
            return true;
        }
        return instrument_panel_point_in_panel(state, event->motion.x, event->motion.y);
    case SDL_MOUSEWHEEL:
        return instrument_panel_handle_scroll(state, event);
    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            if (state->midi_editor_ui.instrument_gesture.active) {
                return instrument_panel_finish_param_drag(state, false);
            }
            if (state->midi_editor_ui.instrument_menu_open) {
                state->midi_editor_ui.instrument_menu_open = false;
                state->midi_editor_ui.instrument_menu_scroll_row = 0;
                state->midi_editor_ui.instrument_menu_expanded_category = -1;
            } else {
                state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void midi_instrument_panel_input_update(InputManager* manager,
                                        AppState* state,
                                        bool left_was_down,
                                        bool left_is_down) {
    (void)manager;
    if (!state) {
        return;
    }
    if (state->midi_editor_ui.instrument_gesture.active && !state->midi_editor_ui.instrument_param_drag_active) {
        (void)instrument_panel_finish_param_drag(state, false);
    }
    if (!midi_instrument_panel_should_render(state)) {
        if (state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT &&
            !midi_editor_should_render(state)) {
            state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
        }
        (void)instrument_panel_finish_param_drag(state, false);
        return;
    }
    if (state->midi_editor_ui.instrument_param_drag_active && left_is_down) {
        instrument_panel_update_param_drag(state, state->mouse_y);
    }
    if (left_was_down && !left_is_down) {
        (void)instrument_panel_finish_param_drag(state, true);
    }
}
