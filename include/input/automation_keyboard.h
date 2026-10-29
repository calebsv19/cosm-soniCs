#pragma once
#include <stdbool.h>
#include <SDL.h>
struct AppState;
// Handles selected-point precision, curve, boundary and deletion keys through shared commands.
bool automation_keyboard_handle(struct AppState* state, const SDL_Event* event);
