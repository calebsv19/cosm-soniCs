#pragma once

#include <SDL2/SDL.h>

#include "effects/effects_manager.h"
#include "ui/effects_panel_slot.h"

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
    SDL_Rect view_toggle_rect;
    SDL_Rect target_label_rect;
    SDL_Rect list_rect;
    SDL_Rect detail_rect;
    SDL_Rect list_row_rects[FX_MASTER_MAX];
    SDL_Rect list_toggle_rects[FX_MASTER_MAX];
    int list_row_count;
    int column_count;
    EffectsSlotLayout slots[FX_MASTER_MAX];
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

#define FX_PANEL_HEADER_HEIGHT 26
#define FX_PANEL_HEADER_BUTTON_HEIGHT 20
#define FX_PANEL_HEADER_BUTTON_GAP 6
#define FX_PANEL_HEADER_BUTTON_PAD_X 6
#define FX_PANEL_HEADER_BUTTON_PAD_Y 2
#define FX_PANEL_BUTTON_SCALE 1.5f
#define FX_PANEL_TITLE_SCALE 1.5f
#define FX_PANEL_MARGIN 16
#define FX_PANEL_COLUMN_GAP 16
#define FX_PANEL_INNER_MARGIN 12
#define FX_PANEL_PARAM_GAP 10
#define FX_PANEL_LIST_ROW_HEIGHT 22
#define FX_PANEL_LIST_ROW_GAP 4
#define FX_PANEL_LIST_PAD 8
#define FX_PANEL_DROPDOWN_ITEM_HEIGHT 22

void effects_panel_init(struct AppState* state);
void effects_panel_refresh_catalog(struct AppState* state);
void effects_panel_sync_from_engine(struct AppState* state);
void effects_panel_compute_layout(const struct AppState* state, EffectsPanelLayout* layout);
void effects_panel_render(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
void effects_panel_render_list(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
