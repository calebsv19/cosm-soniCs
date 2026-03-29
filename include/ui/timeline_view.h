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
#define TIMELINE_CONTROLS_HEIGHT 24
#define TIMELINE_RULER_HEIGHT 18

typedef enum {
    TIMELINE_FOLLOW_OFF = 0,
    TIMELINE_FOLLOW_JUMP = 1,
    TIMELINE_FOLLOW_SMOOTH = 2
} TimelineFollowMode;

typedef struct TimelineTrackHeaderLayout {
    SDL_Rect header_rect;
    SDL_Rect mute_rect;
    SDL_Rect solo_rect;
    int text_x;
    int text_max_x;
    int text_y;
} TimelineTrackHeaderLayout;

int timeline_view_controls_height(void);
int timeline_view_controls_height_for_width(int timeline_width);
int timeline_view_ruler_height(void);
int timeline_view_track_header_width(void);
int timeline_view_lane_clip_inset(int track_height);
void timeline_view_compute_lane_clip_rect(int lane_top,
                                          int track_height,
                                          int clip_x,
                                          int clip_w,
                                          SDL_Rect* out_rect);
void timeline_view_compute_track_header_layout(const SDL_Rect* timeline_rect,
                                               int lane_top,
                                               int track_height,
                                               int header_width,
                                               TimelineTrackHeaderLayout* out_layout);

// Renders the timeline view, including clip waveforms and selection states.
void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, struct AppState* state);
