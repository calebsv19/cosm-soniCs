#pragma once

#include <stdbool.h>

struct AppState;
struct Pane;

typedef struct TimelineGeometry {
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
bool timeline_compute_geometry(const struct AppState* state, const struct Pane* timeline, TimelineGeometry* out_geom);
float timeline_x_to_seconds(const TimelineGeometry* geom, int x);
int timeline_track_at_position(const struct AppState* state, int y, int track_height, int track_spacing);
