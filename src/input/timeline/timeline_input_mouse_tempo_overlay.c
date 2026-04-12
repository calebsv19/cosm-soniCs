#include "input/timeline/timeline_input_mouse_tempo_overlay.h"

#include "app_state.h"
#include "input/tempo_overlay_input.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_snap.h"
#include "ui/timeline_view.h"
#include "time/tempo.h"

#include <math.h>
#include <stdlib.h>

#define TEMPO_OVERLAY_MIN_BPM 20.0
#define TEMPO_OVERLAY_MAX_BPM 200.0

static bool timeline_tempo_overlay_rect(const TimelineGeometry* geom, SDL_Rect* out_rect) {
    SDL_Rect lane_rect = {0, 0, 0, 0};
    if (!geom || !out_rect || geom->content_width <= 0 || geom->track_height <= 0) {
        return false;
    }
    timeline_view_compute_lane_clip_rect(geom->track_top,
                                         geom->track_height,
                                         geom->content_left,
                                         geom->content_width,
                                         &lane_rect);
    if (lane_rect.h < 8) {
        return false;
    }
    *out_rect = lane_rect;
    return true;
}

static void timeline_tempo_overlay_bpm_range(const TempoMap* map,
                                             double fallback_bpm,
                                             double* out_min,
                                             double* out_max) {
    (void)map;
    (void)fallback_bpm;
    if (out_min) *out_min = TEMPO_OVERLAY_MIN_BPM;
    if (out_max) *out_max = TEMPO_OVERLAY_MAX_BPM;
}

static int timeline_tempo_overlay_bpm_to_y(const SDL_Rect* rect,
                                           double bpm,
                                           double min_bpm,
                                           double max_bpm) {
    if (!rect || rect->h <= 0) {
        return 0;
    }
    double range = max_bpm - min_bpm;
    if (range <= 0.0) {
        range = 1.0;
    }
    double t = (bpm - min_bpm) / range;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return rect->y + rect->h - (int)llround(t * (double)rect->h);
}

static double timeline_tempo_overlay_bpm_from_y(const SDL_Rect* rect,
                                                int y,
                                                double min_bpm,
                                                double max_bpm) {
    if (!rect || rect->h <= 0) {
        return min_bpm;
    }
    double range = max_bpm - min_bpm;
    if (range <= 0.0) {
        range = 1.0;
    }
    int clamped_y = y;
    if (clamped_y < rect->y) clamped_y = rect->y;
    if (clamped_y > rect->y + rect->h) clamped_y = rect->y + rect->h;
    double t = (double)(rect->y + rect->h - clamped_y) / (double)rect->h;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return min_bpm + t * range;
}

static double timeline_tempo_overlay_snap_bpm(double bpm, bool free_snap) {
    if (free_snap) {
        return bpm;
    }
    return round(bpm / 10.0) * 10.0;
}

static int timeline_tempo_overlay_find_event(const TempoMap* map, double beat) {
    if (!map || map->event_count <= 0) {
        return -1;
    }
    int best = 0;
    double best_delta = fabs(map->events[0].beat - beat);
    for (int i = 1; i < map->event_count; ++i) {
        double delta = fabs(map->events[i].beat - beat);
        if (delta < best_delta) {
            best_delta = delta;
            best = i;
        }
    }
    return best;
}

static int timeline_tempo_overlay_hit_event(const TempoMap* map,
                                            const SDL_Rect* rect,
                                            float window_start_seconds,
                                            float window_end_seconds,
                                            int content_left,
                                            float pixels_per_second,
                                            double min_bpm,
                                            double max_bpm,
                                            int x,
                                            int y) {
    if (!map || !rect || rect->w <= 0 || rect->h <= 0 || map->event_count <= 0) {
        return -1;
    }
    const int radius = 6;
    for (int i = 0; i < map->event_count; ++i) {
        double sec = tempo_map_beats_to_seconds(map, map->events[i].beat);
        if (sec < window_start_seconds || sec > window_end_seconds) {
            continue;
        }
        int px = content_left + (int)llround((sec - window_start_seconds) * pixels_per_second);
        int py = timeline_tempo_overlay_bpm_to_y(rect, map->events[i].bpm, min_bpm, max_bpm);
        if (abs(px - x) <= radius && abs(py - y) <= radius) {
            return i;
        }
    }
    return -1;
}

bool timeline_input_mouse_handle_tempo_overlay(AppState* state,
                                               const TimelineGeometry* geom,
                                               int sample_rate,
                                               bool was_down,
                                               bool is_down) {
    if (!state || !geom || sample_rate <= 0 || !state->timeline_tempo_overlay_enabled) {
        return false;
    }

    SDL_Rect tempo_rect = {0, 0, 0, 0};
    if (!timeline_tempo_overlay_rect(geom, &tempo_rect)) {
        return false;
    }

    if (state->tempo_overlay_ui.dragging) {
        if (!is_down) {
            state->tempo_overlay_ui.dragging = false;
            tempo_overlay_commit_edit(state);
            return true;
        }
        double min_bpm = 0.0;
        double max_bpm = 0.0;
        timeline_tempo_overlay_bpm_range(&state->tempo_map, state->tempo.bpm, &min_bpm, &max_bpm);
        SDL_Keymod mods = SDL_GetModState();
        bool free_snap = (mods & KMOD_ALT) != 0;
        float seconds = timeline_x_to_seconds(geom, state->mouse_x);
        float window_min = geom->window_start_seconds;
        float window_max = geom->window_start_seconds + geom->visible_seconds;
        if (window_min < 0.0f) window_min = 0.0f;
        if (seconds < window_min) seconds = window_min;
        if (seconds > window_max) seconds = window_max;
        seconds = timeline_snap_seconds_to_grid(state, seconds, geom->visible_seconds);
        double beat = tempo_map_seconds_to_beats(&state->tempo_map, (double)seconds);
        double bpm = timeline_tempo_overlay_bpm_from_y(&tempo_rect, state->mouse_y, min_bpm, max_bpm);
        bpm = timeline_tempo_overlay_snap_bpm(bpm, free_snap);
        if (bpm < min_bpm) bpm = min_bpm;
        if (bpm > max_bpm) bpm = max_bpm;
        if (tempo_map_update_event(&state->tempo_map, state->tempo_overlay_ui.event_index, beat, bpm)) {
            state->tempo_overlay_ui.event_index = timeline_tempo_overlay_find_event(&state->tempo_map, beat);
        }
        return true;
    }

    if (!was_down && is_down) {
        SDL_Point tempo_point = {state->mouse_x, state->mouse_y};
        if (!SDL_PointInRect(&tempo_point, &tempo_rect)) {
            return false;
        }
        tempo_overlay_begin_edit(state);
        double min_bpm = 0.0;
        double max_bpm = 0.0;
        timeline_tempo_overlay_bpm_range(&state->tempo_map, state->tempo.bpm, &min_bpm, &max_bpm);
        SDL_Keymod mods = SDL_GetModState();
        bool free_snap = (mods & KMOD_ALT) != 0;
        int hit = timeline_tempo_overlay_hit_event(&state->tempo_map,
                                                   &tempo_rect,
                                                   geom->window_start_seconds,
                                                   geom->window_start_seconds + geom->visible_seconds,
                                                   geom->content_left,
                                                   geom->pixels_per_second,
                                                   min_bpm,
                                                   max_bpm,
                                                   state->mouse_x,
                                                   state->mouse_y);
        float seconds = timeline_x_to_seconds(geom, state->mouse_x);
        float window_min = geom->window_start_seconds;
        float window_max = geom->window_start_seconds + geom->visible_seconds;
        if (window_min < 0.0f) window_min = 0.0f;
        if (seconds < window_min) seconds = window_min;
        if (seconds > window_max) seconds = window_max;
        seconds = timeline_snap_seconds_to_grid(state, seconds, geom->visible_seconds);
        double beat = tempo_map_seconds_to_beats(&state->tempo_map, (double)seconds);
        double bpm = timeline_tempo_overlay_bpm_from_y(&tempo_rect, state->mouse_y, min_bpm, max_bpm);
        bpm = timeline_tempo_overlay_snap_bpm(bpm, free_snap);
        if (bpm < min_bpm) bpm = min_bpm;
        if (bpm > max_bpm) bpm = max_bpm;
        if (hit >= 0) {
            state->tempo_overlay_ui.event_index = hit;
        } else if (tempo_map_upsert_event(&state->tempo_map, beat, bpm)) {
            state->tempo_overlay_ui.event_index = timeline_tempo_overlay_find_event(&state->tempo_map, beat);
        }
        state->tempo_overlay_ui.dragging = true;
        return true;
    }

    return false;
}
