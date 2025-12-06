#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "ui/panes.h"
#include "ui/resize.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"
#include "ui/library_browser.h"

#define TIMELINE_MAX_SELECTION 256

typedef struct EngineSamplerSource EngineSamplerSource;

typedef struct {
    int track_index;
    int clip_index;
} TimelineSelectionEntry;

typedef struct {
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
    UIResizeZone zones[6];
    int zone_count;
    UIResizeDrag drag;
} UILayoutRuntime;

typedef struct {
    bool active;
    bool trimming_left;
    bool trimming_right;
    bool adjusting_fade_in;
    bool adjusting_fade_out;
    int track_index;
    int clip_index;
    int destination_track_index;
    int start_mouse_x;
    float start_mouse_seconds;
    float start_right_seconds;
    float current_start_seconds;
    float current_duration_seconds;
    uint64_t initial_start_frames;
    uint64_t initial_offset_frames;
    uint64_t initial_duration_frames;
    uint64_t clip_total_frames;
    uint64_t initial_fade_in_frames;
    uint64_t initial_fade_out_frames;
    bool multi_move;
    int multi_clip_count;
    EngineSamplerSource* multi_samplers[TIMELINE_MAX_SELECTION];
    int multi_initial_track[TIMELINE_MAX_SELECTION];
    uint64_t multi_initial_start[TIMELINE_MAX_SELECTION];
} TimelineDragState;

typedef struct {
    bool visible;
    int track_index;
    int clip_index;
    char name[ENGINE_CLIP_NAME_MAX];
    float gain;
    bool editing_name;
    int name_cursor;
    bool adjusting_gain;
    bool adjusting_fade_in;
    bool adjusting_fade_out;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
} ClipInspectorState;

typedef enum {
    FX_PANEL_OVERLAY_CLOSED = 0,
    FX_PANEL_OVERLAY_CATEGORIES,
    FX_PANEL_OVERLAY_EFFECTS
} EffectsPanelOverlayLayer;

#define FX_PANEL_MAX_TYPES 64
#define FX_PANEL_MAX_CATEGORIES 12

typedef struct {
    FxTypeId type_id;
    char name[32];
    uint32_t param_count;
    char param_names[FX_MAX_PARAMS][32];
    float param_defaults[FX_MAX_PARAMS];
    float param_min[FX_MAX_PARAMS];
    float param_max[FX_MAX_PARAMS];
} FxTypeUIInfo;

typedef struct {
    char name[32];
    int type_indices[FX_PANEL_MAX_TYPES];
    int type_count;
} FxCategoryUIInfo;

typedef struct {
    FxInstId id;
    FxTypeId type_id;
    uint32_t param_count;
    float param_values[FX_MAX_PARAMS];
    bool enabled;
} FxSlotUIState;

typedef struct {
    bool initialized;
    EffectsPanelOverlayLayer overlay_layer;
    int hovered_category_index;
    int hovered_effect_index;
    int active_category_index;
    int highlighted_slot_index;
    bool dragging_slider;
    int active_slot_index;
    int active_param_index;
    int overlay_scroll_index;
    FxTypeUIInfo types[FX_PANEL_MAX_TYPES];
    int type_count;
    FxCategoryUIInfo categories[FX_PANEL_MAX_CATEGORIES];
    int category_count;
    FxSlotUIState chain[FX_MASTER_MAX];
    int chain_count;
} EffectsPanelState;

typedef struct {
    SDL_Rect add_rect;
    SDL_Rect remove_rect;
    bool add_hovered;
    bool remove_hovered;
    SDL_Rect loop_toggle_rect;
    SDL_Rect loop_start_rect;
    SDL_Rect loop_end_rect;
    bool loop_toggle_hovered;
    bool loop_start_hovered;
    bool loop_end_hovered;
    bool adjusting_loop_start;
    bool adjusting_loop_end;
} TimelineControlsUI;

typedef struct {
    bool editing;
    int track_index;
    char buffer[ENGINE_CLIP_NAME_MAX];
    int cursor;
} TrackNameEditor;

typedef struct {
    FxTypeId type_id;
    bool enabled;
    uint32_t param_count;
    float param_values[FX_MAX_PARAMS];
} PendingMasterFx;

typedef struct AppState AppState;

struct AppState {
    Pane panes[4];
    PaneManager pane_manager;
    int pane_count;
    int window_width;
    int window_height;
    int mouse_x;
    int mouse_y;
    EngineRuntimeConfig runtime_cfg;
    Engine* engine;
    TransportUI transport_ui;
    UILayoutRuntime layout_runtime;
    LibraryBrowser library;
    int drag_library_index;
    bool dragging_library;
    InputManager input_manager;
    int active_track_index;
    int selected_track_index;
    int selected_clip_index;
    int selection_count;
    TimelineSelectionEntry selection[TIMELINE_MAX_SELECTION];
    TimelineDragState timeline_drag;
    ClipInspectorState inspector;
    EffectsPanelState effects_panel;
    TimelineControlsUI timeline_controls;
    TrackNameEditor track_name_editor;
    float timeline_visible_seconds;
    float timeline_vertical_scale;
    bool timeline_drop_active;
    float timeline_drop_seconds;
    float timeline_drop_seconds_snapped;
    float timeline_drop_preview_duration;
    char timeline_drop_label[LIBRARY_NAME_MAX];
    bool timeline_show_all_grid_lines;
    int timeline_drop_track_index;
    bool loop_enabled;
    uint64_t loop_start_frame;
    uint64_t loop_end_frame;
    bool loop_restart_pending;
    bool bounce_requested;
    bool bounce_active;
    uint64_t bounce_progress_frames;
    uint64_t bounce_total_frames;
    uint64_t bounce_start_frame;
    uint64_t bounce_end_frame;
    bool engine_logging_enabled;
    bool cache_logging_enabled;
    bool timing_logging_enabled;
    PendingMasterFx pending_master_fx[FX_MASTER_MAX];
    int pending_master_fx_count;
    bool pending_master_fx_dirty;
};
