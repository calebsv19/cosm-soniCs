#pragma once

#include <SDL2/SDL.h>

#include "app_state.h"
#include "ui/timeline_view_internal.h"

void timeline_view_render_track_clip_pass(SDL_Renderer* renderer,
                                          const SDL_Rect* timeline_rect,
                                          AppState* state,
                                          const TimelineTheme* theme,
                                          const EngineTrack* tracks,
                                          int track_count,
                                          int track_y,
                                          int track_height,
                                          int track_spacing,
                                          int header_width,
                                          int content_left,
                                          int content_width,
                                          float window_start,
                                          float window_end,
                                          float pixels_per_second,
                                          int sample_rate);
