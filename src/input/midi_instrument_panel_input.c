#include "input/midi_instrument_panel_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"

#include <SDL2/SDL.h>

static bool instrument_panel_point_in_panel(const AppState* state, int x, int y) {
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &layout.panel_rect);
}

static float instrument_panel_clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
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
    float range = spec.max_value - spec.min_value;
    float delta = (float)(state->midi_editor_ui.instrument_param_drag_start_y - y);
    float value = state->midi_editor_ui.instrument_param_drag_start_value + (delta / 140.0f) * range;
    return instrument_panel_clamp_float(value, spec.min_value, spec.max_value);
}

static float instrument_panel_current_param_value(const MidiEditorSelection* selection, int param_index) {
    if (!selection || !selection->clip || param_index < 0) {
        return 0.0f;
    }
    EngineInstrumentParams params = engine_clip_midi_instrument_params(selection->clip);
    return engine_instrument_params_get(params, (EngineInstrumentParamId)param_index);
}

static bool instrument_panel_apply_param(AppState* state,
                                         const MidiEditorSelection* selection,
                                         const MidiInstrumentPanelLayout* layout,
                                         int param_index,
                                         float value) {
    if (!state || !selection || !selection->clip || !layout ||
        param_index < 0 || param_index >= layout->instrument_param_count) {
        return false;
    }
    return engine_clip_midi_set_instrument_param(state->engine,
                                                selection->track_index,
                                                selection->clip_index,
                                                (EngineInstrumentParamId)param_index,
                                                value);
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
    for (int i = 0; i < layout->preset_menu_item_count; ++i) {
        if (SDL_PointInRect(&point, &layout->preset_menu_item_rects[i])) {
            EngineInstrumentPresetId preset = engine_instrument_preset_clamp((EngineInstrumentPresetId)i);
            (void)engine_clip_midi_set_instrument_preset(state->engine,
                                                         selection->track_index,
                                                         selection->clip_index,
                                                         preset);
            state->midi_editor_ui.instrument_menu_open = false;
            return true;
        }
    }
    if (!SDL_PointInRect(&point, &layout->preset_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = false;
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
            state->midi_editor_ui.instrument_param_drag_active = true;
            state->midi_editor_ui.instrument_param_drag_index = i;
            state->midi_editor_ui.instrument_param_drag_start_y = y;
            state->midi_editor_ui.instrument_param_drag_start_value =
                instrument_panel_current_param_value(selection, i);
            state->midi_editor_ui.instrument_menu_open = false;
            return true;
        }
    }
    return false;
}

static bool instrument_panel_update_param_drag(AppState* state, int y) {
    if (!state || !state->midi_editor_ui.instrument_param_drag_active) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_selection(state, &selection)) {
        return false;
    }
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    int param_index = state->midi_editor_ui.instrument_param_drag_index;
    float value = instrument_panel_param_value_from_drag(state, param_index, y);
    return instrument_panel_apply_param(state,
                                        &selection,
                                        &layout,
                                        param_index,
                                        value);
}

static bool instrument_panel_handle_left_down(AppState* state, int x, int y) {
    if (!midi_instrument_panel_should_render(state)) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_selection(state, &selection)) {
        return false;
    }
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(state, &layout);
    SDL_Point point = {x, y};

    if (instrument_panel_handle_preset_menu_click(state, &selection, &layout, x, y)) {
        return true;
    }
    if (SDL_PointInRect(&point, &layout.notes_button_rect)) {
        state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        state->midi_editor_ui.instrument_param_drag_start_y = 0;
        state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
        return true;
    }
    if (SDL_PointInRect(&point, &layout.preset_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = !state->midi_editor_ui.instrument_menu_open;
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
    return SDL_PointInRect(&point, &layout.panel_rect);
}

bool midi_instrument_panel_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event || !midi_instrument_panel_should_render(state)) {
        return false;
    }
    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            return instrument_panel_handle_left_down(state, event->button.x, event->button.y);
        }
        return instrument_panel_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            bool was_dragging_param = state->midi_editor_ui.instrument_param_drag_active;
            state->midi_editor_ui.instrument_param_drag_active = false;
            state->midi_editor_ui.instrument_param_drag_index = -1;
            state->midi_editor_ui.instrument_param_drag_start_y = 0;
            state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
            if (was_dragging_param) {
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
    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            if (state->midi_editor_ui.instrument_menu_open) {
                state->midi_editor_ui.instrument_menu_open = false;
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
    if (!midi_instrument_panel_should_render(state)) {
        if (state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT &&
            !midi_editor_should_render(state)) {
            state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
        }
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        state->midi_editor_ui.instrument_param_drag_start_y = 0;
        state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
        return;
    }
    if (state->midi_editor_ui.instrument_param_drag_active && left_is_down) {
        instrument_panel_update_param_drag(state, state->mouse_y);
    }
    if (left_was_down && !left_is_down) {
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        state->midi_editor_ui.instrument_param_drag_start_y = 0;
        state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;
    }
}
