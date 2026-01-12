#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
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
    int column_gap = 14;
    int column_w = (rect->w - pad * 2 - column_gap) / 2;
    if (column_w < 40) column_w = 40;
    SDL_Rect mid_col = {rect->x + pad, rect->y + pad + 12, column_w, rect->h - pad * 2 - 12};
    SDL_Rect side_col = {mid_col.x + mid_col.w + column_gap, mid_col.y, column_w, mid_col.h};

    SDL_SetRenderDrawColor(renderer, 30, 34, 46, 255);
    SDL_RenderFillRect(renderer, &mid_col);
    SDL_RenderFillRect(renderer, &side_col);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &mid_col);
    SDL_RenderDrawRect(renderer, &side_col);

    SDL_Rect mid_bar = mid_col;
    SDL_Rect side_bar = side_col;
    mid_bar.h = 10;
    side_bar.h = 10;
    mid_bar.y += 6;
    side_bar.y += 6;

    int history_top = mid_bar.y + mid_bar.h + 12;
    SDL_Rect mid_hist = mid_col;
    SDL_Rect side_hist = side_col;
    mid_hist.y = history_top;
    side_hist.y = history_top;
    mid_hist.h = mid_col.y + mid_col.h - history_top;
    side_hist.h = side_col.y + side_col.h - history_top;
    if (mid_hist.h < 30) {
        mid_hist.h = 0;
    }
    if (side_hist.h < 30) {
        side_hist.h = 0;
    }

    SDL_SetRenderDrawColor(renderer, 50, 54, 66, 255);
    SDL_RenderFillRect(renderer, &mid_bar);
    SDL_RenderFillRect(renderer, &side_bar);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &mid_bar);
    SDL_RenderDrawRect(renderer, &side_bar);

    bool valid = snapshot && snapshot->valid;
    if (valid) {
        float mid = clampf(snapshot->mid_rms, 0.0f, 1.5f);
        float side = clampf(snapshot->side_rms, 0.0f, 1.5f);
        float mid_norm = clampf(mid / 1.0f, 0.0f, 1.0f);
        float side_norm = clampf(side / 1.0f, 0.0f, 1.0f);
        int mid_x = mid_bar.x + (int)lroundf(mid_norm * (float)mid_bar.w);
        int side_x = side_bar.x + (int)lroundf(side_norm * (float)side_bar.w);
        SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255);
        SDL_RenderDrawLine(renderer, mid_x, mid_bar.y - 4, mid_x, mid_bar.y + mid_bar.h + 4);
        SDL_SetRenderDrawColor(renderer, 255, 170, 120, 255);
        SDL_RenderDrawLine(renderer, side_x, side_bar.y - 4, side_x, side_bar.y + side_bar.h + 4);

        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f dB", meter_db(mid));
        ui_draw_text(renderer, mid_col.x, mid_col.y - 6, buf, dim_color, 1.0f);
        snprintf(buf, sizeof(buf), "%.1f dB", meter_db(side));
        ui_draw_text(renderer, side_col.x, side_col.y - 6, buf, dim_color, 1.0f);
    } else {
        ui_draw_text(renderer, mid_col.x + 6, mid_col.y + 6, "No data", dim_color, 1.0f);
    }

    ui_draw_text(renderer, mid_col.x, rect->y + 6, "Mid", label_color, 1.2f);
    ui_draw_text(renderer, side_col.x, rect->y + 6, "Side", label_color, 1.2f);

    if (mid_hist.h > 0 && history && history->mid_count > 1) {
        SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
        SDL_RenderFillRect(renderer, &mid_hist);
        SDL_RenderFillRect(renderer, &side_hist);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &mid_hist);
        SDL_RenderDrawRect(renderer, &side_hist);

        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
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
            float mid_x = (float)mid_hist.x + mid_hist_val * (float)mid_hist.w;
            float side_x = (float)side_hist.x + side_hist_val * (float)side_hist.w;
            float y = (float)mid_hist.y + t * (float)mid_hist.h;
            int alpha = (int)lroundf(120.0f * (1.0f - t) + 120.0f);
            SDL_SetRenderDrawColor(renderer, 120, 180, 255, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_mid_x),
                                   (int)lroundf(prev_mid_y),
                                   (int)lroundf(mid_x),
                                   (int)lroundf(y));
            }
            SDL_SetRenderDrawColor(renderer, 255, 170, 120, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer,
                                   (int)lroundf(prev_side_x),
                                   (int)lroundf(prev_side_y),
                                   (int)lroundf(side_x),
                                   (int)lroundf(y));
            }
            prev_mid_x = mid_x;
            prev_mid_y = y;
            prev_side_x = side_x;
            prev_side_y = y;
        }
    }
}
