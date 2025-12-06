#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;
struct Pane;

bool library_input_is_editing(const struct AppState* state);
void library_input_start_edit(struct AppState* state, const struct Pane* library_pane, int mouse_x);
bool library_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
