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

static void resolve_lufs_theme(SDL_Color* fill,
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
        if (marker) *marker = theme.accent_warning;
        if (trace) *trace = theme.accent_warning;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (history_bg) *history_bg = (SDL_Color){26, 28, 36, 255};
    if (meter_bg) *meter_bg = (SDL_Color){50, 54, 66, 255};
    if (marker) *marker = (SDL_Color){190, 230, 140, 255};
    if (trace) *trace = (SDL_Color){130, 170, 100, 180};
}

// Clamps a value between bounds for stable meter rendering.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Returns LUFS history value by age index for the selected mode.
static float history_get_lufs_by_age(const EffectsMeterHistory* history, int age_index, int mode) {
    int count = history ? history->lufs_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return -90.0f;
    }
    int idx = history->lufs_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LUFS_HISTORY_POINTS;
    idx %= FX_METER_LUFS_HISTORY_POINTS;
    if (mode == FX_METER_LUFS_INTEGRATED) {
        return history->lufs_i_values[idx];
    }
    if (mode == FX_METER_LUFS_SHORT_TERM) {
        return history->lufs_s_values[idx];
    }
    return history->lufs_m_values[idx];
}

static bool render_lufs_history_with_adapter(SDL_Renderer* renderer,
                                             const SDL_Rect* history_rect,
                                             const EffectsMeterHistory* history,
                                             int lufs_mode,
                                             float min_db,
                                             float max_db) {
    if (!renderer || !history_rect || !history || history->lufs_count <= 1 || history_rect->w <= 0) {
        return false;
    }
    const int count = history->lufs_count;
    float lufs_samples[FX_METER_LUFS_HISTORY_POINTS];
    for (int i = 0; i < count; ++i) {
        lufs_samples[i] = history_get_lufs_by_age(history, i, lufs_mode);
    }

    KitVizVecSegment segments[FX_METER_LUFS_HISTORY_POINTS];
    size_t segment_count = 0;
    CoreResult r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(lufs_samples,
                                                                           (uint32_t)count,
                                                                           FX_METER_LUFS_HISTORY_POINTS,
                                                                           history_rect,
                                                                           (DawKitVizMeterPlotRange){min_db, max_db},
                                                                           segments,
                                                                           FX_METER_LUFS_HISTORY_POINTS,
                                                                           &segment_count);
    if (r.code != CORE_OK || segment_count == 0) {
        return false;
    }
    SDL_Color trace = {0};
    resolve_lufs_theme(NULL, NULL, NULL, NULL, NULL, &trace);
    daw_kit_viz_meter_render_segments(renderer,
                                      segments,
                                      segment_count,
                                      trace);
    return true;
}

// effects_meter_render_lufs draws a LUFS history trace for the selected window.
void effects_meter_render_lufs(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const EngineFxMeterSnapshot* snapshot,
                               const EffectsMeterHistory* history,
                               const EffectsMeterHistoryGridContext* history_grid,
                               int lufs_mode,
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
    resolve_lufs_theme(&fill, &border, &history_bg, &meter_bg, &marker, &trace);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const float min_db = -60.0f;
    const float max_db = 0.0f;
    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    const int pad = max_int(12, body_h / 2 + 8);
    const int label_w = max_int(22, ui_measure_text_width("-60", 1.0f) + 8);
    const int meter_w = max_int(10, body_h / 2);
    const int history_gap = max_int(0, body_h / 5);
    const int row_gap = max_int(4, body_h / 3);
    const int section_gap = max_int(8, body_h / 2);
    const int header_text_w = rect->w - pad * 2;
    const int title_y = rect->y + max_int(4, pad / 3);
    bool valid = snapshot && snapshot->valid;
    char lufs_i_buf[64] = {0};
    char lufs_s_buf[64] = {0};
    char lufs_m_buf[64] = {0};
    int lufs_i_w = 0;
    int lufs_s_w = 0;
    int lufs_m_w = 0;
    int stats_cols = 1;
    int stats_rows = 1;
    int stat_gap = max_int(12, body_h / 2);
    if (valid) {
        snprintf(lufs_i_buf, sizeof(lufs_i_buf), "I %.1f", snapshot->lufs_integrated);
        snprintf(lufs_s_buf, sizeof(lufs_s_buf), "S %.1f", snapshot->lufs_short_term);
        snprintf(lufs_m_buf, sizeof(lufs_m_buf), "M %.1f", snapshot->lufs_momentary);
        lufs_i_w = ui_measure_text_width(lufs_i_buf, 1.0f);
        lufs_s_w = ui_measure_text_width(lufs_s_buf, 1.0f);
        lufs_m_w = ui_measure_text_width(lufs_m_buf, 1.0f);
        if (lufs_i_w + stat_gap + lufs_s_w + stat_gap + lufs_m_w <= header_text_w) {
            stats_cols = 3;
        } else if (lufs_i_w + stat_gap + lufs_s_w <= header_text_w) {
            stats_cols = 2;
        }
        stats_rows = (3 + stats_cols - 1) / stats_cols;
    }
    const int stats_y = title_y + title_h + row_gap;
    int header_h = (stats_y - (rect->y + pad))
        + stats_rows * body_h
        + (stats_rows - 1) * row_gap
        + section_gap;
    int footer_h = max_int(26, body_h + 10);
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }

    int meter_x = rect->x + pad + label_w;
    int meter_y = rect->y + pad + header_h;
    int meter_h = rect->h - pad * 2 - header_h - footer_h;
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
                         "LUFS Meter",
                         label_color,
                         1.2f,
                         header_text_w);

    if (valid) {
        if (stats_cols == 3) {
            int x = rect->x + pad;
            int w_left = header_text_w;
            ui_draw_text_clipped(renderer, x, stats_y, lufs_i_buf, dim_color, 1.0f, w_left);
            x += lufs_i_w + stat_gap;
            w_left = rect->x + pad + header_text_w - x;
            ui_draw_text_clipped(renderer, x, stats_y, lufs_s_buf, dim_color, 1.0f, w_left);
            x += lufs_s_w + stat_gap;
            w_left = rect->x + pad + header_text_w - x;
            ui_draw_text_clipped(renderer, x, stats_y, lufs_m_buf, dim_color, 1.0f, w_left);
        } else if (stats_cols == 2) {
            int x = rect->x + pad;
            int second_x = x + lufs_i_w + stat_gap;
            int second_w = rect->x + pad + header_text_w - second_x;
            ui_draw_text_clipped(renderer, x, stats_y, lufs_i_buf, dim_color, 1.0f, header_text_w);
            ui_draw_text_clipped(renderer, second_x, stats_y, lufs_s_buf, dim_color, 1.0f, second_w);
            ui_draw_text_clipped(renderer,
                                 rect->x + pad,
                                 stats_y + body_h + row_gap,
                                 lufs_m_buf,
                                 dim_color,
                                 1.0f,
                                 header_text_w);
        } else {
            ui_draw_text_clipped(renderer, rect->x + pad, stats_y, lufs_i_buf, dim_color, 1.0f, header_text_w);
            ui_draw_text_clipped(renderer,
                                 rect->x + pad,
                                 stats_y + (body_h + row_gap),
                                 lufs_s_buf,
                                 dim_color,
                                 1.0f,
                                 header_text_w);
            ui_draw_text_clipped(renderer,
                                 rect->x + pad,
                                 stats_y + (body_h + row_gap) * 2,
                                 lufs_m_buf,
                                 dim_color,
                                 1.0f,
                                 header_text_w);
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
        float current = snapshot->lufs_momentary;
        if (lufs_mode == FX_METER_LUFS_INTEGRATED) {
            current = snapshot->lufs_integrated;
        } else if (lufs_mode == FX_METER_LUFS_SHORT_TERM) {
            current = snapshot->lufs_short_term;
        }
        float norm = (current - min_db) / (max_db - min_db);
        norm = clampf(norm, 0.0f, 1.0f);
        int marker_y = meter_rect.y + (int)lroundf((1.0f - norm) * (float)meter_rect.h);
        SDL_SetRenderDrawColor(renderer, marker.r, marker.g, marker.b, marker.a);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, marker_y, meter_rect.x + meter_rect.w + 4, marker_y);
    }

    int top_label_y = meter_rect.y;
    int mid_label_y = meter_rect.y + (meter_rect.h - body_h) / 2;
    int bot_label_y = meter_rect.y + meter_rect.h - body_h;
    ui_draw_text_clipped(renderer, rect->x + pad, top_label_y, "0", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, mid_label_y, "-30", dim_color, 1.0f, label_w);
    ui_draw_text_clipped(renderer, rect->x + pad, bot_label_y, "-60", dim_color, 1.0f, label_w);

    if (history_rect.w > 0 && history && history->lufs_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        if (effects_meter_history_cache_render_lufs(renderer,
                                                    &history_rect,
                                                    history,
                                                    lufs_mode,
                                                    min_db,
                                                    max_db,
                                                    trace)) {
            return;
        }
        if (render_lufs_history_with_adapter(renderer, &history_rect, history, lufs_mode, min_db, max_db)) {
            return;
        }
        int count = history->lufs_count;
        int total_slots = FX_METER_LUFS_HISTORY_POINTS;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float lufs = history_get_lufs_by_age(history, i, lufs_mode);
            float y_norm = (lufs - min_db) / (max_db - min_db);
            y_norm = clampf(y_norm, 0.0f, 1.0f);
            float x = (float)history_rect.x + t * (float)history_rect.w;
            float y = (float)history_rect.y + (1.0f - y_norm) * (float)history_rect.h;
            int alpha = (int)lroundf(90.0f * (1.0f - t) + 90.0f);
            SDL_SetRenderDrawColor(renderer, trace.r, trace.g, trace.b, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_x),
                                   (int)lroundf(prev_y),
                                   (int)lroundf(x),
                                   (int)lroundf(y));
            }
            prev_x = x;
            prev_y = y;
        }
    }
}
