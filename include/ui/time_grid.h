#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

// Stores precomputed time-grid lines and labels for the visible timeline range.
typedef struct {
    int* minor_x;
    int minor_count;
    int minor_capacity;
    struct {
        int x;
        int total_seconds;
    }* major_ticks;
    int major_count;
    int major_capacity;
    bool show_all_lines;
    bool active;
} TimeGridCache;

// Initializes a time grid cache to empty state.
void time_grid_cache_init(TimeGridCache* cache);
// Releases memory owned by a time grid cache.
void time_grid_cache_free(TimeGridCache* cache);
// Builds a cached set of time-grid lines for the visible window.
void time_grid_cache_build(TimeGridCache* cache,
                           int x0,
                           float pixels_per_second,
                           float visible_seconds,
                           float window_start_seconds,
                           bool show_all_lines);
// Draws a cached time-mode grid for a given lane rectangle.
void time_grid_cache_draw(SDL_Renderer* renderer,
                          int x0,
                          int width,
                          int top,
                          int height,
                          const TimeGridCache* cache);
