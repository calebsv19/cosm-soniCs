#pragma once

#include <stdbool.h>
#include <SDL2/SDL.h>

struct AppState;
struct InputManager;

void transport_input_init(struct InputManager* manager);
void transport_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
void transport_input_update(struct InputManager* manager, struct AppState* state);
