#include "input/timeline/timeline_input_mouse.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "input/timeline/timeline_drop.h"
#include "input/timeline/timeline_input_mouse_click.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "input/timeline/timeline_input_mouse_scroll.h"
#include <SDL2/SDL.h>

void timeline_input_mouse_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event || !state->engine) {
        return;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        timeline_input_mouse_handle_scroll(manager, state, event);
        return;
    }

    timeline_input_mouse_handle_click_event(manager, state, event);
}

void timeline_input_mouse_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    timeline_drop_handle_library_drag(manager, state, was_down, is_down);
    timeline_drop_update_hint(state);
    timeline_input_mouse_click_update(manager, state, was_down, is_down);
    timeline_input_mouse_drag_update(manager, state, was_down, is_down);
}
