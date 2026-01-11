#pragma once

struct AppState;

float timeline_get_snap_interval_seconds(const struct AppState* state, float visible_seconds);
float timeline_snap_seconds_to_grid(const struct AppState* state, float seconds, float visible_seconds);
