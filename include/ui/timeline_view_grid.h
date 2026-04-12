#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "time/tempo.h"

void timeline_view_draw_grid(SDL_Renderer* renderer,
                             int x0,
                             int width,
                             int top,
                             int height,
                             float pixels_per_second,
                             float visible_seconds,
                             bool show_all_lines,
                             float window_start_seconds,
                             bool view_in_beats,
                             const TempoMap* tempo_map,
                             const TimeSignatureMap* signature_map);

void timeline_view_format_label(float seconds,
                                bool view_in_beats,
                                const TempoMap* tempo_map,
                                const TimeSignatureMap* signature_map,
                                char* out,
                                size_t out_len);
