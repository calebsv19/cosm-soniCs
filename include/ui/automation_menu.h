#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
struct AppState;
// Opens a region-aware automation selector using the current toolbar anchor.
void automation_menu_open(struct AppState* state);
// Captures menu pointer and keyboard input before underlying editor controls.
bool automation_menu_event(struct AppState* state, const SDL_Event* event);
// Paints the selector above editor panes using persistent target labels.
void automation_menu_render(SDL_Renderer* renderer, struct AppState* state);
// Returns the visible bounds of a target for rendering and interaction verification.
SDL_Rect automation_menu_target_rect(const struct AppState* state, int target);
