#include "ui/effects_panel_meter_history_grid.h"

#include "ui/shared_theme_font_adapter.h"

#include <math.h>

static SDL_Color clamp_alpha(SDL_Color color, Uint8 max_alpha) {
    if (color.a > max_alpha) {
        color.a = max_alpha;
    }
    return color;
}

static bool near_integer(double value) {
    double rounded = round(value);
    return fabs(value - rounded) <= 1e-4;
}

static int to_history_x(const SDL_Rect* rect,
                        double start_seconds,
                        double span_seconds,
                        double tick_seconds) {
    double end_seconds = start_seconds + span_seconds;
    double t = (end_seconds - tick_seconds) / span_seconds;
    return rect->x + (int)lround(t * (double)(rect->w - 1));
}

static void draw_grid_line(SDL_Renderer* renderer,
                           const SDL_Rect* rect,
                           int x,
                           SDL_Color color) {
    if (!renderer || !rect || x < rect->x || x > rect->x + rect->w - 1) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, x, rect->y, x, rect->y + rect->h - 1);
}

static void draw_seconds_grid(SDL_Renderer* renderer,
                              const SDL_Rect* rect,
                              double start_seconds,
                              double end_seconds,
                              double span_seconds,
                              SDL_Color major_line,
                              SDL_Color minor_line) {
    const double major_step = 1.0;
    double minor_step = 0.1;
    if (span_seconds > 20.0) {
        minor_step = 0.25;
    }
    if (span_seconds > 60.0) {
        minor_step = 0.5;
    }

    double first_minor = floor(start_seconds / minor_step) * minor_step;
    for (double sec = first_minor; sec <= end_seconds + minor_step * 0.5; sec += minor_step) {
        if (near_integer(sec / major_step)) {
            continue;
        }
        draw_grid_line(renderer, rect, to_history_x(rect, start_seconds, span_seconds, sec), minor_line);
    }

    double first_major = floor(start_seconds / major_step) * major_step;
    for (double sec = first_major; sec <= end_seconds + major_step * 0.5; sec += major_step) {
        draw_grid_line(renderer, rect, to_history_x(rect, start_seconds, span_seconds, sec), major_line);
    }
}

static void draw_beats_grid(SDL_Renderer* renderer,
                            const SDL_Rect* rect,
                            double start_seconds,
                            double end_seconds,
                            double span_seconds,
                            const TempoMap* tempo_map,
                            const TimeSignatureMap* signature_map,
                            SDL_Color downbeat_line,
                            SDL_Color major_line,
                            SDL_Color minor_line) {
    if (!tempo_map || tempo_map->event_count <= 0 || tempo_map->sample_rate <= 0.0) {
        draw_seconds_grid(renderer,
                          rect,
                          start_seconds,
                          end_seconds,
                          span_seconds,
                          major_line,
                          minor_line);
        return;
    }

    double start_beats = tempo_map_seconds_to_beats(tempo_map, start_seconds);
    double end_beats = tempo_map_seconds_to_beats(tempo_map, end_seconds);
    double visible_beats = end_beats - start_beats;
    if (visible_beats <= 1e-6) {
        return;
    }

    double sub_step = 0.25;
    if (visible_beats > 16.0) {
        sub_step = 0.5;
    }
    if (visible_beats > 48.0) {
        sub_step = 1.0;
    }

    if (sub_step < 1.0) {
        double first_sub = floor(start_beats / sub_step) * sub_step;
        for (double beat = first_sub; beat <= end_beats + sub_step * 0.5; beat += sub_step) {
            if (near_integer(beat)) {
                continue;
            }
            double sec = tempo_map_beats_to_seconds(tempo_map, beat);
            draw_grid_line(renderer, rect, to_history_x(rect, start_seconds, span_seconds, sec), minor_line);
        }
    }

    double first_major = floor(start_beats);
    for (double beat = first_major; beat <= end_beats + 0.5; beat += 1.0) {
        SDL_Color line_color = major_line;
        if (signature_map) {
            int bar = 1;
            int beat_index = 1;
            double sub_beat = 0.0;
            time_signature_map_beat_to_bar_beat(signature_map, beat, &bar, &beat_index, &sub_beat, NULL, NULL);
            if (beat_index == 1 && fabs(sub_beat) <= 1e-3) {
                line_color = downbeat_line;
            }
        }
        double sec = tempo_map_beats_to_seconds(tempo_map, beat);
        draw_grid_line(renderer, rect, to_history_x(rect, start_seconds, span_seconds, sec), line_color);
    }
}

void effects_meter_history_grid_draw(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     const EffectsMeterHistoryGridContext* grid) {
    if (!renderer || !rect || rect->w <= 1 || rect->h <= 0 || !grid || !grid->enabled ||
        grid->history_span_seconds <= 1e-6) {
        return;
    }

    DawThemePalette theme = {0};
    if (!daw_shared_theme_resolve_palette(&theme)) {
        theme = (DawThemePalette){
            .grid_sub = {66, 70, 100, 200},
            .grid_major = {80, 82, 115, 255},
            .grid_downbeat = {90, 100, 130, 255}
        };
    }

    SDL_Color minor_line = clamp_alpha(theme.grid_sub, 64);
    SDL_Color major_line = clamp_alpha(theme.grid_major, 104);
    SDL_Color downbeat_line = clamp_alpha(theme.grid_downbeat, 132);

    double end_seconds = grid->history_end_seconds;
    double start_seconds = end_seconds - grid->history_span_seconds;
    double span_seconds = grid->history_span_seconds;

    SDL_Rect prior_clip = {0, 0, 0, 0};
    SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
    if (had_clip) {
        SDL_RenderGetClipRect(renderer, &prior_clip);
    }
    SDL_RenderSetClipRect(renderer, rect);

    if (grid->beat_mode) {
        draw_beats_grid(renderer,
                        rect,
                        start_seconds,
                        end_seconds,
                        span_seconds,
                        grid->tempo_map,
                        grid->signature_map,
                        downbeat_line,
                        major_line,
                        minor_line);
    } else {
        draw_seconds_grid(renderer,
                          rect,
                          start_seconds,
                          end_seconds,
                          span_seconds,
                          major_line,
                          minor_line);
    }

    if (had_clip) {
        SDL_RenderSetClipRect(renderer, &prior_clip);
    } else {
        SDL_RenderSetClipRect(renderer, NULL);
    }
}
