#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine.h"
#include "engine/sampler.h"
#include "ui/font5x7.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    int text_x = rect->x + (rect->w - (int)strlen(label) * 6 * 2) / 2;
    int text_y = rect->y + (rect->h - 7 * 2) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text_color, 2);
}

static void draw_timeline_grid(SDL_Renderer* renderer, int x0, int width, int top, int height, float pixels_per_second, float visible_seconds, bool show_all_lines) {
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

        int total_seconds = (int)sec;
        int minutes = total_seconds / 60;
        int seconds = total_seconds % 60;
        char label[16];
        snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
        ui_draw_text(renderer, x + 4, top - 14, label, label_color, 2);
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

    const int track_y = rect->y + 16;
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
    controls->add_rect = (SDL_Rect){rect->x + 16, rect->y + 12, 28, 24};
    controls->remove_rect = (SDL_Rect){rect->x + 48, rect->y + 12, 28, 24};

    bool remove_enabled = track_count > 0;
    draw_timeline_button(renderer, &controls->add_rect, "+", controls->add_hovered, true);
    draw_timeline_button(renderer, &controls->remove_rect, "-", controls->remove_hovered, remove_enabled);

    const float pixels_per_second = (float)content_width / visible_seconds;
    int grid_lanes = track_count > 0 ? track_count : 1;
    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        draw_timeline_grid(renderer, content_left, content_width, lane_top, track_height, pixels_per_second, visible_seconds, state->timeline_show_all_grid_lines);
    }

    SDL_Color header_bg = {30, 30, 38, 255};
    SDL_Color header_border = {70, 70, 90, 255};
    SDL_Color header_text = {200, 200, 210, 255};

    if (tracks && track_count > 0) {
        for (int t = 0; t < track_count; ++t) {
            const EngineTrack* track = &tracks[t];
            int lane_top = track_y + t * (track_height + track_spacing);

            SDL_Rect header_rect = {rect->x + 8, lane_top + 4, header_width - 16, track_height - 8};
            SDL_Color header_fill = header_bg;
            if (state->selected_track_index == t) {
                int r = header_fill.r + 32;
                int g = header_fill.g + 32;
                int b = header_fill.b + 48;
                header_fill.r = (Uint8)(r > 255 ? 255 : r);
                header_fill.g = (Uint8)(g > 255 ? 255 : g);
                header_fill.b = (Uint8)(b > 255 ? 255 : b);
            }
            SDL_SetRenderDrawColor(renderer, header_fill.r, header_fill.g, header_fill.b, header_fill.a);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, header_border.r, header_border.g, header_border.b, header_border.a);
            SDL_RenderDrawRect(renderer, &header_rect);
            char label[32];
            snprintf(label, sizeof(label), "Track %d", t + 1);
            ui_draw_text(renderer, header_rect.x + 8, header_rect.y + 8, label, header_text, 2);

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

                int clip_x = content_left + (int)round(start_sec * pixels_per_second);
                int clip_w = (int)round(clip_sec * pixels_per_second);
                if (clip_w < 4) {
                    clip_w = 4;
                }

                SDL_Rect clip_rect = {
                    clip_x,
                    lane_top + 8,
                    clip_w,
                    track_height - 16,
                };

                bool is_selected = (state->selected_track_index == t && state->selected_clip_index == i);
                SDL_SetRenderDrawColor(renderer,
                                       is_selected ? 120 : CLIP_COLOR_R,
                                       is_selected ? 170 : CLIP_COLOR_G,
                                       is_selected ? 240 : CLIP_COLOR_B,
                                       220);
                SDL_RenderFillRect(renderer, &clip_rect);

                SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
                SDL_RenderDrawRect(renderer, &clip_rect);

                SDL_Color text_color = {200, 200, 210, 255};
                const char* name = clip->name[0] ? clip->name : "Clip";
                ui_draw_text(renderer, clip_rect.x + 8, clip_rect.y + 8, name, text_color, 2);

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

    const uint64_t transport_frame = engine_get_transport_frame(engine);
    const double transport_sec = (double)transport_frame / (double)sample_rate;
    float playhead_offset = (float)(transport_sec / visible_seconds) * (float)content_width;
    playhead_offset = clamp_float(playhead_offset, 0.0f, (float)content_width);
    int playhead_x = content_left + (int)roundf(playhead_offset);
    int timeline_bottom = track_y + (track_count > 0
                                      ? (track_count * (track_height + track_spacing)) - track_spacing
                                      : track_height);
    SDL_SetRenderDrawColor(renderer, 240, 110, 110, 255);
    SDL_RenderDrawLine(renderer, playhead_x, track_y - 8, playhead_x, timeline_bottom + 8);

    if (state->timeline_drop_active) {
        float start_sec = state->timeline_drop_seconds_snapped >= 0.0f
                              ? state->timeline_drop_seconds_snapped
                              : state->timeline_drop_seconds;
        start_sec = clamp_float(start_sec, 0.0f, visible_seconds);
        float duration_sec = state->timeline_drop_preview_duration > 0.0f
                                 ? state->timeline_drop_preview_duration
                                 : 1.0f;
        float end_sec = clamp_float(start_sec + duration_sec, start_sec + 0.01f, visible_seconds);

        int ghost_x = content_left + (int)roundf(start_sec * pixels_per_second);
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
            ui_draw_text(renderer, ghost_rect.x + 6, ghost_rect.y + 6, state->timeline_drop_label, ghost_text, 2);
        }
    }
}
