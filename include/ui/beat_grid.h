#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "time/tempo.h"

// Stores precomputed beat-grid lines and labels for the visible timeline range.
typedef struct {
    int* minor_x;
    int minor_count;
    int minor_capacity;
    int* sub_x;
    int sub_count;
    int sub_capacity;
    struct {
        int x;
        int bar;
    }* bar_ticks;
    int bar_count;
    int bar_capacity;
    struct {
        int x;
        int bar;
        int beat;
        bool downbeat;
    }* major_ticks;
    int major_count;
    int major_capacity;
    bool show_all_lines;
    bool active;
} BeatGridCache;

// Initializes a beat grid cache to empty state.
void beat_grid_cache_init(BeatGridCache* cache);
// Releases memory owned by a beat grid cache.
void beat_grid_cache_free(BeatGridCache* cache);
// Builds a cached set of beat-grid lines for the visible window.
void beat_grid_cache_build(BeatGridCache* cache,
                           int x0,
                           float pixels_per_second,
                           float visible_seconds,
                           float window_start_seconds,
                           bool show_all_lines,
                           const TempoMap* tempo_map,
                           const TimeSignatureMap* signature_map);
// Draws a cached beat-mode grid for a given lane rectangle.
void beat_grid_cache_draw(SDL_Renderer* renderer,
                          int x0,
                          int width,
                          int top,
                          int height,
                          const BeatGridCache* cache);
