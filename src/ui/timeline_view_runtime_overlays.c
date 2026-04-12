#include "ui/timeline_view_runtime_overlays.h"

#include "engine/engine.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/timeline_view.h"
#include "ui/timeline_view_controls.h"
#include "ui/timeline_view_grid.h"

#include <math.h>
#include <stdio.h>

#define MOVE_GHOST_ALPHA 140

static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void timeline_draw_text_in_rect_clipped(SDL_Renderer* renderer,
                                               const SDL_Rect* rect,
                                               const char* text,
                                               SDL_Color color,
                                               int pad_x,
                                               int pad_y,
                                               float scale) {
    int max_w;
    if (!renderer || !rect || !text || text[0] == '\0') {
        return;
    }
    max_w = rect->w - pad_x * 2;
    if (max_w <= 0) {
        return;
    }
    ui_draw_text_clipped(renderer,
                         rect->x + pad_x,
                         rect->y + pad_y,
                         text,
                         color,
                         scale,
                         max_w);
}

void timeline_view_render_runtime_overlays(SDL_Renderer* renderer,
                                           const SDL_Rect* rect,
                                           AppState* state,
                                           const TimelineTheme* theme,
                                           int sample_rate,
                                           float window_start,
                                           float window_end,
                                           float visible_seconds,
                                           int content_left,
                                           int content_width,
                                           int track_y,
                                           int track_height,
                                           int track_spacing,
                                           int track_count,
                                           int header_width,
                                           float pixels_per_second) {
    if (!renderer || !rect || !state || !theme || !state->engine || sample_rate <= 0) {
        return;
    }

    const Engine* engine = state->engine;
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
    SDL_SetRenderDrawColor(renderer, theme->playhead.r, theme->playhead.g, theme->playhead.b, theme->playhead.a);
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
            SDL_Rect ghost_rect = {0, 0, 0, 0};
            timeline_view_compute_lane_clip_rect(lane_top,
                                                 track_height,
                                                 ghost_x,
                                                 ghost_w,
                                                 &ghost_rect);
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
                        TimelineViewTextMetricsSnapshot metrics = {0};
                        timeline_view_get_text_metrics_snapshot(&metrics);
                        SDL_Color text_color = {220, 230, 255, 255};
                        timeline_draw_text_in_rect_clipped(renderer,
                                                           &ghost_rect,
                                                           clip->name,
                                                           text_color,
                                                           metrics.overlay_label_pad_x,
                                                           metrics.label_pad_y,
                                                           1.0f);
                    }
                }
            }

            if (drag->multi_move && drag->multi_clip_count > 1) {
                TimelineViewTextMetricsSnapshot metrics = {0};
                timeline_view_get_text_metrics_snapshot(&metrics);
                char label[64];
                snprintf(label, sizeof(label), "Moving %d clips", drag->multi_clip_count);
                int text_h = metrics.text_h_1x;
                int label_x = ghost_rect.x + metrics.overlay_edge_pad;
                int label_y = ghost_rect.y - text_h - metrics.overlay_badge_gap;
                int min_x = content_left + metrics.overlay_edge_pad;
                int max_x = content_left + content_width - metrics.overlay_edge_pad;
                int max_w;
                if (label_x < min_x) {
                    label_x = min_x;
                }
                if (label_y < track_y - (metrics.overlay_badge_gap * 2)) {
                    label_y = ghost_rect.y + ghost_rect.h + metrics.overlay_badge_gap;
                }
                max_w = max_x - label_x;
                SDL_Color badge = {235, 240, 255, 255};
                if (max_w > 0) {
                    ui_draw_text_clipped(renderer, label_x, label_y, label, badge, 1.0f, max_w);
                }
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
            TimelineTrackHeaderLayout preview_header = {0};
            timeline_view_compute_track_header_layout(rect,
                                                      lane_top,
                                                      track_height,
                                                      header_width,
                                                      &preview_header);
            SDL_Rect header_rect = preview_header.header_rect;
            SDL_SetRenderDrawColor(renderer, 46, 54, 72, 220);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 200);
            SDL_RenderDrawRect(renderer, &header_rect);
            SDL_Color header_text = {210, 220, 235, 230};
            int preview_text_w = preview_header.text_max_x - preview_header.text_x;
            if (preview_text_w > 0) {
                ui_draw_text_clipped(renderer,
                                     preview_header.text_x,
                                     preview_header.text_y,
                                     "New Track",
                                     header_text,
                                     1.0f,
                                     preview_text_w);
            }

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

            timeline_view_draw_grid(renderer,
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
        SDL_Rect ghost_rect = {0, 0, 0, 0};
        timeline_view_compute_lane_clip_rect(lane_top,
                                             track_height,
                                             ghost_x,
                                             ghost_w,
                                             &ghost_rect);
        SDL_SetRenderDrawColor(renderer, 120, 160, 220, 120);
        SDL_RenderFillRect(renderer, &ghost_rect);
        SDL_SetRenderDrawColor(renderer, 180, 210, 255, 180);
        SDL_RenderDrawRect(renderer, &ghost_rect);

        if (state->timeline_drop_label[0] != '\0') {
            TimelineViewTextMetricsSnapshot metrics = {0};
            timeline_view_get_text_metrics_snapshot(&metrics);
            SDL_Color ghost_text = {220, 230, 255, 255};
            timeline_draw_text_in_rect_clipped(renderer,
                                               &ghost_rect,
                                               state->timeline_drop_label,
                                               ghost_text,
                                               metrics.overlay_label_pad_x,
                                               metrics.label_pad_y,
                                               1.0f);
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
