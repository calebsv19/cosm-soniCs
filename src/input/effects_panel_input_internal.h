#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

bool effects_panel_input_handle_mouse_wheel(struct AppState* state, const SDL_Event* event);
void effects_panel_input_update_state(struct InputManager* manager,
                                      struct AppState* state,
                                      bool left_was_down,
                                      bool left_is_down);
