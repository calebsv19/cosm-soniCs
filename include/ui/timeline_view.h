#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

struct AppState;

#define TIMELINE_DEFAULT_VISIBLE_SECONDS 10.0f
#define TIMELINE_MIN_VISIBLE_SECONDS 2.0f
#define TIMELINE_MAX_VISIBLE_SECONDS 120.0f
#define TIMELINE_BORDER_MARGIN   24
#define TIMELINE_SNAP_SECONDS    0.25f
#define TIMELINE_BASE_TRACK_HEIGHT 80
#define TIMELINE_MIN_VERTICAL_SCALE 0.5f
#define TIMELINE_MAX_VERTICAL_SCALE 2.5f
#define TIMELINE_TRACK_HEADER_WIDTH 120
#define TIMELINE_CONTROLS_HEIGHT 40

typedef enum {
    TIMELINE_FOLLOW_OFF = 0,
    TIMELINE_FOLLOW_JUMP = 1,
    TIMELINE_FOLLOW_SMOOTH = 2
} TimelineFollowMode;

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, const struct AppState* state);
