#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "ui/font.h"
#include "ui/timeline_waveform.h"
#include "time/tempo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOVE_GHOST_ALPHA 140

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

static inline Uint8 clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (Uint8)value;
}

static int quantize_samples_per_pixel(int spp) {
    if (spp < 1) {
        return 1;
    }
    int upper = 1;
    while (upper < spp && upper < (1 << 30)) {
        upper <<= 1;
    }
    int lower = upper >> 1;
    if (lower < 1) {
        return upper;
    }
    return (spp - lower) < (upper - spp) ? lower : upper;
}

static float timeline_total_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0 && clip->media) {
                length = clip->media->frame_count;
            }
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    if (max_frames == 0) {
        return 0.0f;
    }
    return (float)max_frames / (float)sample_rate;
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
                               const TempoState* tempo_opt) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_RenderDrawRect(renderer, &(SDL_Rect){x0, top, width, height});

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);

    TempoState tempo = {0};
    if (tempo_opt) {
        tempo = *tempo_opt;
        tempo_state_clamp(&tempo);
    }

    if (!view_in_beats || tempo.bpm <= 0.0 || tempo.sample_rate <= 0.0) {
        if (show_all_lines) {
            int full_ticks = (int)ceilf(visible_seconds);
            for (int t = 0; t <= full_ticks; ++t) {
                float sec = (float)t;
                if (sec > visible_seconds) {
                    sec = visible_seconds;
                }
                int x = x0 + (int)roundf(sec * pixels_per_second);
                SDL_SetRenderDrawColor(renderer, 58, 58, 68, 255);
                SDL_RenderDrawLine(renderer, x, top, x, top + height);
            }
            SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);
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

    double start_beats = tempo_seconds_to_beats((double)window_start_seconds, &tempo);
    double end_beats = tempo_seconds_to_beats((double)(window_start_seconds + visible_seconds), &tempo);
    double visible_beats = end_beats - start_beats;
    if (visible_beats < 0.0001) {
        visible_beats = 0.0001;
    }

    // Minor lines at every beat when show_all_lines is on.
    if (show_all_lines) {
        int full_ticks = (int)ceil(visible_beats);
        for (int i = 0; i <= full_ticks; ++i) {
            double beat = (double)i;
            if (beat > visible_beats) {
                beat = visible_beats;
            }
            double global_beats = start_beats + beat;
            double sec = tempo_beats_to_seconds(global_beats, &tempo);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0) local_sec = 0.0;
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_SetRenderDrawColor(renderer, 58, 58, 68, 255);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
        SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);
    }

    double major_interval_beats = 1.0;
    if (visible_beats > 128.0) {
        major_interval_beats = 8.0;
    } else if (visible_beats > 64.0) {
        major_interval_beats = 4.0;
    } else if (visible_beats > 32.0) {
        major_interval_beats = 2.0;
    }

    int beats_per_bar = tempo.ts_num > 0 ? tempo.ts_num : 4;

    // Extra subdivision lines at high zoom: quarter-beat at tight zoom, half-beat at medium zoom (no labels).
    double sub_interval = 0.0;
    if (visible_beats <= 8.0) {
        sub_interval = 0.25; // quarter-beat
    } else if (visible_beats <= 32.0) {
        sub_interval = 0.5; // half-beat
    }
    if (sub_interval > 0.0) {
        SDL_SetRenderDrawColor(renderer, 70, 70, 80, 200);
        double first_sub = floor(start_beats / sub_interval) * sub_interval;
        double last_sub = end_beats + sub_interval * 0.5;
        for (double gb = first_sub; gb <= last_sub; gb += sub_interval) {
            double sec = tempo_beats_to_seconds(gb, &tempo);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
        SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);
    }

    // Major beats aligned to absolute bars/beats (not reset per window).
    double first_major = floor(start_beats / major_interval_beats) * major_interval_beats;
    double last_major = end_beats + major_interval_beats * 0.5;
    for (double gb = first_major; gb <= last_major; gb += major_interval_beats) {
        double sec = tempo_beats_to_seconds(gb, &tempo);
        double local_sec = sec - window_start_seconds;
        if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
            continue;
        }
        int x = x0 + (int)round(local_sec * (double)pixels_per_second);
        bool is_downbeat = ((int)floor(gb) % beats_per_bar) == 0;
        if (is_downbeat) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 105, 255);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
            SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);
        } else {
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }

        int bar = (int)floor(gb / (double)beats_per_bar) + 1;
        double beat_in_bar_f = fmod(gb, (double)beats_per_bar);
        if (beat_in_bar_f < 0.0) beat_in_bar_f += beats_per_bar;
        int beat_idx = (int)floor(beat_in_bar_f) + 1;
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

static void format_timeline_label(float seconds, bool view_in_beats, const TempoState* tempo, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (view_in_beats && tempo && tempo->bpm > 0.0 && tempo->sample_rate > 0.0) {
        double beats = tempo_seconds_to_beats(seconds, tempo);
        int beats_per_bar = tempo->ts_num > 0 ? tempo->ts_num : 4;
        int bar = (int)floor(beats / (double)beats_per_bar) + 1;
        double beat_in_bar_f = fmod(beats, (double)beats_per_bar);
        if (beat_in_bar_f < 0.0) beat_in_bar_f += beats_per_bar;
        int beat_idx = (int)floor(beat_in_bar_f) + 1;
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

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, const AppState* state) {
    if (!renderer || !rect || !state || !state->engine) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, TRACK_BG_R, TRACK_BG_G, TRACK_BG_B, 255);
    SDL_RenderFillRect(renderer, rect);

    float visible_seconds = clamp_float(state->timeline_visible_seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_float(state->timeline_vertical_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    float total_seconds = timeline_total_seconds(state);
    float max_window_start = total_seconds > visible_seconds ? total_seconds - visible_seconds : 0.0f;
    if (max_window_start < 0.0f) {
        max_window_start = 0.0f;
    }
    float window_start = clamp_float(state->timeline_window_start_seconds, 0.0f, max_window_start);
    float window_end = window_start + visible_seconds;

    const int controls_height = TIMELINE_CONTROLS_HEIGHT;
    const int track_top_offset = controls_height + 8;
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

    TimelineControlsUI* controls = (TimelineControlsUI*)&state->timeline_controls;
    int controls_left = rect->x + 12;
    controls->add_rect = (SDL_Rect){controls_left, rect->y + 8, 28, 24};
    controls->remove_rect = (SDL_Rect){controls_left + 36, rect->y + 8, 28, 24};
    controls->loop_toggle_rect = (SDL_Rect){controls_left + 72, rect->y + 8, 42, 24};

    bool remove_enabled = track_count > 0;
    draw_timeline_button(renderer, &controls->add_rect, "+", controls->add_hovered, true);
    draw_timeline_button(renderer, &controls->remove_rect, "-", controls->remove_hovered, remove_enabled);
    draw_timeline_button(renderer, &controls->loop_toggle_rect, "LOOP", controls->loop_toggle_hovered, true);
    if (state->loop_enabled) {
        SDL_SetRenderDrawColor(renderer, 130, 200, 180, 255);
        SDL_RenderDrawRect(renderer, &controls->loop_toggle_rect);
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
        int top_zone_y = rect->y + 8;
        int top_zone_h = track_y - rect->y - 12;
        if (top_zone_h < 12) top_zone_h = 12;
        int handle_width = 10;
        int handle_height = top_zone_h - 6;
        if (handle_height < 12) handle_height = 12;
        controls->loop_start_rect = (SDL_Rect){loop_start_x - handle_width / 2, top_zone_y + 3, handle_width, handle_height};
        controls->loop_end_rect = (SDL_Rect){loop_end_x - handle_width / 2, top_zone_y + 3, handle_width, handle_height};

        if (loop_end_x > loop_start_x) {
            have_loop_region = true;
            loop_region_rect = (SDL_Rect){
                loop_start_x,
                track_y,
                loop_end_x - loop_start_x,
                grid_lanes * (track_height + track_spacing) - track_spacing
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

    TempoState tempo = state->tempo;
    tempo.sample_rate = (double)sample_rate;
    tempo_state_clamp(&tempo);
    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
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
                           &tempo);
    }

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

                if (clip->media && clip->media->samples && clip->media->frame_count > 0) {
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
                            double pixels_per_second_wave = (double)waveform_rect.w / visible_duration;
                            double exact_spp = (double)sample_rate / pixels_per_second_wave;
                            int samples_per_pixel = (int)llround(exact_spp);
                            if (samples_per_pixel < 1) samples_per_pixel = 1;
                            int bucket_spp = quantize_samples_per_pixel(samples_per_pixel);
                            const WaveformCacheEntry* entry = waveform_cache_get(&state->waveform_cache,
                                                                                 clip->media,
                                                                                 clip->media_path,
                                                                                 bucket_spp);
                            if (entry && entry->bucket_count > 0) {
                                uint64_t local_visible_start = clip->offset_frames;
                                double local_offset_sec = visible_start_sec - start_sec;
                                if (local_offset_sec > 0.0) {
                                    local_visible_start += (uint64_t)llround(local_offset_sec * (double)sample_rate);
                                }
                                uint64_t max_frame = clip->offset_frames + frame_count;
                                if (local_visible_start > max_frame) {
                                    local_visible_start = max_frame;
                                }
                                double bucket_scale = exact_spp / (double)bucket_spp;
                                double bucket_start = (double)local_visible_start / (double)bucket_spp;
                                int mid_y = waveform_rect.y + waveform_rect.h / 2;
                                int amp = waveform_rect.h / 2 - 1;
                                if (amp < 1) {
                                    amp = 1;
                                }
                                SDL_SetRenderDrawColor(renderer, WAVEFORM_COLOR_R, WAVEFORM_COLOR_G, WAVEFORM_COLOR_B, 200);
                                bool line_mode = exact_spp <= 2.0;
                                int prev_x = 0;
                                int prev_y = 0;
                                for (int px = 0; px < waveform_rect.w; ++px) {
                                    double pos = bucket_start + (double)px * bucket_scale;
                                    int bucket = (int)floor(pos);
                                    if (bucket < 0 || bucket >= entry->bucket_count) {
                                        continue;
                                    }
                                    if (bucket + 1 >= entry->bucket_count) {
                                        break;
                                    }
                                    double t = pos - (double)bucket;
                                    float min_a = entry->mins[bucket];
                                    float max_a = entry->maxs[bucket];
                                    float min_b = entry->mins[bucket + 1];
                                    float max_b = entry->maxs[bucket + 1];
                                    float min_v = (float)((1.0 - t) * min_a + t * min_b);
                                    float max_v = (float)((1.0 - t) * max_a + t * max_b);
                                    if (line_mode) {
                                        float v = 0.5f * (min_v + max_v);
                                        int y = mid_y - (int)llround((double)v * (double)amp);
                                        int x = waveform_rect.x + px;
                                        if (px > 0) {
                                            SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
                                        }
                                        prev_x = x;
                                        prev_y = y;
                                    } else {
                                        int y_top = mid_y - (int)llround((double)max_v * (double)amp);
                                        int y_bot = mid_y - (int)llround((double)min_v * (double)amp);
                                        if (y_top > y_bot) {
                                            int tmp = y_top;
                                            y_top = y_bot;
                                            y_bot = tmp;
                                        }
                                        SDL_RenderDrawLine(renderer,
                                                           waveform_rect.x + px,
                                                           y_top,
                                                           waveform_rect.x + px,
                                                           y_bot);
                                    }
                                }
                            }
                        }
                    }
                }

                if (sample_rate > 0) {
                    int fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * pixels_per_second);
                    int fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * pixels_per_second);
                    int clip_clip_left_px = (int)round((visible_start_sec - start_sec) * pixels_per_second);
                    int clip_clip_right_px = (int)round((clip_end_sec - visible_end_sec) * pixels_per_second);

                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    int fade_in_draw = fade_in_px - clip_clip_left_px;
                    if (fade_in_draw < 0) fade_in_draw = 0;
                    if (fade_in_draw > clip_rect.w) fade_in_draw = clip_rect.w;
                    if (fade_in_draw > 0) {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);
                        for (int fx = 0; fx < fade_in_draw; ++fx) {
                            float tf = fade_in_draw > 0 ? (float)fx / (float)fade_in_draw : 0.0f;
                            if (tf > 1.0f) tf = 1.0f;
                            int h = (int)((1.0f - tf) * clip_rect.h);
                            SDL_RenderDrawLine(renderer,
                                               clip_rect.x + fx,
                                               clip_rect.y,
                                               clip_rect.x + fx,
                                               clip_rect.y + h);
                        }
                    }

                    int fade_out_draw = fade_out_px - clip_clip_right_px;
                    if (fade_out_draw < 0) fade_out_draw = 0;
                    if (fade_out_draw > clip_rect.w) fade_out_draw = clip_rect.w;
                    if (fade_out_draw > 0) {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);
                        for (int fx = 0; fx < fade_out_draw; ++fx) {
                            float tf = fade_out_draw > 0 ? (float)fx / (float)fade_out_draw : 0.0f;
                            if (tf > 1.0f) tf = 1.0f;
                            int h = (int)(tf * clip_rect.h);
                            int px = clip_rect.x + clip_rect.w - fade_out_draw + fx;
                            SDL_RenderDrawLine(renderer,
                                               px,
                                               clip_rect.y,
                                               px,
                                               clip_rect.y + h);
                        }
                    }
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                }

                SDL_Color text_color = {200, 200, 210, 255};
                const char* name = clip->name[0] ? clip->name : "Clip";
                int label_padding = 6;
                int label_width = clip_rect.w - label_padding * 2;
                float scale_f = 1.3f;
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

    if (have_loop_controls) {
        SDL_Color start_color = controls->loop_start_hovered || controls->adjusting_loop_start
                                     ? (SDL_Color){200, 240, 220, 255}
                                     : (SDL_Color){170, 220, 200, 255};
        SDL_Color end_color = controls->loop_end_hovered || controls->adjusting_loop_end
                                     ? (SDL_Color){200, 220, 250, 255}
                                     : (SDL_Color){160, 190, 230, 255};
        SDL_SetRenderDrawColor(renderer, start_color.r, start_color.g, start_color.b, start_color.a);
        SDL_RenderFillRect(renderer, &controls->loop_start_rect);
        SDL_SetRenderDrawColor(renderer, 40, 70, 80, 255);
        SDL_RenderDrawRect(renderer, &controls->loop_start_rect);
        SDL_SetRenderDrawColor(renderer, end_color.r, end_color.g, end_color.b, end_color.a);
        SDL_RenderFillRect(renderer, &controls->loop_end_rect);
        SDL_SetRenderDrawColor(renderer, 40, 70, 90, 255);
        SDL_RenderDrawRect(renderer, &controls->loop_end_rect);

        // Loop labels (time or beat) above handles.
        TempoState loop_tempo = state->tempo;
        loop_tempo.sample_rate = (double)sample_rate;
        tempo_state_clamp(&loop_tempo);
        char start_label[32];
        char end_label[32];
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        format_timeline_label(loop_start_sec, state->timeline_view_in_beats, &loop_tempo, start_label, sizeof(start_label));
        format_timeline_label(loop_end_sec, state->timeline_view_in_beats, &loop_tempo, end_label, sizeof(end_label));
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
                               &tempo);
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
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 120, 170, 240, 60);
        SDL_RenderFillRect(renderer, &m);
        SDL_SetRenderDrawColor(renderer, 120, 180, 255, 180);
        SDL_RenderDrawRect(renderer, &m);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}
