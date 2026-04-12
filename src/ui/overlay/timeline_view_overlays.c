#include "ui/timeline_view_overlays.h"

#include "ui/font.h"
#include "ui/timeline_view.h"

#include <math.h>
#include <stdio.h>

#define TEMPO_OVERLAY_MIN_BPM 20.0
#define TEMPO_OVERLAY_MAX_BPM 200.0

bool timeline_view_compute_tempo_overlay_rect(const SDL_Rect* timeline_rect,
                                              int track_y,
                                              int track_height,
                                              int content_left,
                                              int content_width,
                                              SDL_Rect* out_rect) {
    SDL_Rect lane_rect = {0, 0, 0, 0};
    if (!timeline_rect || !out_rect || track_height <= 0 || content_width <= 0) {
        return false;
    }
    timeline_view_compute_lane_clip_rect(track_y,
                                         track_height,
                                         content_left,
                                         content_width,
                                         &lane_rect);
    if (lane_rect.h < 8) {
        return false;
    }
    *out_rect = lane_rect;
    return true;
}

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

void timeline_view_draw_tempo_overlay(SDL_Renderer* renderer,
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

void timeline_view_draw_automation_overlay(SDL_Renderer* renderer,
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

void timeline_view_draw_clip_automation(SDL_Renderer* renderer,
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
