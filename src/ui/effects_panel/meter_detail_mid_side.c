#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/effects_panel_meter_history_cache.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static void resolve_mid_side_theme(SDL_Color* fill,
                                   SDL_Color* border,
                                   SDL_Color* history_bg,
                                   SDL_Color* meter_bg,
                                   SDL_Color* lane_bg,
                                   SDL_Color* mid_marker,
                                   SDL_Color* side_marker) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (fill) *fill = theme.inspector_fill;
        if (border) *border = theme.pane_border;
        if (history_bg) *history_bg = theme.timeline_fill;
        if (meter_bg) *meter_bg = theme.slider_track;
        if (lane_bg) *lane_bg = theme.control_fill;
        if (mid_marker) *mid_marker = theme.slider_handle;
        if (side_marker) *side_marker = theme.accent_warning;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (history_bg) *history_bg = (SDL_Color){26, 28, 36, 255};
    if (meter_bg) *meter_bg = (SDL_Color){50, 54, 66, 255};
    if (lane_bg) *lane_bg = (SDL_Color){60, 64, 74, 255};
    if (mid_marker) *mid_marker = (SDL_Color){150, 210, 255, 255};
    if (side_marker) *side_marker = (SDL_Color){255, 190, 140, 255};
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float meter_db(float linear) {
    if (linear <= 1e-9f) {
        return -90.0f;
    }
    return 20.0f * log10f(linear);
}

static float history_get_mid_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->mid_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->mid_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_MID_SIDE_HISTORY_POINTS;
    idx %= FX_METER_MID_SIDE_HISTORY_POINTS;
    return history->mid_values[idx];
}

static float history_get_side_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->mid_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->mid_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_MID_SIDE_HISTORY_POINTS;
    idx %= FX_METER_MID_SIDE_HISTORY_POINTS;
    return history->side_values[idx];
}

static bool render_mid_side_history_with_adapter(SDL_Renderer* renderer,
                                                 const SDL_Rect* mid_hist,
                                                 const SDL_Rect* side_hist,
                                                 const EffectsMeterHistory* history) {
    if (!renderer || !mid_hist || !side_hist || !history || history->mid_count <= 1 || mid_hist->w <= 0 || side_hist->w <= 0) {
        return false;
    }
    const int count = history->mid_count;
    float mid_samples[FX_METER_MID_SIDE_HISTORY_POINTS];
    float side_samples[FX_METER_MID_SIDE_HISTORY_POINTS];
    for (int i = 0; i < count; ++i) {
        mid_samples[i] = clampf(history_get_mid_by_age(history, i), 0.0f, 1.0f);
        side_samples[i] = clampf(history_get_side_by_age(history, i), 0.0f, 1.0f);
    }

    KitVizVecSegment mid_segments[FX_METER_MID_SIDE_HISTORY_POINTS];
    KitVizVecSegment side_segments[FX_METER_MID_SIDE_HISTORY_POINTS];
    size_t mid_segment_count = 0;
    size_t side_segment_count = 0;
    CoreResult mid_r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(mid_samples,
                                                                               (uint32_t)count,
                                                                               FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                               mid_hist,
                                                                               (DawKitVizMeterPlotRange){0.0f, 1.0f},
                                                                               mid_segments,
                                                                               FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                               &mid_segment_count);
    CoreResult side_r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(side_samples,
                                                                                (uint32_t)count,
                                                                                FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                                side_hist,
                                                                                (DawKitVizMeterPlotRange){0.0f, 1.0f},
                                                                                side_segments,
                                                                                FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                                &side_segment_count);
    if (mid_r.code != CORE_OK || side_r.code != CORE_OK || mid_segment_count == 0 || side_segment_count == 0) {
        return false;
    }
    SDL_Color mid_trace = {0};
    SDL_Color side_trace = {0};
    resolve_mid_side_theme(NULL, NULL, NULL, NULL, NULL, &mid_trace, &side_trace);
    daw_kit_viz_meter_render_segments(renderer,
                                      mid_segments,
                                      mid_segment_count,
                                      mid_trace);
    daw_kit_viz_meter_render_segments(renderer,
                                      side_segments,
                                      side_segment_count,
                                      side_trace);
    return true;
}

// effects_meter_render_mid_side draws mid/side history strips.
void effects_meter_render_mid_side(SDL_Renderer* renderer,
                                   const SDL_Rect* rect,
                                   const EngineFxMeterSnapshot* snapshot,
                                   const EffectsMeterHistory* history,
                                   const EffectsMeterHistoryGridContext* history_grid,
                                   SDL_Color label_color,
                                   SDL_Color dim_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color border = {0};
    SDL_Color fill = {0};
    SDL_Color history_bg = {0};
    SDL_Color meter_bg = {0};
    SDL_Color lane_bg = {0};
    SDL_Color mid_marker = {0};
    SDL_Color side_marker = {0};
    resolve_mid_side_theme(&fill, &border, &history_bg, &meter_bg, &lane_bg, &mid_marker, &side_marker);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    const int pad = max_int(12, body_h / 2 + 8);
    const int label_w = max_int(22, ui_measure_text_width("-90", 1.0f) + 8);
    const int meter_w = max_int(10, body_h / 2);
    const int history_gap = max_int(0, body_h / 5);
    const int row_gap = max_int(4, body_h / 3);
    const int section_gap = max_int(8, body_h / 2);
    const int header_text_w = rect->w - pad * 2;
    const int title_y = rect->y + max_int(4, pad / 3);
    bool valid = snapshot && snapshot->valid;
    char mid_buf[64] = {0};
    char side_buf[64] = {0};
    int mid_w = 0;
    int side_w = 0;
    int stats_rows = 1;
    if (valid) {
        float mid = clampf(snapshot->mid_rms, 0.0f, 1.5f);
        float side = clampf(snapshot->side_rms, 0.0f, 1.5f);
        snprintf(mid_buf, sizeof(mid_buf), "Mid %.1f dB", meter_db(mid));
        snprintf(side_buf, sizeof(side_buf), "Side %.1f dB", meter_db(side));
        mid_w = ui_measure_text_width(mid_buf, 1.0f);
        side_w = ui_measure_text_width(side_buf, 1.0f);
        if (mid_w + max_int(12, body_h / 2) + side_w > header_text_w) {
            stats_rows = 2;
        }
    }
    const int stats_y = title_y + title_h + row_gap;
    int header_h = (stats_y - (rect->y + pad))
        + stats_rows * body_h
        + (stats_rows - 1) * row_gap
        + section_gap;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }

    int meter_x = rect->x + pad + label_w;
    int meter_y = rect->y + pad + header_h;
    int meter_h = rect->h - pad * 2 - header_h;
    SDL_Rect meter_rect = {meter_x, meter_y, meter_w, meter_h};
    SDL_Rect history_rect = {meter_rect.x + meter_rect.w + history_gap,
                             meter_rect.y,
                             rect->x + rect->w - pad - (meter_rect.x + meter_rect.w + history_gap),
                             meter_rect.h};
    if (history_rect.w < 0) {
        history_rect.w = 0;
    }
    if (meter_rect.h <= 0 || meter_rect.w <= 0) {
        return;
    }

    ui_draw_text_clipped(renderer,
                         rect->x + pad,
                         title_y,
                         "Mid/Side",
                         label_color,
                         1.2f,
                         header_text_w);

    SDL_SetRenderDrawColor(renderer, history_bg.r, history_bg.g, history_bg.b, history_bg.a);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);

    SDL_SetRenderDrawColor(renderer, meter_bg.r, meter_bg.g, meter_bg.b, meter_bg.a);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    int lane_gap = max_int(6, body_h / 2);
    int lane_h = (meter_rect.h - lane_gap) / 2;
    SDL_Rect mid_meter = {meter_rect.x, meter_rect.y, meter_rect.w, lane_h};
    SDL_Rect side_meter = {meter_rect.x, meter_rect.y + lane_h + lane_gap, meter_rect.w, lane_h};

    SDL_SetRenderDrawColor(renderer, lane_bg.r, lane_bg.g, lane_bg.b, lane_bg.a);
    SDL_RenderFillRect(renderer, &mid_meter);
    SDL_RenderFillRect(renderer, &side_meter);

    int hist_gap = max_int(6, body_h / 2);
    int hist_h = (history_rect.h - hist_gap) / 2;
    SDL_Rect mid_hist = {history_rect.x, history_rect.y, history_rect.w, hist_h};
    SDL_Rect side_hist = {history_rect.x, history_rect.y + hist_h + hist_gap, history_rect.w, hist_h};
    if (hist_h < max_int(24, body_h + 6)) {
        mid_hist.h = 0;
        side_hist.h = 0;
    }
    if (mid_hist.h > 0) {
        effects_meter_history_grid_draw(renderer, &mid_hist, history_grid);
        effects_meter_history_grid_draw(renderer, &side_hist, history_grid);
    }

    if (valid) {
        float mid = clampf(snapshot->mid_rms, 0.0f, 1.5f);
        float side = clampf(snapshot->side_rms, 0.0f, 1.5f);
        float mid_norm = clampf(mid / 1.0f, 0.0f, 1.0f);
        float side_norm = clampf(side / 1.0f, 0.0f, 1.0f);
        int mid_y = mid_meter.y + (int)lroundf((1.0f - mid_norm) * (float)mid_meter.h);
        int side_y = side_meter.y + (int)lroundf((1.0f - side_norm) * (float)side_meter.h);
        SDL_SetRenderDrawColor(renderer, mid_marker.r, mid_marker.g, mid_marker.b, mid_marker.a);
        SDL_RenderDrawLine(renderer, mid_meter.x - 3, mid_y, mid_meter.x + mid_meter.w + 3, mid_y);
        SDL_SetRenderDrawColor(renderer, side_marker.r, side_marker.g, side_marker.b, side_marker.a);
        SDL_RenderDrawLine(renderer, side_meter.x - 3, side_y, side_meter.x + side_meter.w + 3, side_y);

        if (stats_rows == 1) {
            int text_x = rect->x + pad;
            int stats_gap = max_int(12, body_h / 2);
            int side_x = rect->x + pad + header_text_w - side_w;
            int min_side_x = text_x + mid_w + stats_gap;
            if (side_x < min_side_x) {
                side_x = min_side_x;
            }
            int mid_max_w = side_x - text_x - stats_gap;
            int side_max_w = rect->x + pad + header_text_w - side_x;
            ui_draw_text_clipped(renderer, text_x, stats_y, mid_buf, dim_color, 1.0f, mid_max_w);
            ui_draw_text_clipped(renderer, side_x, stats_y, side_buf, dim_color, 1.0f, side_max_w);
        } else {
            ui_draw_text_clipped(renderer, rect->x + pad, stats_y, mid_buf, dim_color, 1.0f, header_text_w);
            ui_draw_text_clipped(renderer,
                                 rect->x + pad,
                                 stats_y + body_h + row_gap,
                                 side_buf,
                                 dim_color,
                                 1.0f,
                                 header_text_w);
        }
    } else {
        ui_draw_text_clipped(renderer, rect->x + pad, stats_y, "No data", dim_color, 1.0f, header_text_w);
    }

    if (mid_hist.h > 0 && history && history->mid_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        if (effects_meter_history_cache_render_mid_side(renderer,
                                                        &mid_hist,
                                                        &side_hist,
                                                        history,
                                                        mid_marker,
                                                        side_marker)) {
            return;
        }
        if (render_mid_side_history_with_adapter(renderer, &mid_hist, &side_hist, history)) {
            return;
        }
        int count = history->mid_count;
        int total_slots = FX_METER_MID_SIDE_HISTORY_POINTS;
        float prev_mid_x = 0.0f;
        float prev_mid_y = 0.0f;
        float prev_side_x = 0.0f;
        float prev_side_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float mid_hist_val = clampf(history_get_mid_by_age(history, i), 0.0f, 1.0f);
            float side_hist_val = clampf(history_get_side_by_age(history, i), 0.0f, 1.0f);
            float x = (float)mid_hist.x + t * (float)mid_hist.w;
            float mid_y = (float)mid_hist.y + (1.0f - mid_hist_val) * (float)mid_hist.h;
            float side_y = (float)side_hist.y + (1.0f - side_hist_val) * (float)side_hist.h;
            int alpha = (int)lroundf(90.0f * (1.0f - t) + 90.0f);
            SDL_SetRenderDrawColor(renderer, mid_marker.r, mid_marker.g, mid_marker.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_mid_x),
                                   (int)lroundf(prev_mid_y),
                                   (int)lroundf(x),
                                   (int)lroundf(mid_y));
            }
            SDL_SetRenderDrawColor(renderer, side_marker.r, side_marker.g, side_marker.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_side_x),
                                   (int)lroundf(prev_side_y),
                                   (int)lroundf(x),
                                   (int)lroundf(side_y));
            }
            prev_mid_x = x;
            prev_mid_y = mid_y;
            prev_side_x = x;
            prev_side_y = side_y;
        }
    }
}
