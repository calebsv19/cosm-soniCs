#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/render_utils.h"

#include <math.h>
#include <stdio.h>

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
    CoreResult peak_r = daw_kit_viz_meter_plot_line_from_y_samples(peak_samples,
                                                                    (uint32_t)count,
                                                                    history_rect,
                                                                    (DawKitVizMeterPlotRange){min_db, max_db},
                                                                    peak_segments,
                                                                    FX_METER_LEVEL_HISTORY_POINTS,
                                                                    &peak_segment_count);
    CoreResult rms_r = daw_kit_viz_meter_plot_line_from_y_samples(rms_samples,
                                                                   (uint32_t)count,
                                                                   history_rect,
                                                                   (DawKitVizMeterPlotRange){min_db, max_db},
                                                                   rms_segments,
                                                                   FX_METER_LEVEL_HISTORY_POINTS,
                                                                   &rms_segment_count);
    if (peak_r.code != CORE_OK || rms_r.code != CORE_OK || peak_segment_count == 0 || rms_segment_count == 0) {
        return false;
    }
    daw_kit_viz_meter_render_segments(renderer,
                                      peak_segments,
                                      peak_segment_count,
                                      (SDL_Color){90, 150, 210, 180});
    daw_kit_viz_meter_render_segments(renderer,
                                      rms_segments,
                                      rms_segment_count,
                                      (SDL_Color){140, 120, 110, 180});
    return true;
}

// effects_meter_render_levels draws a peak/RMS history trace.
void effects_meter_render_levels(SDL_Renderer* renderer,
                                 const SDL_Rect* rect,
                                 const EngineFxMeterSnapshot* snapshot,
                                 const EffectsMeterHistory* history,
                                 SDL_Color label_color,
                                 SDL_Color dim_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color border = {70, 75, 92, 255};
    SDL_Color fill = {22, 24, 30, 255};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 16;
    const float min_db = -60.0f;
    const float max_db = 6.0f;
    const int label_w = 22;
    const int meter_w = 12;
    const int history_gap = 0;
    int header_h = 42;
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

    ui_draw_text(renderer, rect->x + pad, rect->y + 6, "Level Meter", label_color, 1.2f);

    bool valid = snapshot && snapshot->valid;
    if (valid) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Peak %.1f dB", meter_db(snapshot->peak));
        ui_draw_text(renderer, rect->x + pad, rect->y + 22, buf, dim_color, 1.0f);
        snprintf(buf, sizeof(buf), "RMS %.1f dB", meter_db(snapshot->rms));
        ui_draw_text(renderer, rect->x + pad + 150, rect->y + 22, buf, dim_color, 1.0f);
    } else {
        ui_draw_text(renderer, rect->x + pad, rect->y + 22, "No data", dim_color, 1.0f);
    }

    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);

    SDL_SetRenderDrawColor(renderer, 50, 54, 66, 255);
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
        SDL_SetRenderDrawColor(renderer, 150, 220, 255, 255);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, peak_y, meter_rect.x + meter_rect.w + 4, peak_y);
        SDL_SetRenderDrawColor(renderer, 210, 180, 150, 255);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, rms_y, meter_rect.x + meter_rect.w + 4, rms_y);
    }

    ui_draw_text(renderer, rect->x + pad, meter_rect.y - 6, "+6", dim_color, 1.0f);
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h / 2 - 6, "-30", dim_color, 1.0f);
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h - 10, "-60", dim_color, 1.0f);

    if (history_rect.w > 0 && history && history->level_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
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
            SDL_SetRenderDrawColor(renderer, 90, 150, 210, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_peak_x),
                                   (int)lroundf(prev_peak_y),
                                   (int)lroundf(x),
                                   (int)lroundf(y1));
            }
            SDL_SetRenderDrawColor(renderer, 140, 120, 110, alpha);
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
