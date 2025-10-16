#pragma once

#include <stdbool.h>
#include <SDL2/SDL.h>

struct AppState;
struct InputManager;

void timeline_input_init(struct InputManager* manager);
void timeline_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
void timeline_input_update(struct InputManager* manager, struct AppState* state, bool was_down, bool is_down);
