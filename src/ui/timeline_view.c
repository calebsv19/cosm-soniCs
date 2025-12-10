#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "ui/font.h"

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

#define CLIP_COLOR_R 85
#define CLIP_COLOR_G 125
#define CLIP_COLOR_B 210
#define TRACK_BG_R 38
#define TRACK_BG_G 44
#define TRACK_BG_B 58
#define CLIP_HANDLE_WIDTH 8

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

static void draw_timeline_grid(SDL_Renderer* renderer, int x0, int width, int top, int height, float pixels_per_second, float visible_seconds, bool show_all_lines, float window_start_seconds) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_RenderDrawRect(renderer, &(SDL_Rect){x0, top, width, height});

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);

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

    int major_ticks = (int)(visible_seconds / major_interval) + 1;
    for (int t = 0; t <= major_ticks; ++t) {
        float sec = (float)t * major_interval;
        if (sec > visible_seconds) {
            sec = visible_seconds;
        }
        int x = x0 + (int)roundf(sec * pixels_per_second);
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        float label_sec = window_start_seconds + sec;
        int total_seconds = (int)label_sec;
        int minutes = total_seconds / 60;
        int seconds = total_seconds % 60;
        char label[16];
        snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
        ui_draw_text(renderer, x + 4, top - 14, label, label_color, 1);
    }
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

    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        draw_timeline_grid(renderer, content_left, content_width, lane_top, track_height, pixels_per_second, visible_seconds, state->timeline_show_all_grid_lines, window_start);
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
                if (!clip || !clip->sampler) {
                    continue;
                }

                const uint64_t start_frame = clip->timeline_start_frames;
                uint64_t frame_count = clip->duration_frames;
                if (frame_count == 0) {
                    frame_count = engine_sampler_get_frame_count(clip->sampler);
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
                Uint8 fill_r = CLIP_COLOR_R;
                Uint8 fill_g = CLIP_COLOR_G;
                Uint8 fill_b = CLIP_COLOR_B;
                if (is_selected) {
                    int boost = state->selection_count > 1 ? 60 : 35;
                    fill_r = clamp_u8(fill_r + boost);
                    fill_g = clamp_u8(fill_g + boost + 20);
                    fill_b = clamp_u8(fill_b + boost + (state->selection_count > 1 ? 40 : 20));
                }
                SDL_SetRenderDrawColor(renderer, fill_r, fill_g, fill_b, 220);
                SDL_RenderFillRect(renderer, &clip_rect);

                SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
                SDL_RenderDrawRect(renderer, &clip_rect);

                if (sample_rate > 0) {
                    int fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * pixels_per_second);
                    int fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * pixels_per_second);
                    int clip_clip_left_px = (int)round((visible_start_sec - start_sec) * pixels_per_second);
                    int clip_clip_right_px = (int)round((clip_end_sec - visible_end_sec) * pixels_per_second);

                    int fade_in_draw = fade_in_px - clip_clip_left_px;
                    if (fade_in_draw < 0) fade_in_draw = 0;
                    if (fade_in_draw > clip_rect.w) fade_in_draw = clip_rect.w;
                    if (fade_in_draw > 0) {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);
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
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);
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
                }

                SDL_Color text_color = {200, 200, 210, 255};
                const char* name = clip->name[0] ? clip->name : "Clip";
                int label_padding = 6;
                int label_width = clip_rect.w - label_padding * 2;
                float scale_f = 1.5f; // mid-size for readability on clips
                int text_h = ui_font_line_height(scale_f);
                int label_y = clip_rect.y + (clip_rect.h - text_h) / 2;
                if (label_y < clip_rect.y + 2) {
                    label_y = clip_rect.y + 2;
                }
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
        start_sec = clamp_float(start_sec, window_start, window_end);
        float duration_sec = state->timeline_drop_preview_duration > 0.0f
                                 ? state->timeline_drop_preview_duration
                                 : 1.0f;
        float end_sec = clamp_float(start_sec + duration_sec, start_sec + 0.01f, window_end);

        int ghost_x = content_left + (int)roundf((start_sec - window_start) * pixels_per_second);
        int ghost_w = (int)roundf((end_sec - start_sec) * pixels_per_second);
        if (ghost_w < 6) {
            ghost_w = 6;
        }
        int drop_track = state->timeline_drop_track_index;
        if (drop_track < 0) {
            drop_track = 0;
        }
        if (drop_track >= track_count) {
            drop_track = track_count > 0 ? track_count - 1 : 0;
        }
        int lane_top = track_y + drop_track * (track_height + track_spacing);
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
}
