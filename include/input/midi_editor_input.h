#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

bool midi_editor_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
void midi_editor_input_update(struct InputManager* manager, struct AppState* state, bool left_was_down, bool left_is_down);
bool midi_editor_input_qwerty_capturing(const struct AppState* state);
