#pragma once

#include <stdbool.h>

struct AppState;
struct TimelineGeometry;

bool timeline_input_mouse_handle_tempo_overlay(struct AppState* state,
                                               const struct TimelineGeometry* geom,
                                               int sample_rate,
                                               bool was_down,
                                               bool is_down);
