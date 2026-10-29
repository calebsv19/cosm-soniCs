#pragma once

#include <stdbool.h>
#include <SDL2/SDL.h>

struct AppState;
struct Pane;
struct TimelineTrackHeaderLayout;

// Shares timeline lane, viewport and navigation geometry between painting and input.
typedef struct TimelineGeometry {
    SDL_Rect viewport;
    SDL_Rect scrollbar;
    SDL_Rect scroll_thumb;
    float vertical_scroll;
    float vertical_max;
    int content_left;
    int content_width;
    int track_top;
    int track_height;
    int track_spacing;
    int header_width;
    float visible_seconds;
    float window_start_seconds;
    float pixels_per_second;
} TimelineGeometry;

float timeline_total_seconds(const struct AppState* state);
// Compute scrollable bounds for the timeline window based on visible span.
void timeline_get_scroll_bounds(const struct AppState* state, float visible_seconds, float* out_min_start, float* out_max_start);
bool timeline_compute_geometry(const struct AppState* state, const struct Pane* timeline, TimelineGeometry* out_geom);
float timeline_x_to_seconds(const TimelineGeometry* geom, int x);
int timeline_seconds_to_x(const TimelineGeometry* geom, float seconds);
int timeline_track_at_position(const struct AppState* state, int y, int track_height, int track_spacing);

// Resolves a lane origin through the visible hierarchy; hidden rows return an off-screen sentinel.
int timeline_track_lane_y(const struct AppState*, int track, int origin, int height, int spacing);
// Shares indented header and fold geometry across painting and input.
void timeline_track_header_layout(const struct AppState*, int track, const SDL_Rect*, int y, int height, int width, struct TimelineTrackHeaderLayout*);
// Clamps the vertical offset to the current layout and live row count.
void timeline_set_vertical_scroll(struct AppState* state, float pixels);
// Reveals a selected track with the smallest necessary vertical movement.
void timeline_reveal_track(struct AppState* state, int track_index);
// Resizes rows while keeping the pointer's fractional row position fixed.
void timeline_zoom_rows(struct AppState* state, float scale, int anchor_y);
