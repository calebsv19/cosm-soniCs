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
};
