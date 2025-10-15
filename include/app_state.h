#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "engine.h"
#include "ui/panes.h"
#include "ui/resize.h"
#include "ui/transport.h"

typedef struct {
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
    UIResizeZone zones[6];
    int zone_count;
    UIResizeDrag drag;
} UILayoutRuntime;

typedef struct {
    Pane panes[4];
    PaneManager pane_manager;
    int pane_count;
    int window_width;
    int window_height;
    int mouse_x;
    int mouse_y;
    Uint32 mouse_buttons;
    bool space_down;
    EngineRuntimeConfig runtime_cfg;
    Engine* engine;
    TransportUI transport_ui;
    UILayoutRuntime layout_runtime;
} AppState;
