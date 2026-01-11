#include "ui/effects_panel_eq_detail.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "ui/render_utils.h"

#include <math.h>
#include <stdio.h>

static void compute_toggle_rects(const SDL_Rect* panel,
                                 SDL_Rect* low,
                                 SDL_Rect mids[4],
                                 SDL_Rect* high) {
    if (!panel || !low || !mids || !high) {
        return;
    }
    int y = panel->y + EQ_DETAIL_SELECTOR_PAD - 4;
    int size = EQ_DETAIL_TOGGLE_SIZE;
    int group_w = 6 * size + 5 * EQ_DETAIL_TOGGLE_GAP;
    int x = panel->x + (panel->w - group_w) / 2;
    *low = (SDL_Rect){x, y, size, size};
    x += size + EQ_DETAIL_TOGGLE_GAP;
    for (int i = 0; i < 4; ++i) {
        mids[i] = (SDL_Rect){x, y, size, size};
        x += size + EQ_DETAIL_TOGGLE_GAP;
    }
    *high = (SDL_Rect){x, y, size, size};
}

static void draw_eq_axes(SDL_Renderer* renderer, const SDL_Rect* graph_rect, SDL_Color text) {
    if (!renderer || !graph_rect) {
        return;
    }
    int label_y = graph_rect->y + graph_rect->h + 6;
    ui_draw_text(renderer, graph_rect->x, label_y, "20", text, 1.1f);
    ui_draw_text(renderer, graph_rect->x + graph_rect->w - 34, label_y, "20k", text, 1.1f);
    int freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (int i = 0; i < 8; ++i) {
        int x = (int)lroundf(effects_eq_freq_to_x(graph_rect, (float)freqs[i]));
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
    ui_draw_text(renderer, graph_rect->x - 10, graph_rect->y + graph_rect->h - 10, "-30", text, 1.0f);
    ui_draw_text(renderer, graph_rect->x - 10, graph_rect->y + 4, "+30", text, 1.0f);
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

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static SDL_Rect build_graph_rect(const SDL_Rect* panel, int pad) {
    SDL_Rect graph = *panel;
    graph.x += pad;
    graph.y += pad + 6;
    graph.w -= pad * 2;
    graph.h -= pad * 2 + 18;
    return graph;
}

static void draw_panel_background(SDL_Renderer* renderer,
                                  const SDL_Rect* panel,
                                  SDL_Color fill,
                                  SDL_Color border) {
    if (!renderer || !panel) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, panel);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, panel);
}

static void draw_graph_background(SDL_Renderer* renderer,
                                  const SDL_Rect* graph,
                                  SDL_Color border) {
    if (!renderer || !graph) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 26, 28, 34, 255);
    SDL_RenderFillRect(renderer, graph);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, graph);
}

static void draw_graph_grid(SDL_Renderer* renderer, const SDL_Rect* graph) {
    if (!renderer || !graph) {
        return;
    }
    int zero_y = (int)lroundf(effects_eq_db_to_y(graph, 0.0f));
    SDL_SetRenderDrawColor(renderer, 120, 130, 150, 255);
    SDL_RenderDrawLine(renderer, graph->x + 2, zero_y, graph->x + graph->w - 2, zero_y);

    SDL_SetRenderDrawColor(renderer, 60, 65, 80, 255);
    int grid_freqs[] = {50, 100, 1000, 5000, 10000};
    for (int i = 0; i < 5; ++i) {
        int gx = (int)lroundf(effects_eq_freq_to_x(graph, (float)grid_freqs[i]));
        SDL_RenderDrawLine(renderer, gx, graph->y + 2, gx, graph->y + graph->h - 2);
    }
}

static void draw_band_toggles(SDL_Renderer* renderer,
                              const SDL_Rect* panel,
                              const EqCurveState* curve,
                              SDL_Color label,
                              SDL_Color btn_on,
                              SDL_Color btn_off,
                              SDL_Color btn_border,
                              SDL_Color btn_hover) {
    SDL_Rect low_rect;
    SDL_Rect mid_rects[4];
    SDL_Rect high_rect;
    compute_toggle_rects(panel, &low_rect, mid_rects, &high_rect);

    SDL_SetRenderDrawColor(renderer,
                           curve->hover_toggle_low ? btn_hover.r : (curve->low_cut.enabled ? btn_on.r : btn_off.r),
                           curve->hover_toggle_low ? btn_hover.g : (curve->low_cut.enabled ? btn_on.g : btn_off.g),
                           curve->hover_toggle_low ? btn_hover.b : (curve->low_cut.enabled ? btn_on.b : btn_off.b),
                           curve->hover_toggle_low ? btn_hover.a : (curve->low_cut.enabled ? btn_on.a : btn_off.a));
    SDL_RenderFillRect(renderer, &low_rect);
    SDL_SetRenderDrawColor(renderer, btn_border.r, btn_border.g, btn_border.b, btn_border.a);
    SDL_RenderDrawRect(renderer, &low_rect);
    ui_draw_text(renderer, low_rect.x + 4, low_rect.y + 2, "L", label, 1.0f);

    for (int i = 0; i < 4; ++i) {
        bool enabled = curve->bands[i].enabled;
        SDL_SetRenderDrawColor(renderer,
                               curve->hover_toggle_band == i ? btn_hover.r : (enabled ? btn_on.r : btn_off.r),
                               curve->hover_toggle_band == i ? btn_hover.g : (enabled ? btn_on.g : btn_off.g),
                               curve->hover_toggle_band == i ? btn_hover.b : (enabled ? btn_on.b : btn_off.b),
                               curve->hover_toggle_band == i ? btn_hover.a : (enabled ? btn_on.a : btn_off.a));
        SDL_RenderFillRect(renderer, &mid_rects[i]);
        SDL_SetRenderDrawColor(renderer, btn_border.r, btn_border.g, btn_border.b, btn_border.a);
        SDL_RenderDrawRect(renderer, &mid_rects[i]);
        char label_text[2];
        label_text[0] = (char)('1' + i);
        label_text[1] = '\0';
        ui_draw_text(renderer, mid_rects[i].x + 4, mid_rects[i].y + 2, label_text, label, 1.0f);
    }

    SDL_SetRenderDrawColor(renderer,
                           curve->hover_toggle_high ? btn_hover.r : (curve->high_cut.enabled ? btn_on.r : btn_off.r),
                           curve->hover_toggle_high ? btn_hover.g : (curve->high_cut.enabled ? btn_on.g : btn_off.g),
                           curve->hover_toggle_high ? btn_hover.b : (curve->high_cut.enabled ? btn_on.b : btn_off.b),
                           curve->hover_toggle_high ? btn_hover.a : (curve->high_cut.enabled ? btn_on.a : btn_off.a));
    SDL_RenderFillRect(renderer, &high_rect);
    SDL_SetRenderDrawColor(renderer, btn_border.r, btn_border.g, btn_border.b, btn_border.a);
    SDL_RenderDrawRect(renderer, &high_rect);
    ui_draw_text(renderer, high_rect.x + 4, high_rect.y + 2, "H", label, 1.0f);
}

static void draw_mode_buttons(SDL_Renderer* renderer,
                              const SDL_Rect* panel,
                              const EffectsPanelEqDetailState* eq_state,
                              bool track_available,
                              SDL_Color label,
                              SDL_Color text_dim,
                              SDL_Color border,
                              SDL_Color btn_on,
                              SDL_Color btn_off,
                              SDL_Color btn_disabled) {
    SDL_Rect master_rect;
    SDL_Rect track_rect;
    compute_selector_rects(panel, &master_rect, &track_rect);

    SDL_SetRenderDrawColor(renderer,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.r : btn_off.r,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.g : btn_off.g,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.b : btn_off.b,
                           eq_state->view_mode == EQ_DETAIL_VIEW_MASTER ? btn_on.a : btn_off.a);
    SDL_RenderFillRect(renderer, &master_rect);
    SDL_SetRenderDrawColor(renderer,
                           !track_available ? btn_disabled.r :
                           (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.r : btn_off.r),
                           !track_available ? btn_disabled.g :
                           (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.g : btn_off.g),
                           !track_available ? btn_disabled.b :
                           (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.b : btn_off.b),
                           !track_available ? btn_disabled.a :
                           (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK ? btn_on.a : btn_off.a));
    SDL_RenderFillRect(renderer, &track_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &master_rect);
    SDL_RenderDrawRect(renderer, &track_rect);
    ui_draw_text(renderer, master_rect.x + 6, master_rect.y + 2, "Master", label, 1.0f);
    SDL_Color track_label = track_available ? label : text_dim;
    ui_draw_text(renderer, track_rect.x + 8, track_rect.y + 2, "Track", track_label, 1.0f);
}

static int fetch_spectrum_bins(const AppState* state,
                               bool use_track,
                               int track_index,
                               float* bins,
                               int max_bins) {
    if (!state || !state->engine) {
        return 0;
    }
    EngineSpectrumView view = use_track ? ENGINE_SPECTRUM_VIEW_TRACK : ENGINE_SPECTRUM_VIEW_MASTER;
    int target = use_track ? track_index : -1;
    engine_set_spectrum_target(state->engine, view, target, true);
    if (use_track) {
        return engine_get_track_spectrum_snapshot(state->engine, track_index, bins, max_bins);
    }
    return engine_get_spectrum_snapshot(state->engine, bins, max_bins);
}

static int update_spectrum_smooth(EffectsPanelEqDetailState* eq_state,
                                  const float* bins,
                                  int count) {
    if (!eq_state || !bins) {
        return 0;
    }
    if (count <= 0) {
        if (!eq_state->spectrum_ready) {
            return 0;
        }
        return ENGINE_SPECTRUM_BINS;
    }
    if (!eq_state->spectrum_ready) {
        for (int i = 0; i < count; ++i) {
            eq_state->spectrum_smooth[i] = bins[i];
        }
        eq_state->spectrum_ready = true;
        return count;
    }
    const float alpha_up = 0.45f;
    const float alpha_down = 0.08f;
    for (int i = 0; i < count; ++i) {
        float current = bins[i];
        float prev = eq_state->spectrum_smooth[i];
        float alpha = current > prev ? alpha_up : alpha_down;
        eq_state->spectrum_smooth[i] = prev + alpha * (current - prev);
    }
    return count;
}

static void compute_spectrum_display(const EffectsPanelEqDetailState* eq_state,
                                     int count,
                                     float* spectrum_out) {
    float floor_db = ENGINE_SPECTRUM_DB_FLOOR;
    float ceil_db = ENGINE_SPECTRUM_DB_CEIL;
    for (int i = 0; i < count; ++i) {
        float db = eq_state->spectrum_smooth[i];
        db = clampf(db, floor_db, ceil_db);
        float t;
        if (db <= 0.0f) {
            t = (db - floor_db) / (0.0f - floor_db);
        } else {
            t = 1.0f + (db / ceil_db) * 0.25f;
        }
        spectrum_out[i] = clampf(t, 0.0f, 1.0f);
    }
}

static void draw_spectrum_line(SDL_Renderer* renderer,
                               const SDL_Rect* graph,
                               const float* spectrum,
                               int count) {
    if (!renderer || !graph || !spectrum || count <= 1) {
        return;
    }
    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 120, 180, 220, 120);
    int prev_x = graph->x;
    float h0 = clampf(spectrum[0], 0.0f, 1.0f);
    int base_y = graph->y + graph->h - 1;
    int prev_y = base_y - (int)lroundf(h0 * (float)graph->h);
    for (int i = 1; i < count; ++i) {
        float t = (float)i / (float)(count - 1);
        int x = graph->x + (int)lroundf(t * (float)graph->w);
        float h = clampf(spectrum[i], 0.0f, 1.0f);
        int y = base_y - (int)lroundf(h * (float)graph->h);
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

static void compute_eq_curve(const EqCurveState* curve, const SDL_Rect* graph, float* curve_db) {
    for (int i = 0; i < EQ_DETAIL_CURVE_SAMPLES; ++i) {
        float t = (float)i / (float)(EQ_DETAIL_CURVE_SAMPLES - 1);
        float x = (float)graph->x + t * (float)graph->w;
        float freq = effects_eq_x_to_freq(graph, x);
        float db = 0.0f;
        if (curve->low_cut.enabled && freq < curve->low_cut.freq_hz) {
            float tcut = log2f(curve->low_cut.freq_hz / freq);
            float max_oct = 0.8f;
            float s = tcut / max_oct;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            float drop = s * s * (3.0f - 2.0f * s);
            db += (EQ_DETAIL_DB_MIN - db) * drop;
        }
        if (curve->high_cut.enabled && freq > curve->high_cut.freq_hz) {
            float tcut = log2f(freq / curve->high_cut.freq_hz);
            float max_oct = 1.2f;
            float s = tcut / max_oct;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            float drop = s * s * (3.0f - 2.0f * s);
            db += (EQ_DETAIL_DB_MIN - db) * drop;
        }
        for (int b = 0; b < 4; ++b) {
            if (!curve->bands[b].enabled) {
                continue;
            }
            float width = curve->bands[b].q_width;
            if (width < 0.1f) {
                width = 0.1f;
            }
            float x_oct = log2f(freq / curve->bands[b].freq_hz);
            float sigma = width * 0.35f;
            float influence = expf(-(x_oct * x_oct) / (2.0f * sigma * sigma));
            db += curve->bands[b].gain_db * influence;
        }
        curve_db[i] = clampf(db, EQ_DETAIL_DB_MIN, EQ_DETAIL_DB_MAX);
    }
}

static void draw_eq_curve(SDL_Renderer* renderer, const SDL_Rect* graph, const float* curve_db) {
    SDL_SetRenderDrawColor(renderer, 70, 160, 230, 255);
    int prev_x = graph->x;
    int prev_y = (int)lroundf(effects_eq_db_to_y(graph, curve_db[0]));
    for (int i = 1; i < EQ_DETAIL_CURVE_SAMPLES; ++i) {
        float t = (float)i / (float)(EQ_DETAIL_CURVE_SAMPLES - 1);
        int x = graph->x + (int)lroundf(t * (float)graph->w);
        int y = (int)lroundf(effects_eq_db_to_y(graph, curve_db[i]));
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

static void draw_band_handles(SDL_Renderer* renderer, const SDL_Rect* graph, const EqCurveState* curve) {
    SDL_SetRenderDrawColor(renderer, 110, 205, 190, 255);
    for (int b = 0; b < 4; ++b) {
        if (!curve->bands[b].enabled) {
            continue;
        }
        float freq = curve->bands[b].freq_hz;
        float gain = curve->bands[b].gain_db;
        float width = curve->bands[b].q_width;
        if (width < 0.1f) {
            width = 0.1f;
        }
        float x_center = effects_eq_freq_to_x(graph, freq);
        float y_center = effects_eq_db_to_y(graph, gain);
        bool hovered = (curve->hover_band == b && curve->hover_handle == EQ_CURVE_HANDLE_POINT);
        SDL_Rect point = {
            (int)lroundf(x_center) - 3,
            (int)lroundf(y_center) - 3,
            6,
            6
        };
        if (hovered) {
            SDL_SetRenderDrawColor(renderer, 150, 230, 215, 255);
        }
        SDL_RenderFillRect(renderer, &point);
        if (hovered) {
            SDL_SetRenderDrawColor(renderer, 110, 205, 190, 255);
        }
        float left_freq = freq * powf(2.0f, -width * 0.5f);
        float right_freq = freq * powf(2.0f, width * 0.5f);
        float x_left = effects_eq_freq_to_x(graph, left_freq);
        float x_right = effects_eq_freq_to_x(graph, right_freq);
        int y0 = (int)lroundf(y_center) - 8;
        int y1 = (int)lroundf(y_center) + 8;
        SDL_SetRenderDrawColor(renderer, 110, 205, 190, 120);
        SDL_RenderDrawLine(renderer, (int)lroundf(x_left), (int)lroundf(y_center),
                           (int)lroundf(x_center), (int)lroundf(y_center));
        SDL_RenderDrawLine(renderer, (int)lroundf(x_center), (int)lroundf(y_center),
                           (int)lroundf(x_right), (int)lroundf(y_center));
        SDL_SetRenderDrawColor(renderer, 110, 205, 190, 255);
        if (curve->hover_band == b && curve->hover_handle == EQ_CURVE_HANDLE_WIDTH) {
            SDL_SetRenderDrawColor(renderer, 150, 230, 215, 255);
        }
        SDL_RenderDrawLine(renderer, (int)lroundf(x_left), y0, (int)lroundf(x_left), y1);
        SDL_RenderDrawLine(renderer, (int)lroundf(x_right), y0, (int)lroundf(x_right), y1);
        if (curve->hover_band == b && curve->hover_handle == EQ_CURVE_HANDLE_WIDTH) {
            SDL_SetRenderDrawColor(renderer, 110, 205, 190, 255);
        }
    }
}

static void draw_cut_lines(SDL_Renderer* renderer, const SDL_Rect* graph, const EqCurveState* curve) {
    if (curve->low_cut.enabled) {
        float x_cut = effects_eq_freq_to_x(graph, curve->low_cut.freq_hz);
        SDL_SetRenderDrawColor(renderer,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_LOW ? 220 : 160,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_LOW ? 210 : 180,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_LOW ? 90 : 120,
                               220);
        SDL_RenderDrawLine(renderer, (int)lroundf(x_cut), graph->y + 2, (int)lroundf(x_cut), graph->y + graph->h - 2);
    }
    if (curve->high_cut.enabled) {
        float x_cut = effects_eq_freq_to_x(graph, curve->high_cut.freq_hz);
        SDL_SetRenderDrawColor(renderer,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_HIGH ? 220 : 160,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_HIGH ? 210 : 180,
                               curve->hover_handle == EQ_CURVE_HANDLE_CUT_HIGH ? 90 : 120,
                               220);
        SDL_RenderDrawLine(renderer, (int)lroundf(x_cut), graph->y + 2, (int)lroundf(x_cut), graph->y + graph->h - 2);
    }
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
    draw_panel_background(renderer, &panel, fill, border);

    int pad = 18;
    SDL_Rect graph = build_graph_rect(&panel, pad);
    if (graph.w < 4 || graph.h < 4) {
        return;
    }

    draw_graph_background(renderer, &graph, border);
    draw_graph_grid(renderer, &graph);
    draw_eq_axes(renderer, &graph, text_dim);

    EffectsPanelEqDetailState* eq_state = &((AppState*)state)->effects_panel.eq_detail;
    EqCurveState* curve = &((AppState*)state)->effects_panel.eq_curve;
    SDL_Color btn_on = {80, 110, 160, 220};
    SDL_Color btn_off = {50, 55, 70, 220};
    SDL_Color btn_disabled = {35, 40, 52, 200};
    SDL_Color btn_border = {90, 95, 110, 255};
    SDL_Color btn_hover = {110, 140, 190, 240};

    draw_band_toggles(renderer, &panel, curve, label, btn_on, btn_off, btn_border, btn_hover);
    bool track_available = state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                           state->effects_panel.target_track_index >= 0;
    draw_mode_buttons(renderer, &panel, eq_state, track_available,
                      label, text_dim, border, btn_on, btn_off, btn_disabled);

    int track_index = -1;
    bool use_track = (eq_state->view_mode == EQ_DETAIL_VIEW_TRACK) && track_available;
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
    int count = fetch_spectrum_bins(state, use_track, track_index, bins, ENGINE_SPECTRUM_BINS);
    count = update_spectrum_smooth(eq_state, bins, count);
    if (count <= 0) {
        return;
    }

    float spectrum_norm[ENGINE_SPECTRUM_BINS];
    compute_spectrum_display(eq_state, count, spectrum_norm);
    draw_spectrum_line(renderer, &graph, spectrum_norm, count);

    float curve_db[EQ_DETAIL_CURVE_SAMPLES];
    compute_eq_curve(curve, &graph, curve_db);
    draw_eq_curve(renderer, &graph, curve_db);
    draw_band_handles(renderer, &graph, curve);
    draw_cut_lines(renderer, &graph, curve);
}
