#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Pane Pane;

typedef struct {
    Pane* panes;
    int count;
    Pane* hovered;
} PaneManager;

// Defines DAW UI invalidation reason bits used by pane and frame invalidation state.
typedef enum DawRenderInvalidationReason {
    DAW_RENDER_INVALIDATION_NONE = 0u,
    DAW_RENDER_INVALIDATION_INPUT = 1u << 0,
    DAW_RENDER_INVALIDATION_LAYOUT = 1u << 1,
    DAW_RENDER_INVALIDATION_THEME = 1u << 2,
    DAW_RENDER_INVALIDATION_CONTENT = 1u << 3,
    DAW_RENDER_INVALIDATION_OVERLAY = 1u << 4,
    DAW_RENDER_INVALIDATION_RESIZE = 1u << 5,
    DAW_RENDER_INVALIDATION_BACKGROUND = 1u << 6
} DawRenderInvalidationReason;

struct Pane {
    SDL_Rect rect;
    SDL_Color border_color;
    SDL_Color fill_color;
    const char* title;
    bool drawTitle;
    bool visible;
    bool highlighted;
    bool dirty;
    uint32_t dirty_reasons;
    uint64_t last_render_frame_id;
};

void pane_manager_init(PaneManager* mgr, Pane* panes, int count);
void pane_manager_update_hover(PaneManager* mgr, int mouse_x, int mouse_y);
// Returns the top-most visible pane containing the provided coordinates.
Pane* pane_manager_hit_test(PaneManager* mgr, int mouse_x, int mouse_y);
// Returns number of panes currently marked dirty.
int pane_manager_count_dirty(const PaneManager* mgr);
// Marks one pane dirty and accumulates reason bits.
void pane_mark_dirty(Pane* pane, uint32_t reason_bits);
// Clears dirty state for one pane.
void pane_clear_dirty(Pane* pane);
