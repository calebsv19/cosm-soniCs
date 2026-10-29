#pragma once
#include <SDL.h>
struct AppState;
// Displays selected-point units, exact timing, curve and discoverable precision shortcuts.
void automation_status_draw(SDL_Renderer* renderer, const SDL_Rect* rect, struct AppState* state);
