#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static void resolve_vectorscope_theme(SDL_Color* fill,
                                      SDL_Color* border,
                                      SDL_Color* scope_bg,
                                      SDL_Color* grid,
                                      SDL_Color* trace) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (fill) *fill = theme.inspector_fill;
        if (border) *border = theme.pane_border;
        if (scope_bg) *scope_bg = theme.timeline_fill;
        if (grid) *grid = theme.grid_major;
        if (trace) *trace = theme.accent_warning;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (scope_bg) *scope_bg = (SDL_Color){30, 34, 46, 255};
    if (grid) *grid = (SDL_Color){60, 70, 90, 255};
    if (trace) *trace = (SDL_Color){170, 220, 120, 255};
}

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

static bool render_scope_history_with_adapter(SDL_Renderer* renderer,
                                              const SDL_Rect* scope_rect,
                                              const EffectsMeterHistory* history,
                                              int scope_mode) {
    if (!renderer || !scope_rect || !history || history->vec_count <= 1) {
        return false;
    }
    const int count = history->vec_count;
    float xs[FX_METER_VECTOR_HISTORY_POINTS];
    float ys[FX_METER_VECTOR_HISTORY_POINTS];
    for (int i = 0; i < count; ++i) {
        history_get_vec(history, i, &xs[i], &ys[i]);
    }

    KitVizVecSegment segments[FX_METER_VECTOR_HISTORY_POINTS];
    size_t segment_count = 0;
    DawKitVizMeterScopeMode mode = scope_mode == FX_METER_SCOPE_MID_SIDE
        ? DAW_KIT_VIZ_METER_SCOPE_MID_SIDE
        : DAW_KIT_VIZ_METER_SCOPE_LEFT_RIGHT;
    CoreResult r = daw_kit_viz_meter_plot_scope_segments(xs,
                                                          ys,
                                                          (uint32_t)count,
                                                          scope_rect,
                                                          mode,
                                                          1.5f,
                                                          segments,
                                                          FX_METER_VECTOR_HISTORY_POINTS,
                                                          &segment_count);
    if (r.code != CORE_OK || segment_count == 0) {
        return false;
    }

    for (size_t i = 0; i < segment_count; ++i) {
        float t = segment_count > 1 ? (float)i / (float)(segment_count - 1u) : 0.0f;
        SDL_Color trace = {0};
        resolve_vectorscope_theme(NULL, NULL, NULL, NULL, &trace);
        float fade = powf(1.0f - t, 2.6f);
        int alpha = (int)lroundf(20.0f + 235.0f * fade);
        SDL_SetRenderDrawColor(renderer, trace.r, trace.g, trace.b, alpha);
        const KitVizVecSegment* s = &segments[i];
        SDL_RenderDrawLine(renderer,
                           (int)lroundf(s->x0),
                           (int)lroundf(s->y0),
                           (int)lroundf(s->x1),
                           (int)lroundf(s->y1));
    }
    return true;
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
    SDL_Color border = {0};
    SDL_Color fill = {0};
    SDL_Color scope_bg = {0};
    SDL_Color grid = {0};
    SDL_Color trace = {0};
    resolve_vectorscope_theme(&fill, &border, &scope_bg, &grid, &trace);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    int pad = max_int(12, body_h / 2 + 8);
    int title_y = rect->y + max_int(4, pad / 3);
    int scope_top_gap = max_int(8, body_h / 2);
    int header_h = (title_y - (rect->y + pad)) + title_h + scope_top_gap;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    int header_text_w = rect->w - pad * 2;
    ui_draw_text_clipped(renderer,
                         rect->x + pad,
                         title_y,
                         "Vector Scope",
                         label_color,
                         1.2f,
                         header_text_w);

    SDL_Rect scope = *rect;
    scope.x += pad;
    scope.y += pad + header_h;
    scope.w -= pad * 2;
    scope.h -= pad * 2 + header_h;
    if (scope.w <= 0 || scope.h <= 0) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, scope_bg.r, scope_bg.g, scope_bg.b, scope_bg.a);
    SDL_RenderFillRect(renderer, &scope);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &scope);

    int cx = scope.x + scope.w / 2;
    int cy = scope.y + scope.h / 2;
    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    SDL_RenderDrawLine(renderer, cx, scope.y, cx, scope.y + scope.h);
    SDL_RenderDrawLine(renderer, scope.x, cy, scope.x + scope.w, cy);

    bool valid = snapshot && snapshot->valid;
    if (valid && history && history->vec_count > 1) {
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        if (render_scope_history_with_adapter(renderer, &scope, history, scope_mode)) {
            return;
        }
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
            SDL_SetRenderDrawColor(renderer, trace.r, trace.g, trace.b, alpha);
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
        SDL_SetRenderDrawColor(renderer, trace.r, trace.g, trace.b, trace.a);
        SDL_Rect dot = {px - 3, py - 3, 6, 6};
        SDL_RenderFillRect(renderer, &dot);
    } else {
        ui_draw_text_clipped(renderer, scope.x + 6, scope.y + 6, "No data", dim_color, 1.0f, scope.w - 12);
    }
}
