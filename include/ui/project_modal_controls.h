#pragma once
#include <SDL2/SDL.h>
#include "kit_ui_surface.h"
struct AppState;
// Keeps only transient modal controls; project selection/loading remains app-owned.
typedef struct DawProjectModalControls {
    KitUiSurface surface;
    unsigned generation;
    int active, selected, width, height;
    const void* engine;
} DawProjectModalControls;
// Returns one shared layout for the modal renderer and its input owner.
void daw_project_modal_buttons(const struct AppState*, SDL_Rect*, SDL_Rect*);
// Synchronizes modal scope and cancels stale geometry, selection and engine ownership.
void daw_project_modal_controls_sync(struct AppState*);
// Routes modal release activation; action 1 loads and action 2 cancels.
int daw_project_modal_controls_event(struct AppState*, const SDL_Event*, int*);
// Replaces the two old modal button painters using the existing product theme.
void daw_project_modal_controls_draw(SDL_Renderer*, struct AppState*);
