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

static void resolve_corr_theme(SDL_Color* fill,
                               SDL_Color* border,
                               SDL_Color* history_bg,
                               SDL_Color* meter_bg,
                               SDL_Color* marker,
                               SDL_Color* trace) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (fill) *fill = theme.inspector_fill;
        if (border) *border = theme.pane_border;
        if (history_bg) *history_bg = theme.timeline_fill;
        if (meter_bg) *meter_bg = theme.slider_track;
        if (marker) *marker = theme.slider_handle;
        if (trace) *trace = theme.accent_primary;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (history_bg) *history_bg = (SDL_Color){26, 28, 36, 255};
    if (meter_bg) *meter_bg = (SDL_Color){50, 54, 66, 255};
    if (marker) *marker = (SDL_Color){150, 220, 255, 255};
    if (trace) *trace = (SDL_Color){100, 150, 210, 180};
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float history_get_corr_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->corr_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->corr_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_CORR_HISTORY_POINTS;
    idx %= FX_METER_CORR_HISTORY_POINTS;
    return history->corr_values[idx];
}

static bool render_corr_history_with_adapter(SDL_Renderer* renderer,
                                             const SDL_Rect* history_rect,
                                             const EffectsMeterHistory* history) {
    if (!renderer || !history_rect || !history || history->corr_count <= 1 || history_rect->w <= 0) {
        return false;
    }
    const int count = history->corr_count;
    float y_samples[FX_METER_CORR_HISTORY_POINTS];
    for (int i = 0; i < count; ++i) {
        y_samples[i] = clampf(history_get_corr_by_age(history, i), -1.0f, 1.0f);
    }

    KitVizVecSegment segments[FX_METER_CORR_HISTORY_POINTS];
    size_t segment_count = 0;
    CoreResult r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(y_samples,
                                                                           (uint32_t)count,
                                                                           FX_METER_CORR_HISTORY_POINTS,
                                                                           history_rect,
                                                                           (DawKitVizMeterPlotRange){-1.0f, 1.0f},
                                                                           segments,
                                                                           FX_METER_CORR_HISTORY_POINTS,
                                                                           &segment_count);
    if (r.code != CORE_OK || segment_count == 0) {
        return false;
    }
    SDL_Color trace = {0};
    resolve_corr_theme(NULL, NULL, NULL, NULL, NULL, &trace);
    daw_kit_viz_meter_render_segments(renderer,
                                      segments,
                                      segment_count,
                                      trace);
    return true;
}

// effects_meter_render_correlation draws a horizontal correlation bar.
void effects_meter_render_correlation(SDL_Renderer* renderer,
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
    SDL_Color marker = {0};
    SDL_Color trace = {0};
    resolve_corr_theme(&fill, &border, &history_bg, &meter_bg, &marker, &trace);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    const int pad = max_int(12, body_h / 2 + 8);
    const int label_w = max_int(22, ui_measure_text_width("-1", 1.0f) + 8);
    const int meter_w = max_int(10, body_h / 2);
    const int history_gap = max_int(0, body_h / 5);
    const int header_text_w = rect->w - pad * 2;
    const int title_y = rect->y + max_int(4, pad / 3);
    const int value_y = title_y + title_h + max_int(4, body_h / 3);
    int header_h = (value_y - (rect->y + pad)) + body_h + max_int(8, body_h / 2);
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
                         "Correlation",
                         label_color,
                         1.2f,
                         header_text_w);

    SDL_SetRenderDrawColor(renderer, history_bg.r, history_bg.g, history_bg.b, history_bg.a);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);
    effects_meter_history_grid_draw(renderer, &history_rect, history_grid);

    SDL_SetRenderDrawColor(renderer, meter_bg.r, meter_bg.g, meter_bg.b, meter_bg.a);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    float corr = 0.0f;
    bool valid = snapshot && snapshot->valid;
    if (valid) {
        corr = clampf(snapshot->corr, -1.0f, 1.0f);
    }
    int marker_y = meter_rect.y + (int)lroundf((1.0f - ((corr + 1.0f) * 0.5f)) * (float)meter_rect.h);

    SDL_SetRenderDrawColor(renderer, marker.r, marker.g, marker.b, marker.a);
    SDL_RenderDrawLine(renderer, meter_rect.x - 3, marker_y, meter_rect.x + meter_rect.w + 3, marker_y);

    if (valid) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f", corr);
        ui_draw_text_clipped(renderer, rect->x + pad, value_y, buf, label_color, 1.0f, header_text_w);
    } else {
        ui_draw_text_clipped(renderer, rect->x + pad, value_y, "No data", dim_color, 1.0f, header_text_w);
    }

    int top_label_y = meter_rect.y;
    int mid_label_y = meter_rect.y + (meter_rect.h - body_h) / 2;
    int bot_label_y = meter_rect.y + meter_rect.h - body_h;
    ui_draw_text_clipped(renderer, rect->x + pad, top_label_y, "+1", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, mid_label_y, "0", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, bot_label_y, "-1", dim_color, 1.0f, label_w);

    if (history_rect.w > 0 && history && history->corr_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        if (effects_meter_history_cache_render_correlation(renderer,
                                                           &history_rect,
                                                           history,
                                                           trace)) {
            return;
        }
        if (render_corr_history_with_adapter(renderer, &history_rect, history)) {
            return;
        }
        int count = history->corr_count;
        int total_slots = FX_METER_CORR_HISTORY_POINTS;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float corr_hist = clampf(history_get_corr_by_age(history, i), -1.0f, 1.0f);
            float x = (float)history_rect.x + t * (float)history_rect.w;
            float y_norm = (corr_hist + 1.0f) * 0.5f;
            float y = (float)history_rect.y + (1.0f - y_norm) * (float)history_rect.h;
            int alpha = (int)lroundf(90.0f * (1.0f - t) + 90.0f);
            SDL_SetRenderDrawColor(renderer, trace.r, trace.g, trace.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer, (int)lroundf(prev_x), (int)lroundf(prev_y), (int)lroundf(x), (int)lroundf(y));
            }
            prev_x = x;
            prev_y = y;
        }
    }
}
