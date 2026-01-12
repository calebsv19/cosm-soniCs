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

static void history_get_vec(const EffectsMeterHistory* history,
                            int index,
                            float* out_x,
                            float* out_y) {
    if (!history || !out_x || !out_y || history->vec_count <= 0) {
        if (out_x) *out_x = 0.0f;
        if (out_y) *out_y = 0.0f;
        return;
    }
    int count = history->vec_count;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    int idx = history->vec_head - 1 - index;
    while (idx < 0) idx += FX_METER_VECTOR_HISTORY_POINTS;
    idx %= FX_METER_VECTOR_HISTORY_POINTS;
    *out_x = history->vec_x[idx];
    *out_y = history->vec_y[idx];
}

// effects_meter_render_vectorscope draws a simple XY scope with a history trail.
void effects_meter_render_vectorscope(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const EngineFxMeterSnapshot* snapshot,
                                      const EffectsMeterHistory* history,
                                      int scope_mode,
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

    int pad = 16;
    SDL_Rect scope = *rect;
    scope.x += pad;
    scope.y += pad + 12;
    scope.w -= pad * 2;
    scope.h -= pad * 2 + 12;

    SDL_SetRenderDrawColor(renderer, 30, 34, 46, 255);
    SDL_RenderFillRect(renderer, &scope);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &scope);

    int cx = scope.x + scope.w / 2;
    int cy = scope.y + scope.h / 2;
    SDL_SetRenderDrawColor(renderer, 60, 70, 90, 255);
    SDL_RenderDrawLine(renderer, cx, scope.y, cx, scope.y + scope.h);
    SDL_RenderDrawLine(renderer, scope.x, cy, scope.x + scope.w, cy);

    bool valid = snapshot && snapshot->valid;
    if (valid && history && history->vec_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        int count = history->vec_count;
        int total_slots = FX_METER_VECTOR_HISTORY_POINTS;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
        for (int i = 0; i < count; ++i) {
            float t = total_slots > 1 ? (float)i / (float)(total_slots - 1) : 0.0f;
            float hx = 0.0f;
            float hy = 0.0f;
            history_get_vec(history, i, &hx, &hy);
            hx = clampf(hx, -1.0f, 1.0f);
            hy = clampf(hy, -1.0f, 1.0f);
            float px = hx;
            float py = hy;
            if (scope_mode == FX_METER_SCOPE_MID_SIDE) {
                float mid = 0.5f * (hx + hy);
                float side = 0.5f * (hx - hy);
                px = mid;
                py = side;
            }
            float scale = 1.5f;
            float x = (float)cx + px * (float)(scope.w / 2) * scale;
            float y = (float)cy - py * (float)(scope.h / 2) * scale;
            float fade = powf(1.0f - t, 2.6f);
            int alpha = (int)lroundf(20.0f + 235.0f * fade);
            SDL_SetRenderDrawColor(renderer, 170, 220, 120, alpha);
            if (i > 0) {
                SDL_RenderDrawLine(renderer, (int)lroundf(prev_x), (int)lroundf(prev_y), (int)lroundf(x), (int)lroundf(y));
            }
            prev_x = x;
            prev_y = y;
        }
    } else if (valid) {
        float x = clampf(snapshot->vec_x, -1.0f, 1.0f);
        float y = clampf(snapshot->vec_y, -1.0f, 1.0f);
        if (scope_mode == FX_METER_SCOPE_MID_SIDE) {
            float mid = 0.5f * (x + y);
            float side = 0.5f * (x - y);
            x = mid;
            y = side;
        }
        float scale = 1.5f;
        int px = cx + (int)lroundf(x * (float)(scope.w / 2) * scale);
        int py = cy - (int)lroundf(y * (float)(scope.h / 2) * scale);
        SDL_SetRenderDrawColor(renderer, 170, 220, 120, 255);
        SDL_Rect dot = {px - 3, py - 3, 6, 6};
        SDL_RenderFillRect(renderer, &dot);
    } else {
        ui_draw_text(renderer, scope.x + 6, scope.y + 6, "No data", dim_color, 1.0f);
    }

    ui_draw_text(renderer, rect->x + pad, rect->y + 6, "Vector Scope", label_color, 1.2f);
}
