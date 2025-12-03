#pragma once

#include <SDL2/SDL.h>

#include "effects/effects_manager.h"

#ifndef FX_PANEL_MAX_TYPES
#define FX_PANEL_MAX_TYPES 64
#endif

#ifndef FX_PANEL_MAX_CATEGORIES
#define FX_PANEL_MAX_CATEGORIES 12
#endif

struct AppState;

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect dropdown_button_rect;
    int column_count;
    SDL_Rect column_rects[FX_MASTER_MAX];
    SDL_Rect header_rects[FX_MASTER_MAX];
    SDL_Rect remove_button_rects[FX_MASTER_MAX];
    SDL_Rect param_slider_rects[FX_MASTER_MAX][FX_MAX_PARAMS];
    SDL_Rect param_label_rects[FX_MASTER_MAX][FX_MAX_PARAMS];
    bool overlay_visible;
    SDL_Rect overlay_rect;
    SDL_Rect overlay_header_rect;
    SDL_Rect overlay_back_rect;
    int overlay_item_count;
    SDL_Rect overlay_item_rects[FX_PANEL_MAX_TYPES];
    int overlay_item_order[FX_PANEL_MAX_TYPES];
    int overlay_total_items;
    int overlay_visible_count;
    bool overlay_has_scrollbar;
    SDL_Rect overlay_scrollbar_track;
    SDL_Rect overlay_scrollbar_thumb;
} EffectsPanelLayout;

#define FX_PANEL_DROPDOWN_ITEM_HEIGHT 22

void effects_panel_init(struct AppState* state);
void effects_panel_refresh_catalog(struct AppState* state);
void effects_panel_sync_from_engine(struct AppState* state);
void effects_panel_compute_layout(const struct AppState* state, EffectsPanelLayout* layout);
void effects_panel_render(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
