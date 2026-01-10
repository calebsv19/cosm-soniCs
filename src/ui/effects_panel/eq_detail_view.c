#include "ui/effects_panel_eq_detail.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel.h"
#include "ui/font.h"

#include <math.h>
#include <stdio.h>

static int map_freq_to_x(const SDL_Rect* rect, float freq);

static int map_freq_to_x(const SDL_Rect* rect, float freq);

static void draw_eq_axes(SDL_Renderer* renderer, const SDL_Rect* graph_rect, SDL_Color text) {
    if (!renderer || !graph_rect) {
        return;
    }
    int label_y = graph_rect->y + graph_rect->h + 6;
    ui_draw_text(renderer, graph_rect->x, label_y, "20", text, 1.1f);
    ui_draw_text(renderer, graph_rect->x + graph_rect->w - 34, label_y, "20k", text, 1.1f);
    int freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (int i = 0; i < 8; ++i) {
        int x = map_freq_to_x(graph_rect, (float)freqs[i]);
        const char* label = (freqs[i] >= 1000) ? "k" : NULL;
        char text_buf[8];
        if (label) {
            snprintf(text_buf, sizeof(text_buf), "%d%s", freqs[i] / 1000, label);
        } else {
            snprintf(text_buf, sizeof(text_buf), "%d", freqs[i]);
        }
        ui_draw_text(renderer, x - 6, label_y, text_buf, text, 1.0f);
    }
    ui_draw_text(renderer, graph_rect->x - 4, graph_rect->y - 18, "0 dB", text, 1.2f);
    ui_draw_text(renderer, graph_rect->x - 10, graph_rect->y + graph_rect->h - 10, "-60", text, 1.0f);
}

static int map_db_to_y(const SDL_Rect* rect, float db) {
    float t = (db - ENGINE_SPECTRUM_DB_FLOOR) / (ENGINE_SPECTRUM_DB_CEIL - ENGINE_SPECTRUM_DB_FLOOR);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float y = (float)rect->y + (1.0f - t) * (float)rect->h;
    return (int)lroundf(y);
}

static int map_freq_to_x(const SDL_Rect* rect, float freq) {
    float min_hz = ENGINE_SPECTRUM_MIN_HZ;
    float max_hz = ENGINE_SPECTRUM_MAX_HZ;
    float t = logf(freq / min_hz) / logf(max_hz / min_hz);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float x = (float)rect->x + t * (float)rect->w;
    return (int)lroundf(x);
}

static void compute_selector_rects(const SDL_Rect* panel, SDL_Rect* master, SDL_Rect* track) {
    if (!panel || !master || !track) {
        return;
    }
    int x = panel->x + panel->w - EQ_DETAIL_SELECTOR_W - EQ_DETAIL_SELECTOR_PAD;
    int y = panel->y + EQ_DETAIL_SELECTOR_PAD - 4;
    *master = (SDL_Rect){x, y, EQ_DETAIL_SELECTOR_W / 2 - 2, EQ_DETAIL_SELECTOR_H};
    *track = (SDL_Rect){x + EQ_DETAIL_SELECTOR_W / 2 + 2, y, EQ_DETAIL_SELECTOR_W / 2 - 2, EQ_DETAIL_SELECTOR_H};
}

void effects_panel_eq_detail_render(SDL_Renderer* renderer,
                                    const AppState* state,
                                    const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }

    SDL_Color label = {210, 210, 220, 255};
    SDL_Color text_dim = {150, 160, 180, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color fill = {34, 36, 44, 255};

    SDL_Rect panel = layout->detail_rect;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &panel);

    int pad = 18;
    SDL_Rect graph = panel;
    graph.x += pad;
    graph.y += pad + 6;
    graph.w -= pad * 2;
    graph.h -= pad * 2 + 18;
    if (graph.w < 4 || graph.h < 4) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 26, 28, 34, 255);
    SDL_RenderFillRect(renderer, &graph);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &graph);

    int zero_y = map_db_to_y(&graph, 0.0f);
    SDL_SetRenderDrawColor(renderer, 120, 130, 150, 255);
    SDL_RenderDrawLine(renderer, graph.x + 2, zero_y, graph.x + graph.w - 2, zero_y);

    SDL_SetRenderDrawColor(renderer, 60, 65, 80, 255);
    int grid_freqs[] = {50, 100, 1000, 5000, 10000};
    for (int i = 0; i < 5; ++i) {
        int gx = map_freq_to_x(&graph, (float)grid_freqs[i]);
        SDL_RenderDrawLine(renderer, gx, graph.y + 2, gx, graph.y + graph.h - 2);
    }

    draw_eq_axes(renderer, &graph, text_dim);
    SDL_Rect master_rect;
    SDL_Rect track_rect;
    compute_selector_rects(&panel, &master_rect, &track_rect);
    EffectsPanelEqDetailState* eq_state = &((AppState*)state)->effects_panel.eq_detail;
    SDL_Color btn_on = {80, 110, 160, 220};
    SDL_Color btn_off = {50, 55, 70, 220};
    SDL_SetRenderDrawColor(renderer,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.r : btn_off.r,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.g : btn_off.g,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.b : btn_off.b,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.a : btn_off.a);
    SDL_RenderFillRect(renderer, &master_rect);
    SDL_SetRenderDrawColor(renderer,
                           eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.r : btn_off.r,
                           eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.g : btn_off.g,
                           eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.b : btn_off.b,
                           eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.a : btn_off.a);
    SDL_RenderFillRect(renderer, &track_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &master_rect);
    SDL_RenderDrawRect(renderer, &track_rect);
    ui_draw_text(renderer, master_rect.x + 6, master_rect.y + 2, "Master", label, 1.0f);
    ui_draw_text(renderer, track_rect.x + 8, track_rect.y + 2, "Track", label, 1.0f);

    int track_index = -1;
    bool use_track = (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK) &&
                     state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                     state->effects_panel.target_track_index >= 0;
    if (use_track) {
        track_index = state->effects_panel.target_track_index;
        if (eq_state->last_track_index != track_index) {
            eq_state->spectrum_ready = false;
            eq_state->last_track_index = track_index;
        }
    } else {
        eq_state->last_track_index = -1;
    }

    float bins[ENGINE_SPECTRUM_BINS];
    int count = 0;
    if (state->engine) {
        EngineSpectrumView view = use_track ? ENGINE_SPECTRUM_VIEW_TRACK : ENGINE_SPECTRUM_VIEW_MASTER;
        int target = use_track ? track_index : -1;
        engine_set_spectrum_target(state->engine, view, target, true);
        if (use_track) {
            count = engine_get_track_spectrum_snapshot(state->engine, track_index, bins, ENGINE_SPECTRUM_BINS);
        } else {
            count = engine_get_spectrum_snapshot(state->engine, bins, ENGINE_SPECTRUM_BINS);
        }
    }
    if (count <= 0) {
        return;
    }
    if (!eq_state->spectrum_ready) {
        for (int i = 0; i < count; ++i) {
            eq_state->spectrum_smooth[i] = bins[i];
        }
        eq_state->spectrum_ready = true;
    } else {
        const float alpha = 0.2f;
        for (int i = 0; i < count; ++i) {
            float current = bins[i];
            float prev = eq_state->spectrum_smooth[i];
            eq_state->spectrum_smooth[i] = prev + alpha * (current - prev);
        }
    }

    SDL_SetRenderDrawColor(renderer, 120, 180, 220, 255);
    int prev_x = graph.x;
    int prev_y = map_db_to_y(&graph, eq_state->spectrum_smooth[0]);
    for (int i = 1; i < count; ++i) {
        float t = (float)i / (float)(count - 1);
        int x = graph.x + (int)lroundf(t * (float)graph.w);
        int y = map_db_to_y(&graph, eq_state->spectrum_smooth[i]);
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}
