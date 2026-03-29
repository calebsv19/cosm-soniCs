#include "input/effects_panel_eq_detail_input.h"

#include "app_state.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_eq_detail.h"
#include "undo/undo_manager.h"

#include <math.h>

static void eq_curve_to_engine(const EqCurveState* src, EngineEqCurve* dst) {
    if (!src || !dst) {
        return;
    }
    dst->low_cut.enabled = src->low_cut.enabled;
    dst->low_cut.freq_hz = src->low_cut.freq_hz;
    dst->high_cut.enabled = src->high_cut.enabled;
    dst->high_cut.freq_hz = src->high_cut.freq_hz;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        dst->bands[i].enabled = src->bands[i].enabled;
        dst->bands[i].freq_hz = src->bands[i].freq_hz;
        dst->bands[i].gain_db = src->bands[i].gain_db;
        dst->bands[i].q_width = src->bands[i].q_width;
    }
}

static void eq_curve_to_session(const EqCurveState* src, SessionEqCurve* dst) {
    if (!src || !dst) {
        return;
    }
    dst->low_cut.enabled = src->low_cut.enabled;
    dst->low_cut.freq_hz = src->low_cut.freq_hz;
    dst->low_cut.slope = src->low_cut.slope;
    dst->high_cut.enabled = src->high_cut.enabled;
    dst->high_cut.freq_hz = src->high_cut.freq_hz;
    dst->high_cut.slope = src->high_cut.slope;
    for (int i = 0; i < 4; ++i) {
        dst->bands[i].enabled = src->bands[i].enabled;
        dst->bands[i].freq_hz = src->bands[i].freq_hz;
        dst->bands[i].gain_db = src->bands[i].gain_db;
        dst->bands[i].q_width = src->bands[i].q_width;
    }
}

static bool session_eq_equal(const SessionEqCurve* a, const SessionEqCurve* b) {
    if (!a || !b) {
        return true;
    }
    if (a->low_cut.enabled != b->low_cut.enabled ||
        a->high_cut.enabled != b->high_cut.enabled) {
        return false;
    }
    if (fabsf(a->low_cut.freq_hz - b->low_cut.freq_hz) > 0.001f ||
        fabsf(a->high_cut.freq_hz - b->high_cut.freq_hz) > 0.001f) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (a->bands[i].enabled != b->bands[i].enabled) {
            return false;
        }
        if (fabsf(a->bands[i].freq_hz - b->bands[i].freq_hz) > 0.001f ||
            fabsf(a->bands[i].gain_db - b->bands[i].gain_db) > 0.001f ||
            fabsf(a->bands[i].q_width - b->bands[i].q_width) > 0.001f) {
            return false;
        }
    }
    return true;
}

static void eq_detail_apply_curve(AppState* state);

static void eq_curve_set_defaults(EqCurveState* curve) {
    if (!curve) {
        return;
    }
    SDL_zero(*curve);
    curve->low_cut.enabled = false;
    curve->low_cut.freq_hz = 80.0f;
    curve->low_cut.slope = 12.0f;
    curve->high_cut.enabled = false;
    curve->high_cut.freq_hz = 12000.0f;
    curve->high_cut.slope = 12.0f;
    curve->selected_band = -1;
    curve->selected_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_band = -1;
    curve->hover_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_toggle_band = -1;
    curve->hover_toggle_low = false;
    curve->hover_toggle_high = false;
    for (int i = 0; i < 4; ++i) {
        curve->bands[i].enabled = true;
        curve->bands[i].gain_db = 0.0f;
        curve->bands[i].q_width = 1.0f;
    }
    curve->bands[0].freq_hz = 120.0f;
    curve->bands[1].freq_hz = 500.0f;
    curve->bands[2].freq_hz = 2000.0f;
    curve->bands[3].freq_hz = 8000.0f;
}

static void eq_detail_reset_curve(AppState* state) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_EQ_CURVE;
    if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        panel->target == FX_PANEL_TARGET_TRACK &&
        panel->target_track_index >= 0) {
        cmd.data.eq_curve_edit.is_master = false;
        cmd.data.eq_curve_edit.track_index = panel->target_track_index;
    } else {
        cmd.data.eq_curve_edit.is_master = true;
        cmd.data.eq_curve_edit.track_index = -1;
    }
    eq_curve_to_session(&panel->eq_curve, &cmd.data.eq_curve_edit.before);
    eq_curve_set_defaults(&panel->eq_curve);
    if (cmd.data.eq_curve_edit.is_master) {
        panel->eq_curve_master = panel->eq_curve;
    } else if (panel->eq_curve_tracks &&
               cmd.data.eq_curve_edit.track_index >= 0 &&
               cmd.data.eq_curve_edit.track_index < panel->eq_curve_tracks_count) {
        panel->eq_curve_tracks[cmd.data.eq_curve_edit.track_index] = panel->eq_curve;
    }
    eq_detail_apply_curve(state);
    eq_curve_to_session(&panel->eq_curve, &cmd.data.eq_curve_edit.after);
    if (!session_eq_equal(&cmd.data.eq_curve_edit.before, &cmd.data.eq_curve_edit.after)) {
        undo_manager_push(&state->undo, &cmd);
    }
}

static void eq_detail_begin_undo_drag(AppState* state) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_EQ_CURVE;
    if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        panel->target == FX_PANEL_TARGET_TRACK &&
        panel->target_track_index >= 0) {
        cmd.data.eq_curve_edit.is_master = false;
        cmd.data.eq_curve_edit.track_index = panel->target_track_index;
    } else {
        cmd.data.eq_curve_edit.is_master = true;
        cmd.data.eq_curve_edit.track_index = -1;
    }
    eq_curve_to_session(&panel->eq_curve, &cmd.data.eq_curve_edit.before);
    cmd.data.eq_curve_edit.after = cmd.data.eq_curve_edit.before;
    undo_manager_begin_drag(&state->undo, &cmd);
}

static void eq_detail_apply_curve(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    EngineEqCurve curve;
    eq_curve_to_engine(&panel->eq_curve, &curve);
    if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        panel->target == FX_PANEL_TARGET_TRACK &&
        panel->target_track_index >= 0) {
        engine_set_track_eq_curve(state->engine, panel->target_track_index, &curve);
    } else {
        engine_set_master_eq_curve(state->engine, &curve);
    }
}

static void eq_detail_apply_curve_throttled(AppState* state, Uint32 now_ticks) {
    const Uint32 interval_ms = 80;
    if (!state) {
        return;
    }
    EffectsPanelEqDetailState* eq_detail = &state->effects_panel.eq_detail;
    if (!eq_detail->pending_apply) {
        return;
    }
    if (now_ticks - eq_detail->last_apply_ticks >= interval_ms) {
        eq_detail_apply_curve(state);
        eq_detail->last_apply_ticks = now_ticks;
        eq_detail->pending_apply = false;
    }
}

static bool eq_detail_hit(const EffectsPanelLayout* layout, const SDL_Point* pt) {
    if (!layout || !pt) {
        return false;
    }
    return SDL_PointInRect(pt, &layout->detail_rect);
}

static void update_hover_state(AppState* state,
                               const SDL_Rect* panel,
                               const SDL_Rect* graph,
                               const SDL_Point* pt) {
    EqCurveState* curve = &state->effects_panel.eq_curve;
    curve->hover_band = -1;
    curve->hover_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_toggle_band = -1;
    curve->hover_toggle_low = false;
    curve->hover_toggle_high = false;

    SDL_Rect low_rect;
    SDL_Rect mid_rects[4];
    SDL_Rect high_rect;
    effects_panel_eq_detail_compute_toggle_rects(panel, &low_rect, mid_rects, &high_rect);
    if (SDL_PointInRect(pt, &low_rect)) {
        curve->hover_toggle_low = true;
        return;
    }
    if (SDL_PointInRect(pt, &high_rect)) {
        curve->hover_toggle_high = true;
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (SDL_PointInRect(pt, &mid_rects[i])) {
            curve->hover_toggle_band = i;
            return;
        }
    }
    if (!graph || graph->w <= 0 || graph->h <= 0) {
        return;
    }
    if (curve->low_cut.enabled) {
        float x_cut = effects_eq_freq_to_x(graph, curve->low_cut.freq_hz);
        if (fabsf((float)pt->x - x_cut) <= 6.0f &&
            pt->y >= graph->y && pt->y <= graph->y + graph->h) {
            curve->hover_handle = EQ_CURVE_HANDLE_CUT_LOW;
            return;
        }
    }
    if (curve->high_cut.enabled) {
        float x_cut = effects_eq_freq_to_x(graph, curve->high_cut.freq_hz);
        if (fabsf((float)pt->x - x_cut) <= 6.0f &&
            pt->y >= graph->y && pt->y <= graph->y + graph->h) {
            curve->hover_handle = EQ_CURVE_HANDLE_CUT_HIGH;
            return;
        }
    }
    for (int b = 0; b < 4; ++b) {
        if (!curve->bands[b].enabled) {
            continue;
        }
        float x_center = effects_eq_freq_to_x(graph, curve->bands[b].freq_hz);
        float y_center = effects_eq_db_to_y(graph, curve->bands[b].gain_db);
        SDL_Rect point_rect = {
            (int)lroundf(x_center) - 5,
            (int)lroundf(y_center) - 5,
            10,
            10
        };
        if (SDL_PointInRect(pt, &point_rect)) {
            curve->hover_band = b;
            curve->hover_handle = EQ_CURVE_HANDLE_POINT;
            return;
        }
        float width = curve->bands[b].q_width;
        if (width < 0.1f) {
            width = 0.1f;
        }
        float left_freq = curve->bands[b].freq_hz * powf(2.0f, -width * 0.5f);
        float right_freq = curve->bands[b].freq_hz * powf(2.0f, width * 0.5f);
        float x_left = effects_eq_freq_to_x(graph, left_freq);
        float x_right = effects_eq_freq_to_x(graph, right_freq);
        SDL_Rect left_rect = {
            (int)lroundf(x_left) - 4,
            (int)lroundf(y_center) - 6,
            8,
            12
        };
        SDL_Rect right_rect = {
            (int)lroundf(x_right) - 4,
            (int)lroundf(y_center) - 6,
            8,
            12
        };
        if (SDL_PointInRect(pt, &left_rect) || SDL_PointInRect(pt, &right_rect)) {
            curve->hover_band = b;
            curve->hover_handle = EQ_CURVE_HANDLE_WIDTH;
            return;
        }
    }
}

static void set_band_width_from_freq(EqCurveBand* band, float freq_hz) {
    float ratio = freq_hz / band->freq_hz;
    if (ratio < 1.0f) {
        ratio = 1.0f / ratio;
    }
    float width = 2.0f * log2f(ratio);
    if (width < 0.1f) width = 0.1f;
    if (width > 4.0f) width = 4.0f;
    band->q_width = width;
}

bool effects_panel_eq_detail_handle_mouse_down(AppState* state,
                                               const EffectsPanelLayout* layout,
                                               const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    SDL_Point pt = {event->button.x, event->button.y};
    if (!eq_detail_hit(layout, &pt)) {
        state->effects_panel.eq_detail.hovered = false;
        return false;
    }
    SDL_Rect graph = effects_panel_eq_detail_compute_graph_rect(&layout->detail_rect);
    SDL_Rect master_rect;
    SDL_Rect track_rect;
    effects_panel_eq_detail_compute_selector_rects(&layout->detail_rect, &master_rect, &track_rect);
    bool track_available = state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                           state->effects_panel.target_track_index >= 0;
    SDL_Rect low_rect;
    SDL_Rect mid_rects[4];
    SDL_Rect high_rect;
    effects_panel_eq_detail_compute_toggle_rects(&layout->detail_rect, &low_rect, mid_rects, &high_rect);
    if (SDL_PointInRect(&pt, &master_rect)) {
        effects_panel_set_eq_detail_view(state, EQ_DETAIL_VIEW_MASTER);
        return true;
    }
    if (SDL_PointInRect(&pt, &track_rect)) {
        if (track_available) {
            effects_panel_set_eq_detail_view(state, EQ_DETAIL_VIEW_TRACK);
            return true;
        }
        return true;
    }
    if (event->button.clicks >= 2 && SDL_PointInRect(&pt, &graph)) {
        eq_detail_reset_curve(state);
        return true;
    }
    EqCurveState* curve = &state->effects_panel.eq_curve;
    if (SDL_PointInRect(&pt, &low_rect)) {
        UndoCommand cmd = {0};
        cmd.type = UNDO_CMD_EQ_CURVE;
        cmd.data.eq_curve_edit.is_master = !(state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
                                             state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                                             state->effects_panel.target_track_index >= 0);
        cmd.data.eq_curve_edit.track_index = cmd.data.eq_curve_edit.is_master ? -1 : state->effects_panel.target_track_index;
        eq_curve_to_session(curve, &cmd.data.eq_curve_edit.before);
        curve->low_cut.enabled = !curve->low_cut.enabled;
        eq_detail_apply_curve(state);
        eq_curve_to_session(curve, &cmd.data.eq_curve_edit.after);
        if (!session_eq_equal(&cmd.data.eq_curve_edit.before, &cmd.data.eq_curve_edit.after)) {
            undo_manager_push(&state->undo, &cmd);
        }
        return true;
    }
    if (SDL_PointInRect(&pt, &high_rect)) {
        UndoCommand cmd = {0};
        cmd.type = UNDO_CMD_EQ_CURVE;
        cmd.data.eq_curve_edit.is_master = !(state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
                                             state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                                             state->effects_panel.target_track_index >= 0);
        cmd.data.eq_curve_edit.track_index = cmd.data.eq_curve_edit.is_master ? -1 : state->effects_panel.target_track_index;
        eq_curve_to_session(curve, &cmd.data.eq_curve_edit.before);
        curve->high_cut.enabled = !curve->high_cut.enabled;
        eq_detail_apply_curve(state);
        eq_curve_to_session(curve, &cmd.data.eq_curve_edit.after);
        if (!session_eq_equal(&cmd.data.eq_curve_edit.before, &cmd.data.eq_curve_edit.after)) {
            undo_manager_push(&state->undo, &cmd);
        }
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (SDL_PointInRect(&pt, &mid_rects[i])) {
            UndoCommand cmd = {0};
            cmd.type = UNDO_CMD_EQ_CURVE;
            cmd.data.eq_curve_edit.is_master = !(state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
                                                 state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
                                                 state->effects_panel.target_track_index >= 0);
            cmd.data.eq_curve_edit.track_index = cmd.data.eq_curve_edit.is_master ? -1 : state->effects_panel.target_track_index;
            eq_curve_to_session(curve, &cmd.data.eq_curve_edit.before);
            curve->bands[i].enabled = !curve->bands[i].enabled;
            if (!curve->bands[i].enabled && curve->selected_band == i) {
                curve->selected_band = -1;
                curve->selected_handle = EQ_CURVE_HANDLE_NONE;
            }
            eq_detail_apply_curve(state);
            eq_curve_to_session(curve, &cmd.data.eq_curve_edit.after);
            if (!session_eq_equal(&cmd.data.eq_curve_edit.before, &cmd.data.eq_curve_edit.after)) {
                undo_manager_push(&state->undo, &cmd);
            }
            return true;
        }
    }
    if (graph.w > 0 && graph.h > 0) {
        if (curve->low_cut.enabled) {
            float x_cut = effects_eq_freq_to_x(&graph, curve->low_cut.freq_hz);
            if (fabsf((float)pt.x - x_cut) <= 6.0f) {
                eq_detail_begin_undo_drag(state);
                curve->selected_band = -1;
                curve->selected_handle = EQ_CURVE_HANDLE_CUT_LOW;
                state->effects_panel.eq_detail.dragging = true;
                state->effects_panel.eq_detail.last_mouse = pt;
                state->effects_panel.eq_detail.pending_apply = true;
                return true;
            }
        }
        if (curve->high_cut.enabled) {
            float x_cut = effects_eq_freq_to_x(&graph, curve->high_cut.freq_hz);
            if (fabsf((float)pt.x - x_cut) <= 6.0f) {
                eq_detail_begin_undo_drag(state);
                curve->selected_band = -1;
                curve->selected_handle = EQ_CURVE_HANDLE_CUT_HIGH;
                state->effects_panel.eq_detail.dragging = true;
                state->effects_panel.eq_detail.last_mouse = pt;
                state->effects_panel.eq_detail.pending_apply = true;
                return true;
            }
        }
        for (int b = 0; b < 4; ++b) {
            if (!curve->bands[b].enabled) {
                continue;
            }
            float x_center = effects_eq_freq_to_x(&graph, curve->bands[b].freq_hz);
            float y_center = effects_eq_db_to_y(&graph, curve->bands[b].gain_db);
            SDL_Rect point_rect = {
                (int)lroundf(x_center) - 5,
                (int)lroundf(y_center) - 5,
                10,
                10
            };
            if (SDL_PointInRect(&pt, &point_rect)) {
                eq_detail_begin_undo_drag(state);
                curve->selected_band = b;
                curve->selected_handle = EQ_CURVE_HANDLE_POINT;
                state->effects_panel.eq_detail.dragging = true;
                state->effects_panel.eq_detail.last_mouse = pt;
                state->effects_panel.eq_detail.pending_apply = true;
                return true;
            }
            float width = curve->bands[b].q_width;
            if (width < 0.1f) {
                width = 0.1f;
            }
            float left_freq = curve->bands[b].freq_hz * powf(2.0f, -width * 0.5f);
            float right_freq = curve->bands[b].freq_hz * powf(2.0f, width * 0.5f);
            float x_left = effects_eq_freq_to_x(&graph, left_freq);
            float x_right = effects_eq_freq_to_x(&graph, right_freq);
            SDL_Rect left_rect = {
                (int)lroundf(x_left) - 4,
                (int)lroundf(y_center) - 6,
                8,
                12
            };
            SDL_Rect right_rect = {
                (int)lroundf(x_right) - 4,
                (int)lroundf(y_center) - 6,
                8,
                12
            };
            if (SDL_PointInRect(&pt, &left_rect) || SDL_PointInRect(&pt, &right_rect)) {
                eq_detail_begin_undo_drag(state);
                curve->selected_band = b;
                curve->selected_handle = EQ_CURVE_HANDLE_WIDTH;
                state->effects_panel.eq_detail.dragging = true;
                state->effects_panel.eq_detail.last_mouse = pt;
                state->effects_panel.eq_detail.pending_apply = true;
                return true;
            }
        }
    }
    state->effects_panel.eq_detail.hovered = true;
    state->effects_panel.eq_detail.dragging = true;
    state->effects_panel.eq_detail.last_mouse = pt;
    return true;
}

bool effects_panel_eq_detail_handle_mouse_up(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    if (!state->effects_panel.eq_detail.dragging) {
        return false;
    }
    state->effects_panel.eq_detail.dragging = false;
    state->effects_panel.eq_curve.selected_band = -1;
    state->effects_panel.eq_curve.selected_handle = EQ_CURVE_HANDLE_NONE;
    if (state->undo.active_drag_valid) {
        UndoCommand* cmd = &state->undo.active_drag;
        if (cmd->type == UNDO_CMD_EQ_CURVE) {
            eq_curve_to_session(&state->effects_panel.eq_curve, &cmd->data.eq_curve_edit.after);
            if (!session_eq_equal(&cmd->data.eq_curve_edit.before, &cmd->data.eq_curve_edit.after)) {
                undo_manager_commit_drag(&state->undo, cmd);
            } else {
                undo_manager_cancel_drag(&state->undo);
            }
        } else {
            undo_manager_cancel_drag(&state->undo);
        }
    }
    if (state->effects_panel.eq_detail.pending_apply) {
        eq_detail_apply_curve(state);
        state->effects_panel.eq_detail.last_apply_ticks = event->button.timestamp;
        state->effects_panel.eq_detail.pending_apply = false;
    }
    return true;
}

bool effects_panel_eq_detail_handle_mouse_motion(AppState* state,
                                                 const EffectsPanelLayout* layout,
                                                 const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    SDL_Point pt = {event->motion.x, event->motion.y};
    state->effects_panel.eq_detail.hovered = eq_detail_hit(layout, &pt);
    SDL_Rect graph = effects_panel_eq_detail_compute_graph_rect(&layout->detail_rect);
    update_hover_state(state, &layout->detail_rect, &graph, &pt);
    if (!state->effects_panel.eq_detail.dragging) {
        return state->effects_panel.eq_detail.hovered;
    }
    state->effects_panel.eq_detail.pending_apply = true;
    EqCurveState* curve = &state->effects_panel.eq_curve;
    if (curve->selected_band >= 0 && curve->selected_band < 4 &&
        curve->selected_handle != EQ_CURVE_HANDLE_NONE &&
        graph.w > 0 && graph.h > 0) {
        EqCurveBand* band = &curve->bands[curve->selected_band];
        float freq = effects_eq_x_to_freq(&graph, (float)pt.x);
        if (freq < EQ_DETAIL_MIN_HZ) freq = EQ_DETAIL_MIN_HZ;
        if (freq > EQ_DETAIL_MAX_HZ) freq = EQ_DETAIL_MAX_HZ;
        if (curve->selected_handle == EQ_CURVE_HANDLE_POINT) {
            float gain = effects_eq_y_to_db(&graph, (float)pt.y);
            if (gain < EQ_DETAIL_DB_MIN) gain = EQ_DETAIL_DB_MIN;
            if (gain > EQ_DETAIL_DB_MAX) gain = EQ_DETAIL_DB_MAX;
            band->freq_hz = freq;
            band->gain_db = gain;
        } else if (curve->selected_handle == EQ_CURVE_HANDLE_WIDTH) {
            set_band_width_from_freq(band, freq);
        }
    } else if (curve->selected_handle == EQ_CURVE_HANDLE_CUT_LOW && graph.w > 0 && graph.h > 0) {
        float freq = effects_eq_x_to_freq(&graph, (float)pt.x);
        float max_freq = EQ_DETAIL_MAX_HZ;
        if (curve->high_cut.enabled) {
            max_freq = curve->high_cut.freq_hz * 0.98f;
        }
        if (freq < EQ_DETAIL_MIN_HZ) freq = EQ_DETAIL_MIN_HZ;
        if (freq > max_freq) freq = max_freq;
        curve->low_cut.freq_hz = freq;
    } else if (curve->selected_handle == EQ_CURVE_HANDLE_CUT_HIGH && graph.w > 0 && graph.h > 0) {
        float freq = effects_eq_x_to_freq(&graph, (float)pt.x);
        float min_freq = EQ_DETAIL_MIN_HZ;
        if (curve->low_cut.enabled) {
            min_freq = curve->low_cut.freq_hz * 1.02f;
        }
        if (freq < min_freq) freq = min_freq;
        if (freq > EQ_DETAIL_MAX_HZ) freq = EQ_DETAIL_MAX_HZ;
        curve->high_cut.freq_hz = freq;
    }
    state->effects_panel.eq_detail.last_mouse = pt;
    eq_detail_apply_curve_throttled(state, event->motion.timestamp);
    return true;
}
