#include "input/input_manager.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/timeline_input.h"
#include "input/transport_input.h"
#include "input/inspector_input.h"
#include "input/timeline_selection.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/panes.h"
#include "ui/transport.h"

#include <SDL2/SDL.h>

static void handle_transport_controls(AppState* state, bool was_down, bool is_down) {
    if (!state || !state->engine) {
        return;
    }
    if (!was_down && is_down) {
        if (transport_ui_click_play(&state->transport_ui, state->mouse_x, state->mouse_y)) {
            engine_transport_play(state->engine);
        } else if (transport_ui_click_stop(&state->transport_ui, state->mouse_x, state->mouse_y)) {
            engine_transport_stop(state->engine);
        }
    }
}

static void handle_keyboard_shortcuts(InputManager* manager, AppState* state) {
    if (!manager || !state || !state->engine) {
        return;
    }

    const Uint8* keys = SDL_GetKeyboardState(NULL);
    bool space_now = keys[SDL_SCANCODE_SPACE] != 0;
    if (space_now && !manager->previous_space) {
        bool shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;
        if (shift_down) {
            bool was_playing = engine_transport_is_playing(state->engine);
            uint64_t target_frame = 0;
            if (state->loop_enabled && state->loop_end_frame > state->loop_start_frame) {
                target_frame = state->loop_start_frame;
            }
            engine_transport_seek(state->engine, target_frame);
            if (was_playing) {
                engine_transport_play(state->engine);
            }
        } else {
            bool was_playing = engine_transport_is_playing(state->engine);
            if (was_playing) {
                engine_transport_stop(state->engine);
            } else {
                engine_transport_play(state->engine);
            }
        }
    }
    manager->previous_space = space_now;

    bool l_now = keys[SDL_SCANCODE_L] != 0;
    if (l_now && !manager->previous_l) {
        if (state->library.selected_index >= 0 &&
            state->library.selected_index < state->library.count) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", state->library.directory,
                     state->library.items[state->library.selected_index].name);
            if (!engine_add_clip(state->engine, path, 0)) {
                SDL_Log("Failed to load clip: %s", path);
            }
        } else if (!engine_load_wav(state->engine, "config/test.wav")) {
            SDL_Log("Failed to load test clip");
        }
    }
    manager->previous_l = l_now;

    bool delete_now = keys[SDL_SCANCODE_DELETE] != 0 || keys[SDL_SCANCODE_BACKSPACE] != 0;
    if (!state->inspector.editing_name && delete_now && !manager->previous_delete) {
        timeline_selection_delete(state);
    }
    manager->previous_delete = delete_now;

    bool c_now = keys[SDL_SCANCODE_C] != 0;
    if (c_now && !manager->previous_c) {
        bool new_state = !state->loop_enabled;
        if (new_state && state->loop_end_frame <= state->loop_start_frame) {
            const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
            int sample_rate = cfg ? cfg->sample_rate : 0;
            uint64_t default_len = sample_rate > 0 ? (uint64_t)sample_rate : 48000;
            if (default_len == 0) {
                default_len = 48000;
            }
            state->loop_end_frame = state->loop_start_frame + default_len;
        }
        state->loop_enabled = new_state;
        state->loop_restart_pending = false;
        engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame);
    }
    manager->previous_c = c_now;

    bool enter_now = keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_KP_ENTER] != 0;
    if (enter_now && !manager->previous_enter) {
        bool was_playing = engine_transport_is_playing(state->engine);
        bool loop_active = state->loop_enabled && state->loop_end_frame > state->loop_start_frame;
        if (loop_active) {
            state->loop_restart_pending = true;
            engine_transport_set_loop(state->engine, false, state->loop_start_frame, state->loop_end_frame);
        }
        engine_transport_seek(state->engine, 0);
        if (was_playing) {
            engine_transport_play(state->engine);
        }
    }
    manager->previous_enter = enter_now;

    bool f7_now = keys[SDL_SCANCODE_F7] != 0;
    if (f7_now && !manager->previous_f7) {
        state->engine_logging_enabled = !state->engine_logging_enabled;
        state->runtime_cfg.enable_engine_logs = state->engine_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f7 = f7_now;

    bool f8_now = keys[SDL_SCANCODE_F8] != 0;
    if (f8_now && !manager->previous_f8) {
        state->cache_logging_enabled = !state->cache_logging_enabled;
        state->runtime_cfg.enable_cache_logs = state->cache_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f8 = f8_now;

    bool f9_now = keys[SDL_SCANCODE_F9] != 0;
    if (f9_now && !manager->previous_f9) {
        state->timing_logging_enabled = !state->timing_logging_enabled;
        state->runtime_cfg.enable_timing_logs = state->timing_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f9 = f9_now;
}

void input_manager_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->previous_buttons = 0;
    manager->current_buttons = 0;
    manager->previous_space = false;
    manager->previous_l = false;
    manager->previous_delete = false;
    manager->previous_enter = false;
    manager->previous_c = false;
    manager->previous_f7 = false;
    manager->previous_f8 = false;
    manager->previous_f9 = false;
    manager->last_click_ticks = 0;
    manager->last_click_track = -1;
    manager->last_click_clip = -1;
    manager->last_header_click_ticks = 0;
    manager->last_header_click_track = -1;
    manager->prev_horiz_slider_down = false;
    manager->prev_vert_slider_down = false;

    timeline_input_init(manager);
    transport_input_init(manager);
}

void input_manager_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event) {
        return;
    }

    transport_input_handle_event(manager, state, event);
    inspector_input_handle_event(manager, state, event);
    timeline_input_handle_event(manager, state, event);
}

void input_manager_update(InputManager* manager, AppState* state) {
    if (!manager || !state) {
        return;
    }

    int mouse_x = 0;
    int mouse_y = 0;
    Uint32 prev_buttons = manager->current_buttons;
    Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

    manager->previous_buttons = prev_buttons;
    manager->current_buttons = buttons;

    ui_layout_handle_pointer(state, prev_buttons, buttons, mouse_x, mouse_y);
    if (state->layout_runtime.drag.active) {
        state->dragging_library = false;
        state->drag_library_index = -1;
    }

    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;

    pane_manager_update_hover(&state->pane_manager, mouse_x, mouse_y);
    transport_ui_update_hover(&state->transport_ui, mouse_x, mouse_y);
    ui_layout_handle_hover(state, mouse_x, mouse_y);

    bool left_was_down = (manager->previous_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool left_is_down = (manager->current_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    transport_input_update(manager, state);
    timeline_input_update(manager, state, left_was_down, left_is_down);

    handle_transport_controls(state, left_was_down, left_is_down);
    handle_keyboard_shortcuts(manager, state);

    if (state->loop_restart_pending) {
        if (!state->loop_enabled || state->loop_end_frame <= state->loop_start_frame) {
            state->loop_restart_pending = false;
        } else {
            uint64_t frame = engine_get_transport_frame(state->engine);
            if (frame >= state->loop_start_frame) {
                engine_transport_set_loop(state->engine, true, state->loop_start_frame, state->loop_end_frame);
                state->loop_restart_pending = false;
            }
        }
    }

    if (state->inspector.adjusting_gain) {
        inspector_input_handle_gain_drag(state, state->mouse_x);
    }

    inspector_input_sync(state);
}
