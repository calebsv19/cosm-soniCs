#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
    UI_RESIZE_NONE = 0,
    UI_RESIZE_TRANSPORT,
    UI_RESIZE_LIBRARY,
    UI_RESIZE_TIMELINE_MIXER,
    UI_RESIZE_CORNER_TOP,
    UI_RESIZE_CORNER_BOTTOM
} UIResizeTarget;

typedef struct {
    SDL_Rect rect;
    UIResizeTarget target;
} UIResizeZone;

typedef struct {
    bool active;
    UIResizeTarget target;
    int start_mouse_x;
    int start_mouse_y;
    float start_transport_ratio;
    float start_library_ratio;
    float start_mixer_ratio;
    int start_transport_px;
    int start_library_px;
    int start_mixer_px;
    int start_window_width;
    int start_window_height;
    int start_content_height;
} UIResizeDrag;
