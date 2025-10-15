#include "input/input_manager.h"

#include "app_state.h"
#include "engine.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/panes.h"
#include "ui/transport.h"

#include <SDL2/SDL.h>
#include <stdio.h>

void input_manager_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->previous_buttons = 0;
    manager->current_buttons = 0;
    manager->previous_space = false;
    manager->previous_l = false;
}

static void handle_library_drag(AppState* state, bool was_down, bool is_down) {
    if (!state) {
        return;
    }
    if (!was_down && is_down && !state->layout_runtime.drag.active) {
        if (state->library.hovered_index >= 0) {
            state->library.selected_index = state->library.hovered_index;
            state->dragging_library = true;
            state->drag_library_index = state->library.hovered_index;
        }
    }

    if (state->dragging_library && was_down && !is_down) {
        const Pane* timeline = ui_layout_get_pane(state, 1);
        const int mouse_x = state->mouse_x;
        const int mouse_y = state->mouse_y;
        if (timeline &&
            mouse_x >= timeline->rect.x && mouse_x <= timeline->rect.x + timeline->rect.w &&
            mouse_y >= timeline->rect.y && mouse_y <= timeline->rect.y + timeline->rect.h)
        {
            if (state->engine && state->drag_library_index >= 0 &&
                state->drag_library_index < state->library.count) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", state->library.directory,
                         state->library.items[state->drag_library_index].name);
                if (!engine_add_clip(state->engine, path, 0)) {
                    SDL_Log("Failed to add clip: %s", path);
                } else {
                    SDL_Log("Added clip: %s", path);
                }
            }
        }
        state->dragging_library = false;
        state->drag_library_index = -1;
    }
}

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
        if (engine_transport_is_playing(state->engine)) {
            engine_transport_stop(state->engine);
        } else {
            engine_transport_play(state->engine);
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

    handle_library_drag(state, left_was_down, left_is_down);
    handle_transport_controls(state, left_was_down, left_is_down);
    handle_keyboard_shortcuts(manager, state);
}
