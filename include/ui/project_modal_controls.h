#pragma once
#include <SDL2/SDL.h>
#include "kit_ui_surface.h"
struct AppState;
// Keeps only transient modal controls; project selection/loading remains app-owned.
typedef struct DawProjectModalControls {
    KitUiSurface surface;
    unsigned generation;
    int active, selected, width, height;
    unsigned collection_revision;
    const void* engine;
} DawProjectModalControls;
// Defines responsive geometry shared by native painting, controls and row input.
typedef struct DawProjectModalLayout { SDL_Rect box, list, info, field, buttons[6]; } DawProjectModalLayout;
// Computes the visible project-modal rectangles once for each owner.
void daw_project_modal_layout(const struct AppState*, DawProjectModalLayout*);
// Returns one shared layout for the modal renderer and its input owner.
void daw_project_modal_buttons(const struct AppState*, SDL_Rect*, SDL_Rect*);
// Synchronizes modal scope and cancels stale geometry, selection and engine ownership.
void daw_project_modal_controls_sync(struct AppState*);
// Routes modal release activation; action 1 loads and action 2 cancels.
int daw_project_modal_controls_event(struct AppState*, const SDL_Event*, int*);
// Replaces the two old modal button painters using the existing product theme.
void daw_project_modal_controls_draw(SDL_Renderer*, struct AppState*);
