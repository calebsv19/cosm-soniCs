#pragma once
#include "kit_pane_composition.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
struct AppState;
struct Pane;
// Saves the enclosing native clip for a synchronous pane paint pass.
typedef struct DawPaneClip {
    SDL_Rect previous;
    SDL_bool enabled;
} DawPaneClip;
// Composes the fixed product panes using stable slot identities and existing header policy.
CoreResult daw_panes_compose(const struct AppState*, KitPaneComposition*);
// Resolves a pane's content without duplicating shared header/inset geometry.
SDL_Rect daw_pane_content_rect(const struct Pane*);
// Intersects a visible pane region with the enclosing render clip.
bool daw_pane_clip_begin(SDL_Renderer*, const struct AppState*, int, KitPaneRegion, DawPaneClip*);
// Restores the exact enclosing clip after a synchronous product painter.
void daw_pane_clip_end(SDL_Renderer*, const DawPaneClip*);
// Cancels a divider gesture and requires release before another divider press.
void daw_pane_resize_cancel(struct AppState*);
// Cancels stale divider geometry on window invalidation or owner takeover.
void daw_pane_lifecycle_event(struct AppState*, const SDL_Event*);
