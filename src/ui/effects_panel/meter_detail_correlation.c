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

// effects_meter_render_correlation draws a horizontal correlation bar.
void effects_meter_render_correlation(SDL_Renderer* renderer,
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
    SDL_Rect bar = *rect;
    bar.x += pad;
    bar.w -= pad * 2;
    bar.y += 28;
    bar.h = 12;

    int history_top = bar.y + bar.h + 18;
    SDL_Rect history_rect = *rect;
    history_rect.x += pad;
    history_rect.w -= pad * 2;
    history_rect.y = history_top;
    history_rect.h = rect->y + rect->h - history_top - pad;
    if (history_rect.h < 24) {
        history_rect.h = 0;
    }

    SDL_SetRenderDrawColor(renderer, 50, 54, 66, 255);
    SDL_RenderFillRect(renderer, &bar);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bar);

    float corr = 0.0f;
    bool valid = snapshot && snapshot->valid;
    if (valid) {
        corr = clampf(snapshot->corr, -1.0f, 1.0f);
    }
    int center_x = bar.x + bar.w / 2;
    int marker_x = center_x + (int)lroundf(corr * (float)(bar.w / 2));

    SDL_SetRenderDrawColor(renderer, 140, 200, 255, 255);
    SDL_RenderDrawLine(renderer, marker_x, bar.y - 6, marker_x, bar.y + bar.h + 6);

    ui_draw_text(renderer, rect->x + pad, rect->y + 10, "Correlation", label_color, 1.3f);
    if (valid) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f", corr);
        ui_draw_text(renderer, rect->x + pad, rect->y + 32, buf, label_color, 1.2f);
    } else {
        ui_draw_text(renderer, rect->x + pad, rect->y + 32, "No data", dim_color, 1.1f);
    }

    ui_draw_text(renderer, bar.x, bar.y + bar.h + 10, "-1", dim_color, 1.0f);
    ui_draw_text(renderer, center_x - 8, bar.y + bar.h + 10, "0", dim_color, 1.0f);
    ui_draw_text(renderer, bar.x + bar.w - 12, bar.y + bar.h + 10, "+1", dim_color, 1.0f);

    if (history_rect.h > 0 && history && history->corr_count > 1) {
        SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
        SDL_RenderFillRect(renderer, &history_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &history_rect);

        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        int count = history->corr_count;
        int total_slots = FX_METER_CORR_HISTORY_POINTS;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float corr_hist = clampf(history_get_corr_by_age(history, i), -1.0f, 1.0f);
            float x = (float)history_rect.x + (corr_hist + 1.0f) * 0.5f * (float)history_rect.w;
            float y = (float)history_rect.y + t * (float)history_rect.h;
            int alpha = (int)lroundf(120.0f * (1.0f - t) + 120.0f);
            SDL_SetRenderDrawColor(renderer, 120, 180, 255, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer, (int)lroundf(prev_x), (int)lroundf(prev_y), (int)lroundf(x), (int)lroundf(y));
            }
            prev_x = x;
            prev_y = y;
        }
    }
}
