#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

bool timeline_input_keyboard_handle_event(struct InputManager* manager,
                                          struct AppState* state,
                                          const SDL_Event* event);
