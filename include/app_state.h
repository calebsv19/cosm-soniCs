#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "engine.h"
#include "input/input_manager.h"
#include "ui/panes.h"
#include "ui/resize.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"
#include "ui/library_browser.h"

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
    int track_index;
    int clip_index;
    int start_mouse_x;
    float start_mouse_seconds;
    float start_right_seconds;
    uint64_t initial_start_frames;
    uint64_t initial_offset_frames;
    uint64_t initial_duration_frames;
    uint64_t clip_total_frames;
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
} ClipInspectorState;

typedef struct {
    SDL_Rect add_rect;
    SDL_Rect remove_rect;
    bool add_hovered;
    bool remove_hovered;
} TimelineControlsUI;

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
    int selected_track_index;
    int selected_clip_index;
    TimelineDragState timeline_drag;
    ClipInspectorState inspector;
    TimelineControlsUI timeline_controls;
    float timeline_visible_seconds;
    float timeline_vertical_scale;
    bool timeline_drop_active;
    float timeline_drop_seconds;
    float timeline_drop_seconds_snapped;
    float timeline_drop_preview_duration;
    char timeline_drop_label[LIBRARY_NAME_MAX];
    bool timeline_show_all_grid_lines;
    int timeline_drop_track_index;
};
