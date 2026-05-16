#pragma once

#include <SDL2/SDL.h>

#include "effects/effects_manager.h"
#include "ui/effects_panel_slot.h"
#include "ui/midi_preset_browser.h"

#ifndef FX_PANEL_MAX_TYPES
#define FX_PANEL_MAX_TYPES 64
#endif

#ifndef FX_PANEL_MAX_CATEGORIES
#define FX_PANEL_MAX_CATEGORIES 12
#endif

struct AppState;

#define FX_PANEL_SNAPSHOT_METER_WIDTH 36
#define FX_PANEL_SNAPSHOT_METER_GAP 8
#define FX_PANEL_SNAPSHOT_METER_LABEL_GAP 4
#define FX_PANEL_SNAPSHOT_METER_CLIP_HEIGHT 6
#define FX_PANEL_METER_TICK_COUNT 6
#define FX_PANEL_METER_DB_MIN -60.0f
#define FX_PANEL_METER_DB_MAX 6.0f
#define FX_PANEL_LIST_SCROLLBAR_WIDTH 6
#define FX_PANEL_LIST_SCROLLBAR_HIT_WIDTH 14
#define FX_PANEL_LIST_SCROLLBAR_MIN_THUMB 18

// Layout rectangles for the track snapshot sub-section of the panel.
typedef struct {
    SDL_Rect container_rect;
    SDL_Rect eq_rect;
    SDL_Rect gain_label_rect;
    SDL_Rect gain_rect;
    SDL_Rect gain_hit_rect;
    SDL_Rect pan_label_rect;
    SDL_Rect pan_rect;
    SDL_Rect pan_hit_rect;
    SDL_Rect instrument_button_rect;
    SDL_Rect instrument_menu_rect;
    MidiPresetBrowserLayout instrument_browser;
    SDL_Rect list_rect;
    SDL_Rect list_clip_rect;
    SDL_Rect mute_rect;
    SDL_Rect solo_rect;
    SDL_Rect list_scroll_track;
    SDL_Rect list_scroll_thumb;
    SDL_Rect list_scroll_thumb_hit;
    SDL_Rect meter_rect;
    SDL_Rect meter_clip_rect;
    SDL_Rect meter_tick_rects[FX_PANEL_METER_TICK_COUNT];
    SDL_Rect meter_label_rects[FX_PANEL_METER_TICK_COUNT];
} EffectsPanelTrackSnapshotLayout;

// Pre-computed rectangles + overlay state that define the full effects panel.
typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect dropdown_button_rect;
    SDL_Rect preview_toggle_rect;
    SDL_Rect view_toggle_rect;
    SDL_Rect spec_toggle_rect;
    SDL_Rect target_label_rect;
    SDL_Rect list_rect;
    EffectsPanelTrackSnapshotLayout track_snapshot;
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
#define FX_PANEL_HEADER_HEIGHT 22
#define FX_PANEL_HEADER_BUTTON_HEIGHT 16
#define FX_PANEL_HEADER_BUTTON_GAP 6
#define FX_PANEL_HEADER_BUTTON_PAD_X 6
#define FX_PANEL_HEADER_BUTTON_PAD_Y 2
#define FX_PANEL_BUTTON_SCALE 1.0f
#define FX_PANEL_TITLE_SCALE 1.5f
#define FX_PANEL_MARGIN 16
#define FX_PANEL_COLUMN_GAP 16
#define FX_PANEL_INNER_MARGIN 12
#define FX_PANEL_PARAM_GAP 10
#define FX_PANEL_LIST_ROW_HEIGHT 18
#define FX_PANEL_LIST_ROW_GAP 3
#define FX_PANEL_LIST_PAD 8
#define FX_PANEL_LIST_TEXT_SCALE 1.1f
#define FX_PANEL_SNAPSHOT_EQ_HEIGHT 64
#define FX_PANEL_SNAPSHOT_LABEL_HEIGHT 14
#define FX_PANEL_SNAPSHOT_SLIDER_HEIGHT 6
#define FX_PANEL_SNAPSHOT_SLIDER_HIT_HEIGHT 14
#define FX_PANEL_SNAPSHOT_GAP 4
#define FX_PANEL_SNAPSHOT_LIST_GAP 10
#define FX_PANEL_SNAPSHOT_FOOTER_HEIGHT 18
#define FX_PANEL_SNAPSHOT_BUTTON_GAP 6
#define FX_PANEL_DROPDOWN_ITEM_HEIGHT 22

void effects_panel_init(struct AppState* state);
void effects_panel_refresh_catalog(struct AppState* state);
void effects_panel_sync_from_engine(struct AppState* state);
void effects_panel_ensure_eq_curve_tracks(struct AppState* state, int track_count);
void effects_panel_set_eq_detail_view(struct AppState* state, int view_mode);
// Clears the meter history so meter detail views start from a fresh timeline.
void effects_panel_reset_meter_history(struct AppState* state);
void effects_panel_compute_layout(const struct AppState* state, EffectsPanelLayout* layout);
void effects_panel_render(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
void effects_panel_render_list(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
void effects_panel_render_track_snapshot(SDL_Renderer* renderer, const struct AppState* state, const EffectsPanelLayout* layout);
