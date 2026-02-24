#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/render_utils.h"

#include <math.h>

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
    CoreResult mid_r = daw_kit_viz_meter_plot_line_from_y_samples(mid_samples,
                                                                   (uint32_t)count,
                                                                   mid_hist,
                                                                   (DawKitVizMeterPlotRange){0.0f, 1.0f},
                                                                   mid_segments,
                                                                   FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                   &mid_segment_count);
    CoreResult side_r = daw_kit_viz_meter_plot_line_from_y_samples(side_samples,
                                                                    (uint32_t)count,
                                                                    side_hist,
                                                                    (DawKitVizMeterPlotRange){0.0f, 1.0f},
                                                                    side_segments,
                                                                    FX_METER_MID_SIDE_HISTORY_POINTS,
                                                                    &side_segment_count);
    if (mid_r.code != CORE_OK || side_r.code != CORE_OK || mid_segment_count == 0 || side_segment_count == 0) {
        return false;
    }
    daw_kit_viz_meter_render_segments(renderer,
                                      mid_segments,
                                      mid_segment_count,
                                      (SDL_Color){100, 150, 210, 180});
    daw_kit_viz_meter_render_segments(renderer,
                                      side_segments,
                                      side_segment_count,
                                      (SDL_Color){180, 120, 90, 180});
    return true;
}

// effects_meter_render_mid_side draws mid/side history strips.
void effects_meter_render_mid_side(SDL_Renderer* renderer,
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
    const int label_w = 22;
    const int meter_w = 12;
    const int history_gap = 0;
    int header_h = 36;
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

    ui_draw_text(renderer, rect->x + pad, rect->y + 6, "Mid/Side", label_color, 1.2f);

    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);

    SDL_SetRenderDrawColor(renderer, 50, 54, 66, 255);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    int lane_gap = 10;
    int lane_h = (meter_rect.h - lane_gap) / 2;
    SDL_Rect mid_meter = {meter_rect.x, meter_rect.y, meter_rect.w, lane_h};
    SDL_Rect side_meter = {meter_rect.x, meter_rect.y + lane_h + lane_gap, meter_rect.w, lane_h};

    SDL_SetRenderDrawColor(renderer, 60, 64, 74, 255);
    SDL_RenderFillRect(renderer, &mid_meter);
    SDL_RenderFillRect(renderer, &side_meter);

    int hist_gap = 10;
    int hist_h = (history_rect.h - hist_gap) / 2;
    SDL_Rect mid_hist = {history_rect.x, history_rect.y, history_rect.w, hist_h};
    SDL_Rect side_hist = {history_rect.x, history_rect.y + hist_h + hist_gap, history_rect.w, hist_h};
    if (hist_h < 30) {
        mid_hist.h = 0;
        side_hist.h = 0;
    }

    bool valid = snapshot && snapshot->valid;
    if (valid) {
        float mid = clampf(snapshot->mid_rms, 0.0f, 1.5f);
        float side = clampf(snapshot->side_rms, 0.0f, 1.5f);
        float mid_norm = clampf(mid / 1.0f, 0.0f, 1.0f);
        float side_norm = clampf(side / 1.0f, 0.0f, 1.0f);
        int mid_y = mid_meter.y + (int)lroundf((1.0f - mid_norm) * (float)mid_meter.h);
        int side_y = side_meter.y + (int)lroundf((1.0f - side_norm) * (float)side_meter.h);
        SDL_SetRenderDrawColor(renderer, 150, 210, 255, 255);
        SDL_RenderDrawLine(renderer, mid_meter.x - 3, mid_y, mid_meter.x + mid_meter.w + 3, mid_y);
        SDL_SetRenderDrawColor(renderer, 255, 190, 140, 255);
        SDL_RenderDrawLine(renderer, side_meter.x - 3, side_y, side_meter.x + side_meter.w + 3, side_y);

        char buf[64];
        snprintf(buf, sizeof(buf), "Mid %.1f dB", meter_db(mid));
        ui_draw_text(renderer, rect->x + pad, rect->y + 22, buf, dim_color, 1.0f);
        snprintf(buf, sizeof(buf), "Side %.1f dB", meter_db(side));
        ui_draw_text(renderer, rect->x + pad + 150, rect->y + 22, buf, dim_color, 1.0f);
    } else {
        ui_draw_text(renderer, rect->x + pad, rect->y + 22, "No data", dim_color, 1.0f);
    }

    if (mid_hist.h > 0 && history && history->mid_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
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
            SDL_SetRenderDrawColor(renderer, 100, 150, 210, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_mid_x),
                                   (int)lroundf(prev_mid_y),
                                   (int)lroundf(x),
                                   (int)lroundf(mid_y));
            }
            SDL_SetRenderDrawColor(renderer, 180, 120, 90, alpha);
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
