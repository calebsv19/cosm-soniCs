#include "ui/beat_grid.h"

#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void resolve_grid_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .pane_border = {60, 60, 72, 255},
            .text_primary = {200, 210, 230, 255},
            .text_muted = {150, 150, 160, 255},
            .grid_minor = {65, 65, 85, 255},
            .grid_sub = {66, 70, 100, 200},
            .grid_major = {80, 82, 115, 255},
            .grid_downbeat = {90, 100, 130, 255}
        };
    }
}

// Initializes a beat grid cache to empty state.
void beat_grid_cache_init(BeatGridCache* cache) {
    if (!cache) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
}

// Releases memory owned by a beat grid cache.
void beat_grid_cache_free(BeatGridCache* cache) {
    if (!cache) {
        return;
    }
    free(cache->minor_x);
    free(cache->sub_x);
    free(cache->bar_ticks);
    free(cache->major_ticks);
    beat_grid_cache_init(cache);
}

// Ensures an int buffer can hold at least the requested count.
static bool beat_grid_reserve_ints(int** values, int* capacity, int needed) {
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

// Ensures a bar tick buffer can hold at least the requested count.
static bool beat_grid_reserve_bars(BeatGridCache* cache, int needed) {
    if (!cache) {
        return false;
    }
    if (needed <= cache->bar_capacity) {
        return true;
    }
    int next = cache->bar_capacity > 0 ? cache->bar_capacity : 64;
    while (next < needed) {
        next *= 2;
    }
    void* resized = realloc(cache->bar_ticks, (size_t)next * sizeof(*cache->bar_ticks));
    if (!resized) {
        return false;
    }
    cache->bar_ticks = resized;
    cache->bar_capacity = next;
    return true;
}

// Ensures a major tick buffer can hold at least the requested count.
static bool beat_grid_reserve_majors(BeatGridCache* cache, int needed) {
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

// Builds a cached set of beat-grid lines for the visible window.
void beat_grid_cache_build(BeatGridCache* cache,
                           int x0,
                           float pixels_per_second,
                           float visible_seconds,
                           float window_start_seconds,
                           bool show_all_lines,
                           const TempoMap* tempo_map,
                           const TimeSignatureMap* signature_map) {
    if (!cache || !tempo_map || tempo_map->event_count <= 0 || tempo_map->sample_rate <= 0.0) {
        return;
    }

    cache->show_all_lines = show_all_lines;
    cache->active = true;

    double start_beats = tempo_map_seconds_to_beats(tempo_map, (double)window_start_seconds);
    double end_beats = tempo_map_seconds_to_beats(tempo_map, (double)(window_start_seconds + visible_seconds));
    double visible_beats = end_beats - start_beats;
    if (visible_beats < 0.0001) {
        visible_beats = 0.0001;
    }

    if (show_all_lines) {
        const TimeSignatureEvent* minor_sig =
            signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
        double beat_unit = time_signature_beat_unit(minor_sig);
        if (beat_unit <= 0.0) {
            beat_unit = 1.0;
        }
        double first_minor = floor(start_beats / beat_unit) * beat_unit;
        double last_minor = end_beats;
        for (double gb = first_minor; gb <= last_minor + beat_unit * 0.5; gb += beat_unit) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            if (!beat_grid_reserve_ints(&cache->minor_x, &cache->minor_capacity, cache->minor_count + 1)) {
                break;
            }
            cache->minor_x[cache->minor_count++] = x0 + (int)round(local_sec * (double)pixels_per_second);
        }

        double sub_interval = 0.0;
        if (visible_beats <= 8.0) {
            sub_interval = time_signature_beat_unit(minor_sig) * 0.25;
        } else if (visible_beats <= 32.0) {
            sub_interval = time_signature_beat_unit(minor_sig) * 0.5;
        }
        if (sub_interval > 0.0) {
            double first_sub = floor(start_beats / sub_interval) * sub_interval;
            double last_sub = end_beats + sub_interval * 0.5;
            for (double gb = first_sub; gb <= last_sub; gb += sub_interval) {
                double sec = tempo_map_beats_to_seconds(tempo_map, gb);
                double local_sec = sec - window_start_seconds;
                if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                    continue;
                }
                if (!beat_grid_reserve_ints(&cache->sub_x, &cache->sub_capacity, cache->sub_count + 1)) {
                    break;
                }
                cache->sub_x[cache->sub_count++] = x0 + (int)round(local_sec * (double)pixels_per_second);
            }
        }

        double major_units = 1.0;
        double visible_units = (beat_unit > 0.0) ? (visible_beats / beat_unit) : visible_beats;
        if (visible_units > 128.0) {
            major_units = 8.0;
        } else if (visible_units > 64.0) {
            major_units = 4.0;
        } else if (visible_units > 32.0) {
            major_units = 2.0;
        }
        double major_interval_beats = beat_unit * major_units;
        double first_major = floor(start_beats / major_interval_beats) * major_interval_beats;
        double last_major = end_beats + major_interval_beats * 0.5;
        for (double gb = first_major; gb <= last_major; gb += major_interval_beats) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int bar = 1;
            int beat_idx = 1;
            double sub = 0.0;
            time_signature_map_beat_to_bar_beat(signature_map, gb, &bar, &beat_idx, &sub, NULL, NULL);
            bool downbeat = beat_idx == 1;
            if (!beat_grid_reserve_majors(cache, cache->major_count + 1)) {
                break;
            }
            cache->major_ticks[cache->major_count].x = x0 + (int)round(local_sec * (double)pixels_per_second);
            cache->major_ticks[cache->major_count].bar = bar;
            cache->major_ticks[cache->major_count].beat = beat_idx;
            cache->major_ticks[cache->major_count].downbeat = downbeat;
            cache->major_count += 1;
        }
    } else {
        const TimeSignatureEvent* base_sig =
            signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
        double beats_per_bar = time_signature_beats_per_bar(base_sig);
        if (beats_per_bar <= 0.0) {
            beats_per_bar = 4.0;
        }
        double visible_bars = visible_beats / beats_per_bar;
        int bar_interval = 1;
        if (visible_bars > 128.0) {
            bar_interval = 16;
        } else if (visible_bars > 64.0) {
            bar_interval = 8;
        } else if (visible_bars > 32.0) {
            bar_interval = 4;
        } else if (visible_bars > 16.0) {
            bar_interval = 2;
        }

        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, start_beats, &bar, &beat_idx, &sub, NULL, NULL);
        double beat_unit = time_signature_beat_unit(base_sig);
        if (beat_unit <= 0.0) {
            beat_unit = 1.0;
        }
        double bar_start = start_beats - (((double)(beat_idx - 1) + sub) * beat_unit);
        if (bar_start < 0.0) {
            bar_start = 0.0;
            bar = 1;
        }
        while (bar_start <= end_beats + 0.5) {
            if ((bar - 1) % bar_interval == 0) {
                double sec = tempo_map_beats_to_seconds(tempo_map, bar_start);
                double local_sec = sec - window_start_seconds;
                if (local_sec >= 0.0 && local_sec <= visible_seconds * 1.5) {
                    if (!beat_grid_reserve_bars(cache, cache->bar_count + 1)) {
                        break;
                    }
                    cache->bar_ticks[cache->bar_count].x = x0 + (int)round(local_sec * (double)pixels_per_second);
                    cache->bar_ticks[cache->bar_count].bar = bar;
                    cache->bar_count += 1;
                }
            }
            const TimeSignatureEvent* sig = time_signature_map_event_at_beat(signature_map, bar_start + 1e-6);
            double next_beats_per_bar = time_signature_beats_per_bar(sig);
            if (next_beats_per_bar <= 0.0) {
                next_beats_per_bar = beats_per_bar;
            }
            bar_start += next_beats_per_bar;
            bar += 1;
        }
    }
}

// Draws a cached beat-mode grid for a given lane rectangle.
void beat_grid_cache_draw(SDL_Renderer* renderer,
                          int x0,
                          int width,
                          int top,
                          int height,
                          const BeatGridCache* cache) {
    DawThemePalette palette = {0};
    if (!renderer || !cache || !cache->active) {
        return;
    }
    resolve_grid_theme(&palette);
    SDL_SetRenderDrawColor(renderer,
                           palette.pane_border.r,
                           palette.pane_border.g,
                           palette.pane_border.b,
                           palette.pane_border.a);
    SDL_Rect border = {x0, top, width, height};
    SDL_RenderDrawRect(renderer, &border);

    SDL_Color label_color = palette.text_muted;
    SDL_Color minor_line = palette.grid_minor;
    SDL_Color sub_line = palette.grid_sub;
    SDL_Color major_line = palette.grid_major;
    SDL_Color downbeat_line = palette.grid_downbeat;

    if (cache->show_all_lines) {
        SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
        for (int i = 0; i < cache->minor_count; ++i) {
            int x = cache->minor_x[i];
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
        SDL_SetRenderDrawColor(renderer, sub_line.r, sub_line.g, sub_line.b, sub_line.a);
        for (int i = 0; i < cache->sub_count; ++i) {
            int x = cache->sub_x[i];
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
        for (int i = 0; i < cache->major_count; ++i) {
            const int x = cache->major_ticks[i].x;
            const int bar = cache->major_ticks[i].bar;
            const int beat = cache->major_ticks[i].beat;
            bool downbeat = cache->major_ticks[i].downbeat;
            if (downbeat) {
                SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
            } else {
                SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
            }
            SDL_RenderDrawLine(renderer, x, top, x, top + height);

            char label[24];
            snprintf(label, sizeof(label), "%d.%d", bar, beat);
            int label_scale = 1;
            int base_h = ui_font_line_height(1);
            int scaled_h = ui_font_line_height(label_scale);
            int extra = (scaled_h - base_h) / 2;
            int label_y = top - 10 - extra;
            SDL_Color c = label_color;
            if (downbeat) {
                c = palette.text_primary;
            }
            ui_draw_text(renderer, x + 4, label_y, label, c, label_scale);
        }
        return;
    }

    SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
    for (int i = 0; i < cache->bar_count; ++i) {
        int x = cache->bar_ticks[i].x;
        int bar = cache->bar_ticks[i].bar;
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        char label[24];
        snprintf(label, sizeof(label), "%d", bar);
        int label_scale = 1;
        int base_h = ui_font_line_height(1);
        int scaled_h = ui_font_line_height(label_scale);
        int extra = (scaled_h - base_h) / 2;
        int label_y = top - 10 - extra;
        ui_draw_text(renderer, x + 4, label_y, label, label_color, label_scale);
    }
}
