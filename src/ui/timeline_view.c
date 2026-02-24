#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "audio/media_registry.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/beat_grid.h"
#include "ui/kit_viz_waveform_adapter.h"
#include "ui/time_grid.h"
#include "ui/timeline_waveform.h"
#include "ui/waveform_render.h"
#include "time/tempo.h"
#include "input/timeline/timeline_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOVE_GHOST_ALPHA 140
#define TEMPO_OVERLAY_MIN_BPM 20.0
#define TEMPO_OVERLAY_MAX_BPM 200.0

static bool timeline_clip_is_selected(const AppState* state, int track_index, int clip_index) {
    if (!state) {
        return false;
    }
    if (state->selection_count > 0) {
        for (int i = 0; i < state->selection_count; ++i) {
            if (state->selection[i].track_index == track_index && state->selection[i].clip_index == clip_index) {
                return true;
            }
        }
        return false;
    }
    return state->selected_track_index == track_index && state->selected_clip_index == clip_index;
}

static const char* timeline_basename(const char* path, char* scratch, size_t scratch_len) {
    if (!scratch || scratch_len == 0) {
        return "";
    }
    scratch[0] = '\0';
    if (!path || path[0] == '\0') {
        return scratch;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    SDL_strlcpy(scratch, base, scratch_len);
    char* dot = strrchr(scratch, '.');
    if (dot) {
        *dot = '\0';
    }
    return scratch;
}

static const char* timeline_clip_display_name(const AppState* state,
                                              const EngineClip* clip,
                                              char* scratch,
                                              size_t scratch_len) {
    if (!clip) {
        return "Clip";
    }
    if (clip->name[0] != '\0') {
        return clip->name;
    }
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry) {
            if (entry->name[0] != '\0') {
                return entry->name;
            }
            if (entry->path[0] != '\0') {
                return timeline_basename(entry->path, scratch, scratch_len);
            }
        }
    }
    const char* media_path = engine_clip_get_media_path(clip);
    if (media_path && media_path[0] != '\0') {
        return timeline_basename(media_path, scratch, scratch_len);
    }
    return "Clip";
}

#define CLIP_COLOR_R 36
#define CLIP_COLOR_G 40
#define CLIP_COLOR_B 52
#define TRACK_BG_R 38
#define TRACK_BG_G 44
#define TRACK_BG_B 58
#define CLIP_HANDLE_WIDTH 8

#define WAVEFORM_COLOR_R 120
#define WAVEFORM_COLOR_G 140
#define WAVEFORM_COLOR_B 170

static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void draw_timeline_button(SDL_Renderer* renderer,
                                 const SDL_Rect* rect,
                                 const char* label,
                                 bool hovered,
                                 bool enabled) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = {50, 58, 70, 255};
    SDL_Color hover = {72, 82, 100, 255};
    SDL_Color disabled = {34, 36, 40, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color text = {220, 220, 230, 255};
    SDL_Color text_disabled = {140, 140, 150, 255};

    SDL_Color fill = enabled ? (hovered ? hover : base) : disabled;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Color text_color = enabled ? text : text_disabled;
    int scale = 1;
    int text_w = ui_measure_text_width(label, scale);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text_color, scale);
}

static void fit_label_ellipsis(const char* src, int max_px, float scale, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!src) return;
    int full_w = ui_measure_text_width(src, scale);
    if (full_w <= max_px) {
        strncpy(out, src, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    const char* ell = "...";
    int ell_w = ui_measure_text_width(ell, scale);
    int target_px = max_px - ell_w;
    if (target_px <= 0) {
        strncpy(out, ell, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t len = strlen(src);
    for (size_t i = len; i > 0; --i) {
        char tmp[ENGINE_CLIP_NAME_MAX];
        snprintf(tmp, sizeof(tmp), "%.*s", (int)i, src);
        if (ui_measure_text_width(tmp, scale) <= target_px) {
            snprintf(out, out_size, "%s%s", tmp, ell);
            out[out_size - 1] = '\0';
            return;
        }
    }
    strncpy(out, ell, out_size - 1);
    out[out_size - 1] = '\0';
}

static void draw_toggle_button(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const char* label,
                               bool active,
                               bool hovered,
                               SDL_Color active_col) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = {58, 62, 70, 255};
    SDL_Color hover = {82, 92, 110, 255};
    SDL_Color active_color = active_col;
    SDL_Color text = {220, 220, 230, 255};
    SDL_Color border = {90, 95, 110, 255};

    SDL_Color fill = base;
    if (active) {
        fill = active_color;
    } else if (hovered) {
        fill = hover;
    }
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    int scale = 1;
    int text_w = ui_measure_text_width(label, scale);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    if (text_x < rect->x + 2) text_x = rect->x + 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

static void draw_timeline_grid(SDL_Renderer* renderer,
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
                               const TimeSignatureMap* signature_map) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_Rect border = {x0, top, width, height};
    SDL_RenderDrawRect(renderer, &border);

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_Color minor_line = {65, 65, 85, 255};
    SDL_Color sub_line = {66, 70, 100, 200};
    SDL_Color major_line = {80, 82, 115, 255};
    SDL_Color downbeat_line = {90, 100, 130, 255};
    SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);

    if (!view_in_beats || !tempo_map || tempo_map->event_count <= 0 || tempo_map->sample_rate <= 0.0) {
        if (show_all_lines) {
            float first_minor = floorf(window_start_seconds);
            float last_minor = window_start_seconds + visible_seconds;
            SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
            for (float sec_abs = first_minor; sec_abs <= last_minor + 0.5f; sec_abs += 1.0f) {
                float local = sec_abs - window_start_seconds;
                if (local < 0.0f || local > visible_seconds * 1.5f) {
                    continue;
                }
                int x = x0 + (int)roundf(local * pixels_per_second);
                SDL_RenderDrawLine(renderer, x, top, x, top + height);
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

        float first_major_sec = floor(window_start_seconds / major_interval) * major_interval;
        float last_major_sec = window_start_seconds + visible_seconds + major_interval * 0.5f;
        for (float sec_abs = first_major_sec; sec_abs <= last_major_sec; sec_abs += major_interval) {
            float local = sec_abs - window_start_seconds;
            if (local < 0.0f || local > visible_seconds * 1.5f) {
                continue;
            }
            int x = x0 + (int)roundf(local * pixels_per_second);
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);

            float label_sec = sec_abs;
            int total_seconds = (int)label_sec;
            int minutes = total_seconds / 60;
            int seconds = total_seconds % 60;
            char label[16];
            snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
            ui_draw_text(renderer, x + 4, top - 14, label, label_color, 1);
        }
        return;
    }

    double start_beats = tempo_map_seconds_to_beats(tempo_map, (double)window_start_seconds);
    double end_beats = tempo_map_seconds_to_beats(tempo_map, (double)(window_start_seconds + visible_seconds));
    double visible_beats = end_beats - start_beats;
    if (visible_beats < 0.0001) {
        visible_beats = 0.0001;
    }

    // Minor lines at every beat when show_all_lines is on.
    if (show_all_lines) {
        const TimeSignatureEvent* minor_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
        double beat_unit = time_signature_beat_unit(minor_sig);
        if (beat_unit <= 0.0) {
            beat_unit = 1.0;
        }
        double first_minor = floor(start_beats / beat_unit) * beat_unit;
        double last_minor = end_beats;
        SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
        for (double gb = first_minor; gb <= last_minor + beat_unit * 0.5; gb += beat_unit) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    const TimeSignatureEvent* base_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
    double beat_unit = time_signature_beat_unit(base_sig);
    if (beat_unit <= 0.0) {
        beat_unit = 1.0;
    }
    double beats_per_bar = time_signature_beats_per_bar(base_sig);
    if (beats_per_bar <= 0.0) {
        beats_per_bar = 4.0;
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

    // Extra subdivision lines at high zoom: quarter-beat at tight zoom, half-beat at medium zoom (no labels).
    double sub_interval = 0.0;
    if (visible_beats <= 8.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.25; // quarter of a beat unit
    } else if (visible_beats <= 32.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.5; // half of a beat unit
    }
    if (sub_interval > 0.0) {
        SDL_SetRenderDrawColor(renderer, sub_line.r, sub_line.g, sub_line.b, sub_line.a);
        double first_sub = floor(start_beats / sub_interval) * sub_interval;
        double last_sub = end_beats + sub_interval * 0.5;
        for (double gb = first_sub; gb <= last_sub; gb += sub_interval) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    if (!show_all_lines) {
        double visible_bars = visible_beats / (double)beats_per_bar;
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
                    int x = x0 + (int)round(local_sec * (double)pixels_per_second);
                    SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
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
            const TimeSignatureEvent* sig = time_signature_map_event_at_beat(signature_map, bar_start + 1e-6);
            double next_beats_per_bar = time_signature_beats_per_bar(sig);
            if (next_beats_per_bar <= 0.0) {
                next_beats_per_bar = beats_per_bar;
            }
            bar_start += next_beats_per_bar;
            bar += 1;
        }
        return;
    }

    // Major beats aligned to absolute bars/beats (not reset per window).
    double first_major = floor(start_beats / major_interval_beats) * major_interval_beats;
    double last_major = end_beats + major_interval_beats * 0.5;
    for (double gb = first_major; gb <= last_major; gb += major_interval_beats) {
        double sec = tempo_map_beats_to_seconds(tempo_map, gb);
        double local_sec = sec - window_start_seconds;
        if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
            continue;
        }
        int x = x0 + (int)round(local_sec * (double)pixels_per_second);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, gb, &bar, &beat_idx, &sub, NULL, NULL);
        bool is_downbeat = beat_idx == 1;
        if (is_downbeat) {
            SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
        } else {
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
        }
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        char label[24];
        snprintf(label, sizeof(label), "%d.%d", bar, beat_idx);
        int label_scale = (beat_idx == 1) ? 2 : 1;
        if (label_scale == 2) {
            label_scale = 1; // keep subtle; future tweak could use 1.5 if fractional supported
        }
        int base_h = ui_font_line_height(1);
        int scaled_h = ui_font_line_height(label_scale);
        int extra = (scaled_h - base_h) / 2;
        int label_y = top - 10 - extra;
        SDL_Color c = label_color;
        if (beat_idx == 1) {
            c = (SDL_Color){200, 210, 230, 255};
        }
        ui_draw_text(renderer, x + 4, label_y, label, c, label_scale);
    }
}

static void format_timeline_label(float seconds,
                                  bool view_in_beats,
                                  const TempoMap* tempo_map,
                                  const TimeSignatureMap* signature_map,
                                  char* out,
                                  size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (view_in_beats && tempo_map && tempo_map->event_count > 0 && tempo_map->sample_rate > 0.0) {
        double beats = tempo_map_seconds_to_beats(tempo_map, seconds);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, beats, &bar, &beat_idx, &sub, NULL, NULL);
        if (bar < 1) bar = 1;
        if (beat_idx < 1) beat_idx = 1;
        snprintf(out, out_len, "%d.%02d", bar, beat_idx);
    } else {
        int total_ms = (int)llroundf(seconds * 1000.0f);
        int minutes = total_ms / 60000;
        int seconds_part = (total_ms / 1000) % 60;
        int millis = total_ms % 1000;
        snprintf(out, out_len, "%02d:%02d.%03d", minutes, seconds_part, millis);
    }
    out[out_len - 1] = '\0';
}

// Computes the tempo overlay rectangle positioned over the top track lane.
static bool timeline_compute_tempo_overlay_rect(const SDL_Rect* timeline_rect,
                                                int track_y,
                                                int track_height,
                                                int content_left,
                                                int content_width,
                                                SDL_Rect* out_rect) {
    if (!timeline_rect || !out_rect || track_height <= 0 || content_width <= 0) {
        return false;
    }
    int overlay_height = track_height - 16;
    if (overlay_height < 8) {
        return false;
    }
    *out_rect = (SDL_Rect){
        content_left,
        track_y + 8,
        content_width,
        overlay_height
    };
    return true;
}

// Maps a BPM value into a y position within the overlay rectangle.
static int timeline_tempo_value_to_y(const SDL_Rect* rect, double bpm, double min_bpm, double max_bpm) {
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

// Draws the tempo overlay lane with step segments and optional value labels.
static void draw_timeline_tempo_overlay(SDL_Renderer* renderer,
                                        const SDL_Rect* overlay_rect,
                                        const TempoMap* tempo_map,
                                        int selected_index,
                                        float window_start_seconds,
                                        float window_end_seconds,
                                        int content_left,
                                        float pixels_per_second,
                                        bool draw_labels) {
    if (!renderer || !overlay_rect || overlay_rect->w <= 0 || overlay_rect->h <= 0 ||
        pixels_per_second <= 0.0f || window_end_seconds <= window_start_seconds) {
        return;
    }
    if (!tempo_map || tempo_map->event_count <= 0) {
        return;
    }
    const double min_bpm = TEMPO_OVERLAY_MIN_BPM;
    const double max_bpm = TEMPO_OVERLAY_MAX_BPM;

    SDL_SetRenderDrawColor(renderer, 12, 16, 22, 140);
    SDL_RenderFillRect(renderer, overlay_rect);
    SDL_SetRenderDrawColor(renderer, 70, 90, 120, 200);
    SDL_RenderDrawRect(renderer, overlay_rect);

    SDL_Color line_color = {180, 210, 230, 220};
    SDL_Color point_color = {200, 230, 250, 255};
    SDL_Color label_color = {220, 235, 255, 255};

    for (int i = 0; i < tempo_map->event_count; ++i) {
        const TempoEvent* evt = &tempo_map->events[i];
        double seg_start = tempo_map_beats_to_seconds(tempo_map, evt->beat);
        double seg_end = window_end_seconds;
        if (i + 1 < tempo_map->event_count) {
            seg_end = tempo_map_beats_to_seconds(tempo_map, tempo_map->events[i + 1].beat);
        }
        if (seg_end < window_start_seconds || seg_start > window_end_seconds) {
            continue;
        }
        double draw_start = seg_start < window_start_seconds ? window_start_seconds : seg_start;
        double draw_end = seg_end > window_end_seconds ? window_end_seconds : seg_end;
        if (draw_end <= draw_start) {
            continue;
        }
        int x0 = content_left + (int)llround((draw_start - window_start_seconds) * pixels_per_second);
        int x1 = content_left + (int)llround((draw_end - window_start_seconds) * pixels_per_second);
        int y = timeline_tempo_value_to_y(overlay_rect, evt->bpm, min_bpm, max_bpm);
        SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);
        SDL_RenderDrawLine(renderer, x0, y, x1, y);

        if (seg_start >= window_start_seconds && seg_start <= window_end_seconds) {
            int px = content_left + (int)llround((seg_start - window_start_seconds) * pixels_per_second);
            SDL_Rect dot = {px - 3, y - 3, 6, 6};
            if (i == selected_index) {
                SDL_SetRenderDrawColor(renderer, 240, 250, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, point_color.r, point_color.g, point_color.b, point_color.a);
            }
            SDL_RenderFillRect(renderer, &dot);
            if (draw_labels) {
                char label[32];
                snprintf(label, sizeof(label), "%.1f", evt->bpm);
                int text_w = ui_measure_text_width(label, 1);
                int text_x = px + 6;
                int text_y = y - ui_font_line_height(1) - 2;
                if (text_x + text_w > overlay_rect->x + overlay_rect->w - 4) {
                    text_x = px - text_w - 6;
                }
                if (text_y < overlay_rect->y) {
                    text_y = overlay_rect->y;
                }
                ui_draw_text(renderer, text_x, text_y, label, label_color, 1);
            }
        }
    }
}

// Draws the automation mode overlay across the visible timeline lanes.
static void draw_timeline_automation_overlay(SDL_Renderer* renderer,
                                             const SDL_Rect* rect,
                                             int track_y,
                                             int track_height,
                                             int track_spacing,
                                             int track_count,
                                             int content_left,
                                             int content_width) {
    if (!renderer || !rect || track_height <= 0 || content_width <= 0) {
        return;
    }
    int lanes = track_count > 0 ? track_count : 1;
    int timeline_bottom = track_y + (lanes * (track_height + track_spacing)) - track_spacing;
    if (timeline_bottom <= track_y) {
        return;
    }
    SDL_Rect overlay_rect = {rect->x, track_y, rect->w, timeline_bottom - track_y};
    SDL_SetRenderDrawColor(renderer, 10, 12, 16, 140);
    SDL_RenderFillRect(renderer, &overlay_rect);
    SDL_SetRenderDrawColor(renderer, 120, 150, 175, 220);
    for (int lane = 0; lane < lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        int baseline_y = lane_top + track_height / 2;
        SDL_RenderDrawLine(renderer, content_left, baseline_y, content_left + content_width, baseline_y);
    }
}

static void draw_timeline_automation_segment(SDL_Renderer* renderer,
                                             int baseline_y,
                                             int range,
                                             float window_start_seconds,
                                             float window_end_seconds,
                                             int content_left,
                                             float pixels_per_second,
                                             double seg_start_seconds,
                                             double seg_end_seconds,
                                             float start_value,
                                             float end_value) {
    if (!renderer || range <= 0 || pixels_per_second <= 0.0f) {
        return;
    }
    if (seg_end_seconds <= seg_start_seconds) {
        return;
    }
    double visible_start = seg_start_seconds;
    double visible_end = seg_end_seconds;
    if (visible_start < window_start_seconds) {
        visible_start = window_start_seconds;
    }
    if (visible_end > window_end_seconds) {
        visible_end = window_end_seconds;
    }
    if (visible_end <= visible_start) {
        return;
    }
    double span = seg_end_seconds - seg_start_seconds;
    float t0 = span > 0.0 ? (float)((visible_start - seg_start_seconds) / span) : 0.0f;
    float t1 = span > 0.0 ? (float)((visible_end - seg_start_seconds) / span) : 0.0f;
    float v0 = start_value + (end_value - start_value) * t0;
    float v1 = start_value + (end_value - start_value) * t1;
    int x0 = content_left + (int)llroundf((float)(visible_start - window_start_seconds) * pixels_per_second);
    int x1 = content_left + (int)llroundf((float)(visible_end - window_start_seconds) * pixels_per_second);
    int y0 = baseline_y - (int)llroundf(v0 * (float)range);
    int y1 = baseline_y - (int)llroundf(v1 * (float)range);
    SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
}

// Draws automation points and linear segments for a clip.
static void draw_timeline_clip_automation(SDL_Renderer* renderer,
                                          const SDL_Rect* clip_rect,
                                          const EngineClip* clip,
                                          uint64_t clip_frames,
                                          EngineAutomationTarget target,
                                          const AutomationUIState* automation_ui,
                                          int track_index,
                                          int clip_index,
                                          int sample_rate,
                                          float window_start_seconds,
                                          float window_end_seconds,
                                          int content_left,
                                          int content_right,
                                          float pixels_per_second) {
    if (!renderer || !clip_rect || !clip || clip_rect->h <= 0 || clip_frames == 0 || sample_rate <= 0) {
        return;
    }
    const EngineAutomationLane* lane = NULL;
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        if (clip->automation_lanes[i].target == target) {
            lane = &clip->automation_lanes[i];
            break;
        }
    }
    int baseline_y = clip_rect->y + clip_rect->h / 2;
    int range = clip_rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    SDL_Color line_color = {170, 210, 230, 220};
    SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);

    double clip_start_seconds = (double)clip->timeline_start_frames / (double)sample_rate;
    double clip_end_seconds = clip_start_seconds + (double)clip_frames / (double)sample_rate;
    if (clip_end_seconds <= window_start_seconds || clip_start_seconds >= window_end_seconds) {
        return;
    }
    float prev_value = 0.0f;
    double prev_seconds = clip_start_seconds;
    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t frame = point->frame > clip_frames ? clip_frames : point->frame;
            float value = point->value;
            double point_seconds = clip_start_seconds + (double)frame / (double)sample_rate;
            draw_timeline_automation_segment(renderer,
                                             baseline_y,
                                             range,
                                             window_start_seconds,
                                             window_end_seconds,
                                             content_left,
                                             pixels_per_second,
                                             prev_seconds,
                                             point_seconds,
                                             prev_value,
                                             value);
            prev_seconds = point_seconds;
            prev_value = value;
        }
    }
    draw_timeline_automation_segment(renderer,
                                     baseline_y,
                                     range,
                                     window_start_seconds,
                                     window_end_seconds,
                                     content_left,
                                     pixels_per_second,
                                     prev_seconds,
                                     clip_end_seconds,
                                     prev_value,
                                     0.0f);

    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t frame = point->frame > clip_frames ? clip_frames : point->frame;
            double point_seconds = clip_start_seconds + (double)frame / (double)sample_rate;
            if (point_seconds < window_start_seconds || point_seconds > window_end_seconds) {
                continue;
            }
            int x = content_left + (int)llround((float)(point_seconds - window_start_seconds) * pixels_per_second);
            if (x < content_left - 6 || x > content_right + 6) {
                continue;
            }
            int y = baseline_y - (int)llround((double)point->value * (double)range);
            int r = 3;
            SDL_Rect dot = {x - r, y - r, r * 2, r * 2};
            bool selected = automation_ui &&
                            automation_ui->track_index == track_index &&
                            automation_ui->clip_index == clip_index &&
                            automation_ui->point_index == i &&
                            automation_ui->target == target;
            if (selected) {
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 120, 150, 170, 255);
            }
            SDL_RenderFillRect(renderer, &dot);
        }
    }
}

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, AppState* state) {
    if (!renderer || !rect || !state || !state->engine) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, TRACK_BG_R, TRACK_BG_G, TRACK_BG_B, 255);
    SDL_RenderFillRect(renderer, rect);

    float visible_seconds = clamp_float(state->timeline_visible_seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_float(state->timeline_vertical_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    float min_window_start = 0.0f;
    float max_window_start = 0.0f;
    timeline_get_scroll_bounds(state, visible_seconds, &min_window_start, &max_window_start);
    float window_start = clamp_float(state->timeline_window_start_seconds, min_window_start, max_window_start);
    float window_end = window_start + visible_seconds;

    const int controls_height = TIMELINE_CONTROLS_HEIGHT;
    const int track_top_offset = controls_height + TIMELINE_RULER_HEIGHT;
    const int track_y = rect->y + track_top_offset;
    int track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (track_height < 32) {
        track_height = 32;
    }
    const int track_spacing = 12;
    const int header_width = TIMELINE_TRACK_HEADER_WIDTH;
    const int content_left = rect->x + header_width + TIMELINE_BORDER_MARGIN;
    const int content_right = rect->x + rect->w - TIMELINE_BORDER_MARGIN;
    const int content_width = content_right - content_left;
    if (content_width <= 0) {
        return;
    }

    const Engine* engine = state->engine;
    const EngineTrack* tracks = engine_get_tracks(engine);
    const int track_count = engine_get_track_count(engine);
    const int sample_rate = engine_get_config(engine)->sample_rate;

    SDL_Rect header_rect = {rect->x, rect->y, rect->w, controls_height};
    SDL_SetRenderDrawColor(renderer, 28, 32, 40, 255);
    SDL_RenderFillRect(renderer, &header_rect);
    SDL_SetRenderDrawColor(renderer, 50, 55, 70, 255);
    SDL_RenderDrawLine(renderer, rect->x, rect->y + controls_height - 1, rect->x + rect->w, rect->y + controls_height - 1);

    TimelineControlsUI* controls = (TimelineControlsUI*)&state->timeline_controls;
    int controls_left = rect->x + 10;
    int button_h = controls_height - 6;
    if (button_h < 14) {
        button_h = 14;
    }
    int button_y = rect->y + (controls_height - button_h) / 2;
    int button_w_small = 22;
    int button_spacing = 6;
    controls->add_rect = (SDL_Rect){controls_left, button_y, button_w_small, button_h};
    controls->remove_rect = (SDL_Rect){controls_left + button_w_small + button_spacing, button_y, button_w_small, button_h};
    controls->loop_toggle_rect = (SDL_Rect){controls->remove_rect.x + button_w_small + button_spacing, button_y, 36, button_h};
    controls->snap_toggle_rect = (SDL_Rect){controls->loop_toggle_rect.x + controls->loop_toggle_rect.w + button_spacing,
                                            button_y, 40, button_h};
    controls->automation_toggle_rect = (SDL_Rect){controls->snap_toggle_rect.x + controls->snap_toggle_rect.w + button_spacing,
                                                  button_y, 44, button_h};
    controls->automation_target_rect = (SDL_Rect){controls->automation_toggle_rect.x + controls->automation_toggle_rect.w + button_spacing,
                                                  button_y, 40, button_h};
    controls->tempo_toggle_rect = (SDL_Rect){controls->automation_target_rect.x + controls->automation_target_rect.w + button_spacing,
                                             button_y, 52, button_h};
    controls->automation_label_toggle_rect = (SDL_Rect){controls->tempo_toggle_rect.x + controls->tempo_toggle_rect.w + button_spacing,
                                                        button_y, 40, button_h};

    bool remove_enabled = track_count > 0;
    draw_timeline_button(renderer, &controls->add_rect, "+", controls->add_hovered, true);
    draw_timeline_button(renderer, &controls->remove_rect, "-", controls->remove_hovered, remove_enabled);
    draw_timeline_button(renderer, &controls->loop_toggle_rect, "LOOP", controls->loop_toggle_hovered, true);
    draw_timeline_button(renderer, &controls->snap_toggle_rect, "SNAP", controls->snap_toggle_hovered, true);
    draw_timeline_button(renderer, &controls->automation_toggle_rect, "AUTO", controls->automation_toggle_hovered, true);
    const char* target_label = state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN ? "PAN" : "VOL";
    draw_timeline_button(renderer, &controls->automation_target_rect, target_label, controls->automation_target_hovered, true);
    draw_timeline_button(renderer, &controls->tempo_toggle_rect, "TEMPO", controls->tempo_toggle_hovered, true);
    draw_timeline_button(renderer, &controls->automation_label_toggle_rect, "VAL", controls->automation_label_toggle_hovered, true);
    if (state->loop_enabled) {
        SDL_SetRenderDrawColor(renderer, 130, 200, 180, 255);
        SDL_RenderDrawRect(renderer, &controls->loop_toggle_rect);
    }
    if (state->timeline_snap_enabled) {
        SDL_SetRenderDrawColor(renderer, 130, 160, 210, 255);
        SDL_RenderDrawRect(renderer, &controls->snap_toggle_rect);
    }
    if (state->timeline_automation_mode) {
        SDL_SetRenderDrawColor(renderer, 110, 150, 200, 255);
        SDL_RenderDrawRect(renderer, &controls->automation_toggle_rect);
    }
    if (state->timeline_tempo_overlay_enabled) {
        SDL_SetRenderDrawColor(renderer, 120, 170, 210, 255);
        SDL_RenderDrawRect(renderer, &controls->tempo_toggle_rect);
    }
    if (state->timeline_automation_labels_enabled) {
        SDL_SetRenderDrawColor(renderer, 150, 170, 220, 255);
        SDL_RenderDrawRect(renderer, &controls->automation_label_toggle_rect);
    }

    controls->loop_start_rect = (SDL_Rect){0,0,0,0};
    controls->loop_end_rect = (SDL_Rect){0,0,0,0};

    const float pixels_per_second = (float)content_width / visible_seconds;
    int grid_lanes = track_count > 0 ? track_count : 1;

    bool have_loop_controls = state->loop_enabled && sample_rate > 0 && state->loop_end_frame > state->loop_start_frame;
    bool have_loop_region = false;
    SDL_Rect loop_region_rect = {0, 0, 0, 0};
    if (have_loop_controls) {
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        float loop_start_local = loop_start_sec - window_start;
        float loop_end_local = loop_end_sec - window_start;
        if (loop_start_local < 0.0f) loop_start_local = 0.0f;
        if (loop_end_local < 0.0f) loop_end_local = 0.0f;
        if (loop_start_local > visible_seconds) loop_start_local = visible_seconds;
        if (loop_end_local > visible_seconds) loop_end_local = visible_seconds;
        int loop_start_x = content_left + (int)roundf(loop_start_local * pixels_per_second);
        int loop_end_x = content_left + (int)roundf(loop_end_local * pixels_per_second);
        if (loop_start_x < content_left) loop_start_x = content_left;
        if (loop_start_x > content_left + content_width) loop_start_x = content_left + content_width;
        if (loop_end_x < content_left) loop_end_x = content_left;
        if (loop_end_x > content_left + content_width) loop_end_x = content_left + content_width;
        if (loop_end_x < loop_start_x) {
            int tmp = loop_end_x;
            loop_end_x = loop_start_x;
            loop_start_x = tmp;
        }
        int top_zone_y = rect->y + controls_height + 2;
        int top_zone_h = TIMELINE_RULER_HEIGHT - 4;
        if (top_zone_h < 10) top_zone_h = 10;
        int handle_width = 10;
        int handle_height = top_zone_h - 4;
        if (handle_height < 8) handle_height = 8;
        int handle_y = top_zone_y + (top_zone_h - handle_height) / 2;
        controls->loop_start_rect = (SDL_Rect){loop_start_x - handle_width / 2, handle_y, handle_width, handle_height};
        controls->loop_end_rect = (SDL_Rect){loop_end_x - handle_width / 2, handle_y, handle_width, handle_height};

        if (loop_end_x > loop_start_x) {
            have_loop_region = true;
            int loop_top = rect->y + controls_height;
            int loop_bottom = track_y + grid_lanes * (track_height + track_spacing) - track_spacing;
            int loop_height = loop_bottom - loop_top;
            if (loop_height < 0) loop_height = 0;
            loop_region_rect = (SDL_Rect){
                loop_start_x,
                loop_top,
                loop_end_x - loop_start_x,
                loop_height
            };
        }
    }

    if (have_loop_region) {
        SDL_Color loop_fill = {60, 120, 140, 50};
        SDL_Color loop_border = {90, 170, 190, 180};
        SDL_SetRenderDrawColor(renderer, loop_fill.r, loop_fill.g, loop_fill.b, loop_fill.a);
        SDL_RenderFillRect(renderer, &loop_region_rect);
        SDL_SetRenderDrawColor(renderer, loop_border.r, loop_border.g, loop_border.b, loop_border.a);
        SDL_RenderDrawRect(renderer, &loop_region_rect);
    }

    state->tempo_map.sample_rate = (double)sample_rate;
    BeatGridCache beat_cache;
    TimeGridCache time_cache;
    beat_grid_cache_init(&beat_cache);
    time_grid_cache_init(&time_cache);
    bool beat_mode_ready = state->timeline_view_in_beats
        && state->tempo_map.event_count > 0
        && state->tempo_map.sample_rate > 0.0;
    if (beat_mode_ready) {
        beat_grid_cache_build(&beat_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines,
                              &state->tempo_map,
                              &state->time_signature_map);
    } else {
        time_grid_cache_build(&time_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines);
    }
    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        if (beat_cache.active) {
            beat_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &beat_cache);
        } else if (time_cache.active) {
            time_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &time_cache);
        } else {
            draw_timeline_grid(renderer,
                               content_left,
                               content_width,
                               lane_top,
                               track_height,
                               pixels_per_second,
                               visible_seconds,
                               state->timeline_show_all_grid_lines,
                               window_start,
                               state->timeline_view_in_beats,
                               &state->tempo_map,
                               &state->time_signature_map);
        }
    }
    beat_grid_cache_free(&beat_cache);
    time_grid_cache_free(&time_cache);

    SDL_Color header_bg = {30, 30, 38, 255};
    SDL_Color header_border = {70, 70, 90, 255};
    SDL_Color header_text = {200, 200, 210, 255};
    int active_track = (state->selected_track_index >= 0 && state->selected_track_index < track_count)
                           ? state->selected_track_index
                           : -1;
    const TrackNameEditor* editor = &state->track_name_editor;
    SDL_Point mouse_point = {state->mouse_x, state->mouse_y};

    if (tracks && track_count > 0) {
        for (int t = 0; t < track_count; ++t) {
            const EngineTrack* track = &tracks[t];
            int lane_top = track_y + t * (track_height + track_spacing);

            SDL_Rect header_rect = {rect->x + 8, lane_top + 4, header_width - 16, track_height - 8};
            SDL_Color header_fill = header_bg;
            SDL_Color label_color = header_text;
            if (active_track == t) {
                int r = header_fill.r + 32;
                int g = header_fill.g + 32;
                int b = header_fill.b + 48;
                header_fill.r = (Uint8)(r > 255 ? 255 : r);
                header_fill.g = (Uint8)(g > 255 ? 255 : g);
                header_fill.b = (Uint8)(b > 255 ? 255 : b);
            }
            if (track && track->muted) {
                header_fill.r = (Uint8)((int)header_fill.r * 3 / 5);
                header_fill.g = (Uint8)((int)header_fill.g * 3 / 5);
                header_fill.b = (Uint8)((int)header_fill.b * 3 / 5);
                label_color = (SDL_Color){160, 160, 170, 255};
            }
            SDL_SetRenderDrawColor(renderer, header_fill.r, header_fill.g, header_fill.b, header_fill.a);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, header_border.r, header_border.g, header_border.b, header_border.a);
            SDL_RenderDrawRect(renderer, &header_rect);
            char default_label[32];
            const char* label = NULL;
            if (editor && editor->editing && editor->track_index == t) {
                label = editor->buffer;
            } else if (track && track->name[0]) {
                label = track->name;
            }
            if (!label || label[0] == '\0') {
                snprintf(default_label, sizeof(default_label), "Track %d", t + 1);
                label = default_label;
            }

            int button_w = 18;
            int button_h = 16;
            int button_spacing = 4;
            int buttons_total = button_w * 2 + button_spacing;
            int buttons_x = header_rect.x + header_rect.w - buttons_total - 8;
            if (buttons_x < header_rect.x + 36) {
                buttons_x = header_rect.x + 36;
            }
            int button_y = header_rect.y + header_rect.h - button_h - 4;
            SDL_Rect mute_rect = {buttons_x, button_y, button_w, button_h};
            SDL_Rect solo_rect = {buttons_x + button_w + button_spacing, button_y, button_w, button_h};

            bool mute_hover = SDL_PointInRect(&mouse_point, &mute_rect);
            bool solo_hover = SDL_PointInRect(&mouse_point, &solo_rect);
            SDL_Color mute_active = {170, 80, 80, 255};
            SDL_Color solo_active = {90, 150, 220, 255};
            draw_toggle_button(renderer, &mute_rect, "M", track ? track->muted : false, mute_hover, mute_active);
            draw_toggle_button(renderer, &solo_rect, "S", track ? track->solo : false, solo_hover, solo_active);

            int text_x = header_rect.x + 6;
            int text_max_x = mute_rect.x - 4;
            int available_px = text_max_x - text_x;
            available_px = (int)((float)available_px * 1.5f);
            char text_buf[ENGINE_CLIP_NAME_MAX];
            const char* display = label;
            if (available_px > 0) {
                fit_label_ellipsis(label, available_px, 1.0f, text_buf, sizeof(text_buf));
                display = text_buf;
            }
            int label_text_y = header_rect.y + 4;
            ui_draw_text(renderer, text_x, label_text_y, display, label_color, 1);
            if (editor && editor->editing && editor->track_index == t) {
                float scale = 1.0f;
                int caret_limit = text_max_x;
                if (caret_limit < text_x) caret_limit = text_x;
                char temp[ENGINE_CLIP_NAME_MAX];
                int len = (int)strlen(editor->buffer);
                int caret_x = text_x;
                int target_index = editor->cursor;
                if (target_index < 0) target_index = 0;
                if (target_index > len) target_index = len;
                snprintf(temp, sizeof(temp), "%.*s", target_index, editor->buffer);
                caret_x = text_x + ui_measure_text_width(temp, scale);
                if (caret_x > caret_limit) {
                    caret_x = caret_limit;
                }
                int caret_h = ui_font_line_height(scale);
                SDL_Rect caret_rect = {
                    caret_x,
                    label_text_y,
                    2,
                    caret_h
                };
                SDL_SetRenderDrawColor(renderer, 220, 230, 255, 255);
                SDL_RenderFillRect(renderer, &caret_rect);
            }

            if (!track || track->clip_count <= 0) {
                continue;
            }

            for (int i = 0; i < track->clip_count; ++i) {
                const EngineClip* clip = &track->clips[i];
                if (!clip) {
                    continue;
                }

                const uint64_t start_frame = clip->timeline_start_frames;
                uint64_t frame_count = clip->duration_frames;
                uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
                uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
                if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                    frame_count = clip_available;
                }
                const double clip_sec = (double)frame_count / (double)sample_rate;
                const double start_sec = (double)start_frame / (double)sample_rate;

                double clip_end_sec = start_sec + clip_sec;
                double visible_start_sec = start_sec;
                if (visible_start_sec < (double)window_start) {
                    visible_start_sec = (double)window_start;
                }
                double visible_end_sec = clip_end_sec;
                double window_end_sec = (double)window_end;
                if (visible_end_sec > window_end_sec) {
                    visible_end_sec = window_end_sec;
                }
                if (visible_end_sec <= visible_start_sec) {
                    continue;
                }

                double visible_offset = visible_start_sec - (double)window_start;
                int clip_x = content_left + (int)round(visible_offset * pixels_per_second);
                int clip_w = (int)round((visible_end_sec - visible_start_sec) * pixels_per_second);
                if (clip_w < 4) {
                    clip_w = 4;
                }

                SDL_Rect clip_rect = {
                    clip_x,
                    lane_top + 8,
                    clip_w,
                    track_height - 16,
                };

                bool is_selected = timeline_clip_is_selected(state, t, i);
                SDL_SetRenderDrawColor(renderer, CLIP_COLOR_R, CLIP_COLOR_G, CLIP_COLOR_B, 230);
                SDL_RenderFillRect(renderer, &clip_rect);

                if (is_selected) {
                    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 220);
                } else {
                    SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
                }
                SDL_RenderDrawRect(renderer, &clip_rect);

                const char* media_path = engine_clip_get_media_path(clip);
                if (clip->media && clip->media->frame_count > 0 &&
                    media_path && media_path[0] != '\0') {
                    int wave_label_pad = 4;
                    float label_scale = 1.3f;
                    int label_h = ui_font_line_height(label_scale);
                    SDL_Rect waveform_rect = clip_rect;
                    waveform_rect.y += label_h + wave_label_pad;
                    waveform_rect.h -= label_h + wave_label_pad + 2;
                    if (waveform_rect.h < 8) {
                        waveform_rect = clip_rect;
                    }
                    if (waveform_rect.w > 0 && waveform_rect.h > 0) {
                        double visible_duration = visible_end_sec - visible_start_sec;
                        if (visible_duration > 0.0) {
                            uint64_t local_visible_start = clip->offset_frames;
                            double local_offset_sec = visible_start_sec - start_sec;
                            if (local_offset_sec > 0.0) {
                                local_visible_start += (uint64_t)llround(local_offset_sec * (double)sample_rate);
                            }
                            uint64_t max_frame = clip->offset_frames + frame_count;
                            if (local_visible_start > max_frame) {
                                local_visible_start = max_frame;
                            }
                            uint64_t visible_frames = (uint64_t)llround(visible_duration * (double)sample_rate);
                            if (visible_frames == 0) {
                                visible_frames = 1;
                            }
                            if (local_visible_start + visible_frames > max_frame) {
                                visible_frames = max_frame > local_visible_start ? max_frame - local_visible_start : 0;
                            }
                            if (visible_frames > 0) {
                                SDL_Color wave_color = {WAVEFORM_COLOR_R, WAVEFORM_COLOR_G, WAVEFORM_COLOR_B, 200};
                                bool rendered = false;
                                if (state->inspector.waveform.use_kit_viz_waveform) {
                                    DawKitVizWaveformRequest request = {
                                        .renderer = renderer,
                                        .cache = &state->waveform_cache,
                                        .clip = clip->media,
                                        .source_path = media_path,
                                        .target_rect = &waveform_rect,
                                        .view_start_frame = local_visible_start,
                                        .view_frame_count = visible_frames,
                                        .color = wave_color,
                                    };
                                    rendered = daw_kit_viz_render_waveform(&request);
                                }
                                if (!rendered) {
                                    waveform_render_view(renderer,
                                                         &state->waveform_cache,
                                                         clip->media,
                                                         media_path,
                                                         &waveform_rect,
                                                         local_visible_start,
                                                         visible_frames,
                                                         wave_color);
                                }
                            }
                        }
                    }
                }

                if (sample_rate > 0) {
                    int fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * pixels_per_second);
                    int fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * pixels_per_second);
                    int clip_clip_left_px = (int)round((visible_start_sec - start_sec) * pixels_per_second);
                    int clip_offset_px = clip_clip_left_px;
                    if (clip_offset_px < 0) clip_offset_px = 0;
                    double clip_total_px = (clip_end_sec - start_sec) * pixels_per_second;
                    EngineFadeCurve fade_in_curve = clip->fade_in_curve;
                    EngineFadeCurve fade_out_curve = clip->fade_out_curve;

                    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
                    int fade_in_draw = 0;
                    if (fade_in_px > 0 && clip_offset_px < fade_in_px) {
                        fade_in_draw = fade_in_px - clip_offset_px;
                        if (fade_in_draw > clip_rect.w) fade_in_draw = clip_rect.w;
                    }
                    if (fade_in_draw > 0 && fade_in_px > 0) {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);
                        for (int fx = 0; fx < fade_in_draw; ++fx) {
                            float tf = (float)(clip_offset_px + fx) / (float)fade_in_px;
                            float gain = ui_fade_curve_eval(fade_in_curve, tf);
                            float overlay = 1.0f - gain;
                            int h = (int)(overlay * clip_rect.h);
                            SDL_RenderDrawLine(renderer,
                                               clip_rect.x + fx,
                                               clip_rect.y,
                                               clip_rect.x + fx,
                                               clip_rect.y + h);
                        }
                    }

                    int fade_out_draw = 0;
                    double fade_out_start_px = clip_total_px - (double)fade_out_px;
                    if (fade_out_px > 0 && clip_total_px > 0.0) {
                        int visible_right = clip_offset_px + clip_rect.w;
                        if ((double)visible_right > fade_out_start_px) {
                            int start_x = (int)floor(fade_out_start_px - (double)clip_offset_px);
                            if (start_x < 0) start_x = 0;
                            if (start_x < clip_rect.w) {
                                fade_out_draw = clip_rect.w - start_x;
                            }
                        }
                    }
                    if (fade_out_draw > 0 && fade_out_px > 0) {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);
                        int start_x = clip_rect.w - fade_out_draw;
                        for (int fx = 0; fx < fade_out_draw; ++fx) {
                            int local_px = clip_offset_px + start_x + fx;
                            float tf = (float)((double)local_px - fade_out_start_px) / (float)fade_out_px;
                            float gain = ui_fade_curve_eval(fade_out_curve, tf);
                            int h = (int)(gain * clip_rect.h);
                            int px = clip_rect.x + start_x + fx;
                            SDL_RenderDrawLine(renderer,
                                               px,
                                               clip_rect.y,
                                               px,
                                               clip_rect.y + h);
                        }
                    }
                    ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
                }

                SDL_Color text_color = {200, 200, 210, 255};
                char name_buf[ENGINE_CLIP_NAME_MAX];
                const char* name = timeline_clip_display_name(state, clip, name_buf, sizeof(name_buf));
                int label_padding = 6;
                int label_width = clip_rect.w - label_padding * 2;
                float scale_f = 1.0f;
                int label_y = clip_rect.y + 4;
                if (label_width > 0) {
                    ui_draw_text_clipped(renderer,
                                         clip_rect.x + label_padding,
                                         label_y,
                                         name,
                                         text_color,
                                         scale_f,
                                         label_width);
                }

                if (is_selected) {
                    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 220);
                    SDL_Rect left_handle = {
                        clip_rect.x - 2,
                        clip_rect.y,
                        CLIP_HANDLE_WIDTH,
                        clip_rect.h,
                    };
                    SDL_Rect right_handle = {
                        clip_rect.x + clip_rect.w - CLIP_HANDLE_WIDTH + 2,
                        clip_rect.y,
                        CLIP_HANDLE_WIDTH,
                        clip_rect.h,
                    };
                    SDL_RenderFillRect(renderer, &left_handle);
                    SDL_RenderFillRect(renderer, &right_handle);
                }
            }
        }
    }

    if (state->timeline_tempo_overlay_enabled) {
        SDL_Rect tempo_rect = {0, 0, 0, 0};
        if (timeline_compute_tempo_overlay_rect(rect,
                                                track_y,
                                                track_height,
                                                content_left,
                                                content_width,
                                                &tempo_rect)) {
            draw_timeline_tempo_overlay(renderer,
                                        &tempo_rect,
                                        &state->tempo_map,
                                        state->tempo_overlay_ui.event_index,
                                        window_start,
                                        window_end,
                                        content_left,
                                        (float)pixels_per_second,
                                        state->timeline_automation_labels_enabled);
        }
    }

    if (state->timeline_automation_mode) {
        draw_timeline_automation_overlay(renderer,
                                         rect,
                                         track_y,
                                         track_height,
                                         track_spacing,
                                         track_count,
                                         content_left,
                                         content_width);
        if (tracks && track_count > 0 && sample_rate > 0) {
            for (int t = 0; t < track_count; ++t) {
                const EngineTrack* track = &tracks[t];
                int lane_top = track_y + t * (track_height + track_spacing);
                int clip_y = lane_top + 8;
                int clip_h = track_height - 16;
                if (clip_h < 8) {
                    clip_h = track_height;
                }
                if (!track || track->clip_count <= 0) {
                    continue;
                }
                for (int i = 0; i < track->clip_count; ++i) {
                    const EngineClip* clip = &track->clips[i];
                    if (!clip) {
                        continue;
                    }
                    uint64_t frame_count = clip->duration_frames;
                    uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
                    uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
                    if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                        frame_count = clip_available;
                    }
                    if (frame_count == 0) {
                        continue;
                    }
                    const double clip_sec = (double)frame_count / (double)sample_rate;
                    const double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                    int clip_x = content_left + (int)round((start_sec - (double)window_start) * pixels_per_second);
                    int clip_w = (int)round(clip_sec * pixels_per_second);
                    if (clip_w < 4) {
                        clip_w = 4;
                    }
                    SDL_Rect clip_rect = {
                        clip_x,
                        clip_y,
                        clip_w,
                        clip_h,
                    };
                    draw_timeline_clip_automation(renderer,
                                                  &clip_rect,
                                                  clip,
                                                  frame_count,
                                                  state->automation_ui.target,
                                                  &state->automation_ui,
                                                  t,
                                                  i,
                                                  sample_rate,
                                                  window_start,
                                                  window_end,
                                                  content_left,
                                                  content_left + content_width,
                                                  (float)pixels_per_second);
                }
            }
        }
    }

    if (have_loop_controls) {
        SDL_Color start_color = controls->loop_start_hovered || controls->adjusting_loop_start
                                     ? (SDL_Color){200, 240, 220, 255}
                                     : (SDL_Color){170, 220, 200, 255};
        SDL_Color end_color = controls->loop_end_hovered || controls->adjusting_loop_end
                                     ? (SDL_Color){200, 220, 250, 255}
                                     : (SDL_Color){160, 190, 230, 255};
        SDL_Rect start_draw = controls->loop_start_rect;
        SDL_Rect end_draw = controls->loop_end_rect;
        int shrink_w = 4;
        int shrink_h = 4;
        if (start_draw.w > shrink_w) {
            start_draw.x += shrink_w / 2;
            start_draw.w -= shrink_w;
        }
        if (start_draw.h > shrink_h) {
            start_draw.y += shrink_h / 2;
            start_draw.h -= shrink_h;
        }
        if (end_draw.w > shrink_w) {
            end_draw.x += shrink_w / 2;
            end_draw.w -= shrink_w;
        }
        if (end_draw.h > shrink_h) {
            end_draw.y += shrink_h / 2;
            end_draw.h -= shrink_h;
        }
        SDL_SetRenderDrawColor(renderer, start_color.r, start_color.g, start_color.b, start_color.a);
        SDL_RenderFillRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, 40, 70, 80, 255);
        SDL_RenderDrawRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, end_color.r, end_color.g, end_color.b, end_color.a);
        SDL_RenderFillRect(renderer, &end_draw);
        SDL_SetRenderDrawColor(renderer, 40, 70, 90, 255);
        SDL_RenderDrawRect(renderer, &end_draw);

        // Loop labels (time or beat) above handles.
        char start_label[32];
        char end_label[32];
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        format_timeline_label(loop_start_sec,
                              state->timeline_view_in_beats,
                              &state->tempo_map,
                              &state->time_signature_map,
                              start_label,
                              sizeof(start_label));
        format_timeline_label(loop_end_sec,
                              state->timeline_view_in_beats,
                              &state->tempo_map,
                              &state->time_signature_map,
                              end_label,
                              sizeof(end_label));
        SDL_Color loop_label_color = {210, 220, 235, 255};
        int label_scale = 1;
        int start_w = ui_measure_text_width(start_label, label_scale);
        int end_w = ui_measure_text_width(end_label, label_scale);
        int label_y = controls->loop_start_rect.y - ui_font_line_height(label_scale) - 2;
        if (label_y < rect->y) label_y = rect->y;
        int start_x = controls->loop_start_rect.x + (controls->loop_start_rect.w - start_w) / 2;
        int end_x = controls->loop_end_rect.x + (controls->loop_end_rect.w - end_w) / 2;
        ui_draw_text(renderer, start_x, label_y, start_label, loop_label_color, label_scale);
        ui_draw_text(renderer, end_x, label_y, end_label, loop_label_color, label_scale);
    }

    uint64_t playhead_frame = engine_get_transport_frame(engine);
    if (state->bounce_active) {
        uint64_t start = state->bounce_start_frame;
        uint64_t end = state->bounce_end_frame > start ? state->bounce_end_frame : start;
        playhead_frame = start + state->bounce_progress_frames;
        if (playhead_frame > end) {
            playhead_frame = end;
        }
    }
    const double transport_sec = (double)playhead_frame / (double)sample_rate;
    float playhead_offset = (float)((transport_sec - window_start) / visible_seconds) * (float)content_width;
    playhead_offset = clamp_float(playhead_offset, 0.0f, (float)content_width);
    int playhead_x = content_left + (int)roundf(playhead_offset);
    int timeline_bottom = track_y + (track_count > 0
                                      ? (track_count * (track_height + track_spacing)) - track_spacing
                                      : track_height);
    SDL_SetRenderDrawColor(renderer, 240, 110, 110, 255);
    SDL_RenderDrawLine(renderer, playhead_x, track_y - 8, playhead_x, timeline_bottom + 8);

    if (state->timeline_drag.active) {
        const TimelineDragState* drag = &state->timeline_drag;
        int dest_track = drag->destination_track_index;
        if (dest_track < 0) {
            dest_track = drag->track_index;
        }
        if (dest_track >= track_count) {
            dest_track = track_count > 0 ? track_count - 1 : 0;
        }
        if (dest_track >= 0 && dest_track < track_count && dest_track != drag->track_index) {
            double start_sec = drag->current_start_seconds;
            double duration_sec = drag->current_duration_seconds;
            if (duration_sec <= 0.0) {
                duration_sec = 1.0 / (double)sample_rate;
            }
            double ghost_offset = start_sec - (double)window_start;
            int ghost_x = content_left + (int)round(ghost_offset * pixels_per_second);
            int ghost_w = (int)round(duration_sec * pixels_per_second);
            if (ghost_w < 4) {
                ghost_w = 4;
            }
            int lane_top = track_y + dest_track * (track_height + track_spacing);
            SDL_Rect ghost_rect = {
                ghost_x,
                lane_top + 8,
                ghost_w,
                track_height - 16,
            };
            SDL_SetRenderDrawColor(renderer, 170, 200, 255, MOVE_GHOST_ALPHA);
            SDL_RenderFillRect(renderer, &ghost_rect);
            SDL_SetRenderDrawColor(renderer, 210, 230, 255, 200);
            SDL_RenderDrawRect(renderer, &ghost_rect);

            const EngineTrack* tracks = engine_get_tracks(state->engine);
            int count = engine_get_track_count(state->engine);
            if (tracks && drag->track_index >= 0 && drag->track_index < count) {
                const EngineTrack* src_track = &tracks[drag->track_index];
                if (src_track && drag->clip_index >= 0 && drag->clip_index < src_track->clip_count) {
                    const EngineClip* clip = &src_track->clips[drag->clip_index];
                    if (clip && clip->name[0]) {
                        SDL_Color text_color = {220, 230, 255, 255};
                        ui_draw_text(renderer, ghost_rect.x + 4, ghost_rect.y + 4, clip->name, text_color, 1);
                    }
                }
            }

            if (drag->multi_move && drag->multi_clip_count > 1) {
                char label[64];
                snprintf(label, sizeof(label), "Moving %d clips", drag->multi_clip_count);
                int text_w = ui_measure_text_width(label, 2);
                int label_x = ghost_rect.x + 6;
                int label_y = ghost_rect.y - 18;
                int content_right = content_left + content_width;
                if (label_x + text_w > content_right - 4) {
                    label_x = content_right - text_w - 4;
                }
                if (label_x < content_left + 4) {
                    label_x = content_left + 4;
                }
                if (label_y < track_y - 12) {
                    label_y = ghost_rect.y + ghost_rect.h + 6;
                }
                SDL_Color badge = {235, 240, 255, 255};
                ui_draw_text(renderer, label_x, label_y, label, badge, 1);
            }
        }
    }

    if (state->timeline_drop_active) {
        float start_sec = state->timeline_drop_seconds_snapped >= 0.0f
                              ? state->timeline_drop_seconds_snapped
                              : state->timeline_drop_seconds;
        float duration_sec = state->timeline_drop_preview_duration > 0.0f
                                 ? state->timeline_drop_preview_duration
                                 : 1.0f;
        float end_sec = start_sec + duration_sec;

        float visible_start = start_sec < window_start ? window_start : start_sec;
        float visible_end = end_sec > window_end ? window_end : end_sec;
        if (visible_end <= visible_start) {
            visible_end = visible_start + 0.01f;
        }

        int ghost_x = content_left + (int)roundf((visible_start - window_start) * pixels_per_second);
        int ghost_w = (int)roundf((visible_end - visible_start) * pixels_per_second);
        if (ghost_w < 6) {
            ghost_w = 6;
        }
        if (ghost_x < content_left) {
            ghost_w -= (content_left - ghost_x);
            ghost_x = content_left;
        }
        if (ghost_x + ghost_w > content_left + content_width) {
            ghost_w = content_left + content_width - ghost_x;
        }
        if (ghost_w < 1) {
            ghost_w = 1;
        }
        int drop_track = state->timeline_drop_track_index;
        if (drop_track < 0) {
            drop_track = 0;
        }
        if (track_count == 0) {
            drop_track = 0;
        }
        int lane_top = track_y + drop_track * (track_height + track_spacing);
        bool is_preview_track = (track_count == 0) || (drop_track >= track_count);
        if (is_preview_track) {
            SDL_Rect header_rect = {
                rect->x + 8,
                lane_top,
                header_width - 16,
                track_height
            };
            SDL_SetRenderDrawColor(renderer, 46, 54, 72, 220);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 200);
            SDL_RenderDrawRect(renderer, &header_rect);
            SDL_Color header_text = {210, 220, 235, 230};
            ui_draw_text(renderer, header_rect.x + 8, header_rect.y + 8, "New Track", header_text, 1);

            SDL_Rect lane_rect = {
                content_left,
                lane_top,
                content_width,
                track_height
            };
            SDL_SetRenderDrawColor(renderer, 52, 62, 86, 120);
            SDL_RenderFillRect(renderer, &lane_rect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 140);
            SDL_RenderDrawRect(renderer, &lane_rect);

            draw_timeline_grid(renderer,
                               content_left,
                               content_width,
                               lane_top,
                               track_height,
                               pixels_per_second,
                               visible_seconds,
                               state->timeline_show_all_grid_lines,
                               window_start,
                               state->timeline_view_in_beats,
                               &state->tempo_map,
                               &state->time_signature_map);
        }
        SDL_Rect ghost_rect = {
            ghost_x,
            lane_top + 12,
            ghost_w,
            track_height - 24,
        };
        SDL_SetRenderDrawColor(renderer, 120, 160, 220, 120);
        SDL_RenderFillRect(renderer, &ghost_rect);
        SDL_SetRenderDrawColor(renderer, 180, 210, 255, 180);
        SDL_RenderDrawRect(renderer, &ghost_rect);

        if (state->timeline_drop_label[0] != '\0') {
            SDL_Color ghost_text = {220, 230, 255, 255};
            ui_draw_text(renderer, ghost_rect.x + 4, ghost_rect.y + 4, state->timeline_drop_label, ghost_text, 1);
        }
    }

    if (state->timeline_marquee_active && state->timeline_marquee_rect.w != 0 && state->timeline_marquee_rect.h != 0) {
        SDL_Rect m = state->timeline_marquee_rect;
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 120, 170, 240, 60);
        SDL_RenderFillRect(renderer, &m);
        SDL_SetRenderDrawColor(renderer, 120, 180, 255, 180);
        SDL_RenderDrawRect(renderer, &m);
        ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
    }
}
