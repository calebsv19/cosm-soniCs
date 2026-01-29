#include "ui/time_grid.h"

#include "ui/font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Initializes a time grid cache to empty state.
void time_grid_cache_init(TimeGridCache* cache) {
    if (!cache) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
}

// Releases memory owned by a time grid cache.
void time_grid_cache_free(TimeGridCache* cache) {
    if (!cache) {
        return;
    }
    free(cache->minor_x);
    free(cache->major_ticks);
    time_grid_cache_init(cache);
}

// Ensures an int buffer can hold at least the requested count.
static bool time_grid_reserve_ints(int** values, int* capacity, int needed) {
    if (!values || !capacity) {
        return false;
    }
    if (needed <= *capacity) {
        return true;
    }
    int next = *capacity > 0 ? *capacity : 64;
    while (next < needed) {
        next *= 2;
    }
    int* resized = (int*)realloc(*values, (size_t)next * sizeof(int));
    if (!resized) {
        return false;
    }
    *values = resized;
    *capacity = next;
    return true;
}

// Ensures a major tick buffer can hold at least the requested count.
static bool time_grid_reserve_majors(TimeGridCache* cache, int needed) {
    if (!cache) {
        return false;
    }
    if (needed <= cache->major_capacity) {
        return true;
    }
    int next = cache->major_capacity > 0 ? cache->major_capacity : 64;
    while (next < needed) {
        next *= 2;
    }
    void* resized = realloc(cache->major_ticks, (size_t)next * sizeof(*cache->major_ticks));
    if (!resized) {
        return false;
    }
    cache->major_ticks = resized;
    cache->major_capacity = next;
    return true;
}

// Builds a cached set of time-grid lines for the visible window.
void time_grid_cache_build(TimeGridCache* cache,
                           int x0,
                           float pixels_per_second,
                           float visible_seconds,
                           float window_start_seconds,
                           bool show_all_lines) {
    if (!cache) {
        return;
    }
    cache->show_all_lines = show_all_lines;
    cache->active = true;

    if (show_all_lines) {
        float first_minor = floorf(window_start_seconds);
        float last_minor = window_start_seconds + visible_seconds;
        for (float sec_abs = first_minor; sec_abs <= last_minor + 0.5f; sec_abs += 1.0f) {
            float local = sec_abs - window_start_seconds;
            if (local < 0.0f || local > visible_seconds * 1.5f) {
                continue;
            }
            if (!time_grid_reserve_ints(&cache->minor_x, &cache->minor_capacity, cache->minor_count + 1)) {
                break;
            }
            cache->minor_x[cache->minor_count++] = x0 + (int)roundf(local * pixels_per_second);
        }
    }

    float major_interval = 1.0f;
    if (visible_seconds > 60.0f) {
        major_interval = 10.0f;
    } else if (visible_seconds > 30.0f) {
        major_interval = 5.0f;
    } else if (visible_seconds > 15.0f) {
        major_interval = 2.0f;
    }

    float first_major_sec = floorf(window_start_seconds / major_interval) * major_interval;
    float last_major_sec = window_start_seconds + visible_seconds + major_interval * 0.5f;
    for (float sec_abs = first_major_sec; sec_abs <= last_major_sec; sec_abs += major_interval) {
        float local = sec_abs - window_start_seconds;
        if (local < 0.0f || local > visible_seconds * 1.5f) {
            continue;
        }
        if (!time_grid_reserve_majors(cache, cache->major_count + 1)) {
            break;
        }
        cache->major_ticks[cache->major_count].x = x0 + (int)roundf(local * pixels_per_second);
        cache->major_ticks[cache->major_count].total_seconds = (int)sec_abs;
        cache->major_count += 1;
    }
}

// Draws a cached time-mode grid for a given lane rectangle.
void time_grid_cache_draw(SDL_Renderer* renderer,
                          int x0,
                          int width,
                          int top,
                          int height,
                          const TimeGridCache* cache) {
    if (!renderer || !cache || !cache->active) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_Rect border = {x0, top, width, height};
    SDL_RenderDrawRect(renderer, &border);

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_Color minor_line = {65, 65, 85, 255};
    SDL_Color major_line = {80, 82, 115, 255};

    if (cache->show_all_lines) {
        SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
        for (int i = 0; i < cache->minor_count; ++i) {
            int x = cache->minor_x[i];
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    for (int i = 0; i < cache->major_count; ++i) {
        int x = cache->major_ticks[i].x;
        int total_seconds = cache->major_ticks[i].total_seconds;
        SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        int minutes = total_seconds / 60;
        int seconds = total_seconds % 60;
        char label[16];
        snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
        ui_draw_text(renderer, x + 4, top - 14, label, label_color, 1);
    }
}
