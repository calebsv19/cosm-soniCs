#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "app_state.h"
#include "ui/timeline_view_internal.h"

typedef struct TimelineViewTextMetricsSnapshot {
    int text_h_1x;
    int clip_label_pad_x;
    int overlay_label_pad_x;
    int label_pad_y;
    int overlay_badge_gap;
    int overlay_edge_pad;
} TimelineViewTextMetricsSnapshot;

int timeline_view_controls_compute_layout(int timeline_x,
                                          int timeline_y,
                                          int timeline_width,
                                          TimelineControlsUI* out_controls);

void timeline_view_draw_button(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const char* label,
                               bool hovered,
                               bool enabled,
                               const TimelineTheme* theme);

void timeline_view_get_text_metrics_snapshot(TimelineViewTextMetricsSnapshot* out_metrics);
