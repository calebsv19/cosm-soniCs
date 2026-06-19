#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

bool project_modal_input_active(const struct AppState* state);
void project_modal_input_open_save_prompt(struct AppState* state);
void project_modal_input_close_save_prompt(struct AppState* state);
bool project_modal_input_open_load_modal(struct AppState* state);
void project_modal_input_close_load_modal(struct AppState* state);
bool project_modal_input_handle_event(struct AppState* state, const SDL_Event* event);
