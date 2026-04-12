#pragma once

#include <SDL2/SDL.h>

#include "app_state.h"
#include "ui/timeline_view_internal.h"

void timeline_view_render_runtime_overlays(SDL_Renderer* renderer,
                                           const SDL_Rect* rect,
                                           AppState* state,
                                           const TimelineTheme* theme,
                                           int sample_rate,
                                           float window_start,
                                           float window_end,
                                           float visible_seconds,
                                           int content_left,
                                           int content_width,
                                           int track_y,
                                           int track_height,
                                           int track_spacing,
                                           int track_count,
                                           int header_width,
                                           float pixels_per_second);
