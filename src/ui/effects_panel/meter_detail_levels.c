#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/effects_panel_meter_history_cache.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>

static void resolve_levels_theme(SDL_Color* fill,
                                 SDL_Color* border,
                                 SDL_Color* history_bg,
                                 SDL_Color* meter_bg,
                                 SDL_Color* peak_marker,
                                 SDL_Color* rms_marker) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (fill) *fill = theme.inspector_fill;
        if (border) *border = theme.pane_border;
        if (history_bg) *history_bg = theme.timeline_fill;
        if (meter_bg) *meter_bg = theme.slider_track;
        if (peak_marker) *peak_marker = theme.slider_handle;
        if (rms_marker) *rms_marker = theme.accent_warning;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (history_bg) *history_bg = (SDL_Color){26, 28, 36, 255};
    if (meter_bg) *meter_bg = (SDL_Color){50, 54, 66, 255};
    if (peak_marker) *peak_marker = (SDL_Color){150, 220, 255, 255};
    if (rms_marker) *rms_marker = (SDL_Color){210, 180, 150, 255};
}

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

// Clamps a value between bounds for stable meter rendering.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Converts a linear amplitude to dB for display.
static float meter_db(float linear) {
    if (linear <= 1e-9f) {
        return -90.0f;
    }
    return 20.0f * log10f(linear);
}

// Returns peak history value by age index.
static float history_get_peak_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->level_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->level_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LEVEL_HISTORY_POINTS;
    idx %= FX_METER_LEVEL_HISTORY_POINTS;
    return history->peak_values[idx];
}

// Returns RMS history value by age index.
static float history_get_rms_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->level_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->level_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LEVEL_HISTORY_POINTS;
    idx %= FX_METER_LEVEL_HISTORY_POINTS;
    return history->rms_values[idx];
}

static bool render_level_history_with_adapter(SDL_Renderer* renderer,
                                              const SDL_Rect* history_rect,
                                              const EffectsMeterHistory* history,
                                              float min_db,
                                              float max_db) {
    if (!renderer || !history_rect || !history || history->level_count <= 1 || history_rect->w <= 0) {
        return false;
    }
    const int count = history->level_count;
    float peak_samples[FX_METER_LEVEL_HISTORY_POINTS];
    float rms_samples[FX_METER_LEVEL_HISTORY_POINTS];
    for (int i = 0; i < count; ++i) {
        float peak = clampf(history_get_peak_by_age(history, i), 0.0f, 2.0f);
        float rms = clampf(history_get_rms_by_age(history, i), 0.0f, 2.0f);
        peak_samples[i] = meter_db(peak);
        rms_samples[i] = meter_db(rms);
    }

    KitVizVecSegment peak_segments[FX_METER_LEVEL_HISTORY_POINTS];
    KitVizVecSegment rms_segments[FX_METER_LEVEL_HISTORY_POINTS];
    size_t peak_segment_count = 0;
    size_t rms_segment_count = 0;
    CoreResult peak_r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(peak_samples,
                                                                                (uint32_t)count,
                                                                                FX_METER_LEVEL_HISTORY_POINTS,
                                                                                history_rect,
                                                                                (DawKitVizMeterPlotRange){min_db, max_db},
                                                                                peak_segments,
                                                                                FX_METER_LEVEL_HISTORY_POINTS,
                                                                                &peak_segment_count);
    CoreResult rms_r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(rms_samples,
                                                                               (uint32_t)count,
                                                                               FX_METER_LEVEL_HISTORY_POINTS,
                                                                               history_rect,
                                                                               (DawKitVizMeterPlotRange){min_db, max_db},
                                                                               rms_segments,
                                                                               FX_METER_LEVEL_HISTORY_POINTS,
                                                                               &rms_segment_count);
    if (peak_r.code != CORE_OK || rms_r.code != CORE_OK || peak_segment_count == 0 || rms_segment_count == 0) {
        return false;
    }
    SDL_Color peak_marker = {0};
    SDL_Color rms_marker = {0};
    resolve_levels_theme(NULL, NULL, NULL, NULL, &peak_marker, &rms_marker);
    daw_kit_viz_meter_render_segments(renderer,
                                      peak_segments,
                                      peak_segment_count,
                                      peak_marker);
    daw_kit_viz_meter_render_segments(renderer,
                                      rms_segments,
                                      rms_segment_count,
                                      rms_marker);
    return true;
}

// effects_meter_render_levels draws a peak/RMS history trace.
void effects_meter_render_levels(SDL_Renderer* renderer,
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
    SDL_Color peak_marker = {0};
    SDL_Color rms_marker = {0};
    resolve_levels_theme(&fill, &border, &history_bg, &meter_bg, &peak_marker, &rms_marker);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const float min_db = -60.0f;
    const float max_db = 6.0f;
    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    const int pad = max_int(12, body_h / 2 + 8);
    const int label_w = max_int(22, ui_measure_text_width("-60", 1.0f) + 8);
    const int meter_w = max_int(10, body_h / 2);
    const int history_gap = max_int(0, body_h / 5);
    const int title_y = rect->y + max_int(4, pad / 3);
    const int row_gap = max_int(4, body_h / 3);
    const int section_gap = max_int(8, body_h / 2);
    const int header_text_w = rect->w - pad * 2;
    bool valid = snapshot && snapshot->valid;
    char peak_buf[64] = {0};
    char rms_buf[64] = {0};
    int peak_w = 0;
    int rms_w = 0;
    int stats_rows = 1;
    if (valid) {
        snprintf(peak_buf, sizeof(peak_buf), "Peak %.1f dB", meter_db(snapshot->peak));
        snprintf(rms_buf, sizeof(rms_buf), "RMS %.1f dB", meter_db(snapshot->rms));
        peak_w = ui_measure_text_width(peak_buf, 1.0f);
        rms_w = ui_measure_text_width(rms_buf, 1.0f);
        if (peak_w + max_int(12, body_h / 2) + rms_w > header_text_w) {
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
                         "Level Meter",
                         label_color,
                         1.2f,
                         header_text_w);
    if (valid) {
        if (stats_rows == 1) {
            int text_x = rect->x + pad;
            int stats_gap = max_int(12, body_h / 2);
            int rms_x = rect->x + pad + header_text_w - rms_w;
            int min_rms_x = text_x + peak_w + stats_gap;
            if (rms_x < min_rms_x) {
                rms_x = min_rms_x;
            }
            int peak_max_w = rms_x - text_x - stats_gap;
            int rms_max_w = rect->x + pad + header_text_w - rms_x;
            ui_draw_text_clipped(renderer, text_x, stats_y, peak_buf, dim_color, 1.0f, peak_max_w);
            ui_draw_text_clipped(renderer, rms_x, stats_y, rms_buf, dim_color, 1.0f, rms_max_w);
        } else {
            ui_draw_text_clipped(renderer, rect->x + pad, stats_y, peak_buf, dim_color, 1.0f, header_text_w);
            ui_draw_text_clipped(renderer, rect->x + pad, stats_y + body_h + row_gap, rms_buf, dim_color, 1.0f, header_text_w);
        }
    } else {
        ui_draw_text_clipped(renderer, rect->x + pad, stats_y, "No data", dim_color, 1.0f, header_text_w);
    }

    SDL_SetRenderDrawColor(renderer, history_bg.r, history_bg.g, history_bg.b, history_bg.a);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);
    effects_meter_history_grid_draw(renderer, &history_rect, history_grid);

    SDL_SetRenderDrawColor(renderer, meter_bg.r, meter_bg.g, meter_bg.b, meter_bg.a);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    if (valid) {
        float peak_db = meter_db(snapshot->peak);
        float rms_db = meter_db(snapshot->rms);
        float peak_norm = (peak_db - min_db) / (max_db - min_db);
        float rms_norm = (rms_db - min_db) / (max_db - min_db);
        peak_norm = clampf(peak_norm, 0.0f, 1.0f);
        rms_norm = clampf(rms_norm, 0.0f, 1.0f);
        int peak_y = meter_rect.y + (int)lroundf((1.0f - peak_norm) * (float)meter_rect.h);
        int rms_y = meter_rect.y + (int)lroundf((1.0f - rms_norm) * (float)meter_rect.h);
        SDL_SetRenderDrawColor(renderer, peak_marker.r, peak_marker.g, peak_marker.b, peak_marker.a);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, peak_y, meter_rect.x + meter_rect.w + 4, peak_y);
        SDL_SetRenderDrawColor(renderer, rms_marker.r, rms_marker.g, rms_marker.b, rms_marker.a);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, rms_y, meter_rect.x + meter_rect.w + 4, rms_y);
    }

    int top_label_y = meter_rect.y;
    int mid_label_y = meter_rect.y + (meter_rect.h - body_h) / 2;
    int bot_label_y = meter_rect.y + meter_rect.h - body_h;
    ui_draw_text_clipped(renderer, rect->x + pad, top_label_y, "+6", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, mid_label_y, "-30", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, bot_label_y, "-60", dim_color, 1.0f, label_w);

    if (history_rect.w > 0 && history && history->level_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        if (effects_meter_history_cache_render_levels(renderer,
                                                      &history_rect,
                                                      history,
                                                      min_db,
                                                      max_db,
                                                      peak_marker,
                                                      rms_marker)) {
            return;
        }
        if (render_level_history_with_adapter(renderer, &history_rect, history, min_db, max_db)) {
            return;
        }
        int count = history->level_count;
        int total_slots = FX_METER_LEVEL_HISTORY_POINTS;
        float prev_peak_x = 0.0f;
        float prev_peak_y = 0.0f;
        float prev_rms_x = 0.0f;
        float prev_rms_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float peak_db = meter_db(clampf(history_get_peak_by_age(history, i), 0.0f, 2.0f));
            float rms_db = meter_db(clampf(history_get_rms_by_age(history, i), 0.0f, 2.0f));
            float y_peak = (peak_db - min_db) / (max_db - min_db);
            float y_rms = (rms_db - min_db) / (max_db - min_db);
            y_peak = clampf(y_peak, 0.0f, 1.0f);
            y_rms = clampf(y_rms, 0.0f, 1.0f);
            float x = (float)history_rect.x + t * (float)history_rect.w;
            float y1 = (float)history_rect.y + (1.0f - y_peak) * (float)history_rect.h;
            float y2 = (float)history_rect.y + (1.0f - y_rms) * (float)history_rect.h;
            int alpha = (int)lroundf(120.0f * (1.0f - t) + 120.0f);
            SDL_SetRenderDrawColor(renderer, peak_marker.r, peak_marker.g, peak_marker.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_peak_x),
                                   (int)lroundf(prev_peak_y),
                                   (int)lroundf(x),
                                   (int)lroundf(y1));
            }
            SDL_SetRenderDrawColor(renderer, rms_marker.r, rms_marker.g, rms_marker.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_rms_x),
                                   (int)lroundf(prev_rms_y),
                                   (int)lroundf(x),
                                   (int)lroundf(y2));
            }
            prev_peak_x = x;
            prev_peak_y = y1;
            prev_rms_x = x;
            prev_rms_y = y2;
        }
    }
}
