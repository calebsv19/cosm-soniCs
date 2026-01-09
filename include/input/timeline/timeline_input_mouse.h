#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

void timeline_input_mouse_handle_event(struct InputManager* manager,
                                       struct AppState* state,
                                       const SDL_Event* event);
void timeline_input_mouse_update(struct InputManager* manager,
                                 struct AppState* state,
                                 bool was_down,
                                 bool is_down);
