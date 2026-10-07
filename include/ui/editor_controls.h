#pragma once
#include <SDL2/SDL.h>
#include "kit_ui_surface.h"

struct AppState;
struct InputManager;
// Retains shared discrete-control ownership separately from continuous product gestures.
typedef struct DawEditorControls {
    KitUiSurface surface;
    uint64_t context;
    unsigned generation;
    bool keyboard_focus;
    SDL_Keymod modifiers;
} DawEditorControls;

// Rebuilds semantic controls from the same product layouts used for drawing.
void daw_editor_controls_sync(struct AppState*);
// Routes matching release/focus and invokes the existing product control owner once.
bool daw_editor_controls_event(struct InputManager*, struct AppState*, const SDL_Event*);
// Prevents sampled pointer state from replaying a registered discrete control.
bool daw_editor_controls_at(struct AppState*, int, int);
// Draws only the shared focus marker; existing product controls remain the sole painters.
void daw_editor_controls_draw_focus(SDL_Renderer*, struct AppState*);
