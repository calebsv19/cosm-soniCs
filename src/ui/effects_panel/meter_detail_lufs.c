#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/render_utils.h"

#include <math.h>
#include <stdio.h>

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

// effects_meter_render_lufs draws a LUFS history trace for the selected window.
void effects_meter_render_lufs(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const EngineFxMeterSnapshot* snapshot,
                               const EffectsMeterHistory* history,
                               int lufs_mode,
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
    const float max_db = 0.0f;
    const int label_w = 22;
    const int meter_w = 12;
    const int history_gap = 0;
    int header_h = 54;
    int footer_h = 26;
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

    ui_draw_text(renderer, rect->x + pad, rect->y + 6, "LUFS Meter", label_color, 1.2f);

    bool valid = snapshot && snapshot->valid;
    if (valid) {
        char buf[64];
        snprintf(buf, sizeof(buf), "I %.1f", snapshot->lufs_integrated);
        ui_draw_text(renderer, rect->x + pad, rect->y + 22, buf, dim_color, 1.0f);
        snprintf(buf, sizeof(buf), "S %.1f", snapshot->lufs_short_term);
        ui_draw_text(renderer, rect->x + pad + 70, rect->y + 22, buf, dim_color, 1.0f);
        snprintf(buf, sizeof(buf), "M %.1f", snapshot->lufs_momentary);
        ui_draw_text(renderer, rect->x + pad + 140, rect->y + 22, buf, dim_color, 1.0f);
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
        float current = snapshot->lufs_momentary;
        if (lufs_mode == FX_METER_LUFS_INTEGRATED) {
            current = snapshot->lufs_integrated;
        } else if (lufs_mode == FX_METER_LUFS_SHORT_TERM) {
            current = snapshot->lufs_short_term;
        }
        float norm = (current - min_db) / (max_db - min_db);
        norm = clampf(norm, 0.0f, 1.0f);
        int marker_y = meter_rect.y + (int)lroundf((1.0f - norm) * (float)meter_rect.h);
        SDL_SetRenderDrawColor(renderer, 190, 230, 140, 255);
        SDL_RenderDrawLine(renderer, meter_rect.x - 4, marker_y, meter_rect.x + meter_rect.w + 4, marker_y);
    }

    ui_draw_text(renderer, rect->x + pad, meter_rect.y - 6, "0", dim_color, 1.0f);
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h / 2 - 6, "-30", dim_color, 1.0f);
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h - 10, "-60", dim_color, 1.0f);

    if (history_rect.w > 0 && history && history->lufs_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
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
            SDL_SetRenderDrawColor(renderer, 130, 170, 100, alpha);
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
