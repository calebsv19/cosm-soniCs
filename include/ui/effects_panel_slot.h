#pragma once

#include <SDL2/SDL.h>
#include "effects/effects_manager.h"

typedef struct {
    float scroll;
    float scroll_max;
    bool dragging;
    int drag_start_y;
    float drag_start_val;
} EffectsSlotRuntime;

typedef struct {
    SDL_Rect column_rect;
    SDL_Rect header_rect;
    SDL_Rect remove_rect;
    SDL_Rect body_rect;
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
void effects_slot_compute_layout(struct EffectsPanelState* panel,
                                 int slot_index,
                                 const SDL_Rect* column_rect,
                                 int header_height,
                                 int inner_margin,
                                 int default_param_gap,
                                 EffectsSlotLayout* out_layout);
void effects_slot_render(SDL_Renderer* renderer,
                         const struct AppState* state,
                         int slot_index,
                         const EffectsSlotLayout* slot_layout,
                         bool remove_highlight,
                         SDL_Color label_color,
                         SDL_Color text_dim);
