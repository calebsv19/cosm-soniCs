#pragma once

#include <SDL2/SDL.h>
#include "effects/effects_manager.h"

#define FX_PANEL_PREVIEW_HISTORY 256
#define FX_PANEL_PREVIEW_HEIGHT 90
#define FX_PANEL_PREVIEW_COLLAPSED_HEIGHT 18
#define FX_PANEL_PREVIEW_GAP 6

typedef struct {
    float scroll;
    float scroll_max;
    bool dragging;
    int drag_start_y;
    float drag_start_val;
} EffectsSlotRuntime;

// Holds preview state and history samples for a single effect slot.
typedef struct {
    FxInstId fx_id;
    bool open;
    float history[FX_PANEL_PREVIEW_HISTORY];
    int history_write;
    bool history_filled;
} EffectsPanelPreviewSlotState;

typedef struct {
    SDL_Rect column_rect;
    SDL_Rect header_rect;
    SDL_Rect toggle_rect;
    SDL_Rect remove_rect;
    SDL_Rect body_rect;
    SDL_Rect preview_rect;
    SDL_Rect preview_toggle_rect;
    SDL_Rect label_rects[FX_MAX_PARAMS];
    SDL_Rect slider_rects[FX_MAX_PARAMS];
    SDL_Rect value_rects[FX_MAX_PARAMS];
    SDL_Rect mode_rects[FX_MAX_PARAMS];
    SDL_Rect scrollbar_track;
    SDL_Rect scrollbar_thumb;
    uint32_t param_count;
    int block_height;
    int param_gap;
} EffectsSlotLayout;

struct AppState;
struct EffectsPanelState;

void effects_slot_reset_runtime(EffectsSlotRuntime* runtime);
void effects_slot_render(SDL_Renderer* renderer,
                         const struct AppState* state,
                         int slot_index,
                         const EffectsSlotLayout* slot_layout,
                         bool remove_highlight,
                         bool toggle_highlight,
                         bool selected,
                         SDL_Color label_color,
                         SDL_Color text_dim);
