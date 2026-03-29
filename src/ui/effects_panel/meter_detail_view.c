#include "ui/effects_panel_meter_detail.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel_meter_views.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int meter_detail_pad(void) {
    int needed = ui_font_line_height(1.0f) / 2 + 8;
    return max_int(12, needed);
}

static int meter_detail_gap(void) {
    return max_int(12, ui_font_line_height(1.0f) / 2 + 6);
}

static int meter_detail_button_height(void) {
    return max_int(14, ui_font_line_height(1.0f) + 4);
}

static int meter_detail_button_gap(void) {
    int h = meter_detail_button_height();
    return max_int(6, h / 3);
}

static int meter_detail_button_width(const char* label, int min_w) {
    int needed = ui_measure_text_width(label ? label : "", 1.0f) + 12;
    if (needed < min_w) {
        needed = min_w;
    }
    return needed;
}

static void meter_detail_compute_split_rects(const SDL_Rect* detail_rect,
                                             SDL_Rect* out_left_rect,
                                             SDL_Rect* out_right_rect) {
    if (!detail_rect || !out_left_rect || !out_right_rect || detail_rect->w <= 0 || detail_rect->h <= 0) {
        if (out_left_rect) *out_left_rect = (SDL_Rect){0, 0, 0, 0};
        if (out_right_rect) *out_right_rect = (SDL_Rect){0, 0, 0, 0};
        return;
    }
    int pad = meter_detail_pad();
    int gap = meter_detail_gap();
    int min_left_w = max_int(140, ui_measure_text_width("Meter Detail", 1.3f) + 24);
    int left_w = (int)lroundf(detail_rect->w * 0.20f);
    if (left_w < min_left_w) {
        left_w = min_left_w;
    }
    if (left_w > detail_rect->w - 120) {
        left_w = detail_rect->w - 120;
    }

    SDL_Rect left_rect = {detail_rect->x + pad, detail_rect->y + pad, left_w - pad, detail_rect->h - pad * 2};
    SDL_Rect right_rect = {left_rect.x + left_rect.w + gap,
                           detail_rect->y + pad,
                           detail_rect->w - (left_rect.w + gap + pad * 2),
                           detail_rect->h - pad * 2};
    if (left_rect.w < 0) left_rect.w = 0;
    if (left_rect.h < 0) left_rect.h = 0;
    if (right_rect.w < 0) right_rect.w = 0;
    if (right_rect.h < 0) right_rect.h = 0;
    *out_left_rect = left_rect;
    *out_right_rect = right_rect;
}

static void resolve_meter_detail_theme(SDL_Color* border,
                                       SDL_Color* fill,
                                       SDL_Color* left_fill,
                                       SDL_Color* right_fill,
                                       SDL_Color* label_color,
                                       SDL_Color* dim_color,
                                       SDL_Color* btn_on,
                                       SDL_Color* btn_off) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (border) *border = theme.pane_border;
        if (fill) *fill = theme.inspector_fill;
        if (left_fill) *left_fill = theme.timeline_fill;
        if (right_fill) *right_fill = theme.inspector_fill;
        if (label_color) *label_color = theme.text_primary;
        if (dim_color) *dim_color = theme.text_muted;
        if (btn_on) *btn_on = theme.control_active_fill;
        if (btn_off) *btn_off = theme.control_fill;
        return;
    }
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (fill) *fill = (SDL_Color){24, 26, 32, 255};
    if (left_fill) *left_fill = (SDL_Color){26, 28, 36, 255};
    if (right_fill) *right_fill = (SDL_Color){22, 24, 30, 255};
    if (label_color) *label_color = (SDL_Color){210, 210, 220, 255};
    if (dim_color) *dim_color = (SDL_Color){150, 160, 180, 255};
    if (btn_on) *btn_on = (SDL_Color){90, 120, 170, 220};
    if (btn_off) *btn_off = (SDL_Color){40, 44, 54, 220};
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float linear_to_db(float linear) {
    if (linear <= 1e-9f) {
        return -90.0f;
    }
    return 20.0f * log10f(linear);
}

static const FxTypeUIInfo* find_type_info(const EffectsPanelState* panel, FxTypeId type_id) {
    if (!panel) {
        return NULL;
    }
    for (int i = 0; i < panel->type_count; ++i) {
        if (panel->types[i].type_id == type_id) {
            return &panel->types[i];
        }
    }
    return NULL;
}

static void draw_info_row(SDL_Renderer* renderer,
                          int x,
                          int y,
                          int max_w,
                          const char* label,
                          const char* value,
                          SDL_Color label_color,
                          SDL_Color value_color) {
    int label_w = ui_measure_text_width(label ? label : "", 1.0f);
    int safe_w = max_w > 0 ? max_w : label_w + 4;
    ui_draw_text_clipped(renderer, x, y, label, label_color, 1.0f, safe_w);
    if (value) {
        int value_x = x + label_w + 8;
        int value_w = max_w - (value_x - x);
        if (value_w > 0) {
            ui_draw_text_clipped(renderer, value_x, y, value, value_color, 1.0f, value_w);
        }
    }
}

static bool panel_targets_track(const EffectsPanelState* panel) {
    return panel && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0;
}

static void draw_centered_toggle_text(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const char* label,
                                      SDL_Color color) {
    if (!renderer || !rect || !label || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    int text_h = ui_font_line_height(1.0f);
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text_clipped(renderer, rect->x + 2, text_y, label, color, 1.0f, rect->w - 4);
}

void effects_panel_meter_detail_compute_toggle_rects(const SDL_Rect* detail_rect,
                                                     SDL_Rect* out_mid_side,
                                                     SDL_Rect* out_left_right) {
    if (out_mid_side) {
        *out_mid_side = (SDL_Rect){0, 0, 0, 0};
    }
    if (out_left_right) {
        *out_left_right = (SDL_Rect){0, 0, 0, 0};
    }
    if (!detail_rect || detail_rect->w <= 0 || detail_rect->h <= 0) {
        return;
    }
    SDL_Rect left_rect = {0, 0, 0, 0};
    SDL_Rect right_rect = {0, 0, 0, 0};
    meter_detail_compute_split_rects(detail_rect, &left_rect, &right_rect);

    int btn_w = meter_detail_button_width("MS", 36);
    int btn_h = meter_detail_button_height();
    int btn_gap = meter_detail_button_gap();
    int btn_y = right_rect.y + max_int(6, btn_h / 3);
    int right_edge = right_rect.x + right_rect.w;
    if (out_left_right) {
        *out_left_right = (SDL_Rect){right_edge - btn_w, btn_y, btn_w, btn_h};
    }
    if (out_mid_side) {
        *out_mid_side = (SDL_Rect){right_edge - btn_w * 2 - btn_gap, btn_y, btn_w, btn_h};
    }
}

// Computes toggle rectangles for LUFS mode buttons.
void effects_panel_meter_detail_compute_lufs_toggle_rects(const SDL_Rect* detail_rect,
                                                          SDL_Rect* out_integrated,
                                                          SDL_Rect* out_short_term,
                                                          SDL_Rect* out_momentary) {
    if (out_integrated) {
        *out_integrated = (SDL_Rect){0, 0, 0, 0};
    }
    if (out_short_term) {
        *out_short_term = (SDL_Rect){0, 0, 0, 0};
    }
    if (out_momentary) {
        *out_momentary = (SDL_Rect){0, 0, 0, 0};
    }
    if (!detail_rect || detail_rect->w <= 0 || detail_rect->h <= 0) {
        return;
    }
    SDL_Rect left_rect = {0, 0, 0, 0};
    SDL_Rect right_rect = {0, 0, 0, 0};
    meter_detail_compute_split_rects(detail_rect, &left_rect, &right_rect);

    int btn_w = meter_detail_button_width("INT", 46);
    int btn_h = meter_detail_button_height();
    int btn_gap = meter_detail_button_gap();
    int btn_y = right_rect.y + right_rect.h - btn_h - max_int(8, btn_h / 2);
    int right_edge = right_rect.x + right_rect.w;
    if (out_momentary) {
        *out_momentary = (SDL_Rect){right_edge - btn_w, btn_y, btn_w, btn_h};
    }
    if (out_short_term) {
        *out_short_term = (SDL_Rect){right_edge - btn_w * 2 - btn_gap, btn_y, btn_w, btn_h};
    }
    if (out_integrated) {
        *out_integrated = (SDL_Rect){right_edge - btn_w * 3 - btn_gap * 2, btn_y, btn_w, btn_h};
    }
}

// Computes toggle rectangles for spectrogram palette buttons.
void effects_panel_meter_detail_compute_spectrogram_toggle_rects(const SDL_Rect* detail_rect,
                                                                 SDL_Rect* out_white_black,
                                                                 SDL_Rect* out_black_white,
                                                                 SDL_Rect* out_heat) {
    if (out_white_black) {
        *out_white_black = (SDL_Rect){0, 0, 0, 0};
    }
    if (out_black_white) {
        *out_black_white = (SDL_Rect){0, 0, 0, 0};
    }
    if (out_heat) {
        *out_heat = (SDL_Rect){0, 0, 0, 0};
    }
    if (!detail_rect || detail_rect->w <= 0 || detail_rect->h <= 0) {
        return;
    }
    SDL_Rect left_rect = {0, 0, 0, 0};
    SDL_Rect right_rect = {0, 0, 0, 0};
    meter_detail_compute_split_rects(detail_rect, &left_rect, &right_rect);

    int btn_w = meter_detail_button_width("Heat", 44);
    int btn_h = meter_detail_button_height();
    int btn_gap = meter_detail_button_gap();
    int btn_y = right_rect.y + max_int(6, btn_h / 3);
    int total_w = btn_w * 3 + btn_gap * 2;
    int btn_x = right_rect.x + right_rect.w - total_w - 6;
    if (out_white_black) {
        *out_white_black = (SDL_Rect){btn_x, btn_y, btn_w, btn_h};
    }
    btn_x += btn_w + btn_gap;
    if (out_black_white) {
        *out_black_white = (SDL_Rect){btn_x, btn_y, btn_w, btn_h};
    }
    btn_x += btn_w + btn_gap;
    if (out_heat) {
        *out_heat = (SDL_Rect){btn_x, btn_y, btn_w, btn_h};
    }
}

static void meter_history_reset(EffectsMeterHistory* history, FxInstId id, FxTypeId type) {
    if (!history) {
        return;
    }
    SDL_zero(*history);
    history->active_id = id;
    history->active_type = type;
}

static void meter_history_push(float* values, int capacity, int* head, int* count, float value) {
    if (!values || !head || !count) {
        return;
    }
    values[*head] = value;
    *head = (*head + 1) % capacity;
    if (*count < capacity) {
        *count += 1;
    }
}

static void meter_history_push_pair(float* a_values,
                                    float* b_values,
                                    int capacity,
                                    int* head,
                                    int* count,
                                    float a_value,
                                    float b_value) {
    if (!a_values || !b_values || !head || !count) {
        return;
    }
    a_values[*head] = a_value;
    b_values[*head] = b_value;
    *head = (*head + 1) % capacity;
    if (*count < capacity) {
        *count += 1;
    }
}

// Pushes a triple of history values into a rolling buffer.
static void meter_history_push_triple(float* a_values,
                                      float* b_values,
                                      float* c_values,
                                      int capacity,
                                      int* head,
                                      int* count,
                                      float a_value,
                                      float b_value,
                                      float c_value) {
    if (!a_values || !b_values || !c_values || !head || !count) {
        return;
    }
    a_values[*head] = a_value;
    b_values[*head] = b_value;
    c_values[*head] = c_value;
    *head = (*head + 1) % capacity;
    if (*count < capacity) {
        *count += 1;
    }
}

static void meter_history_update(EffectsMeterHistory* history,
                                 FxInstId id,
                                 FxTypeId type,
                                 const EngineFxMeterSnapshot* snapshot) {
    if (!history || !snapshot || !snapshot->valid) {
        return;
    }
    if (history->active_id != id || history->active_type != type) {
        meter_history_reset(history, id, type);
    }
    Uint32 now = SDL_GetTicks();
    Uint32 sample_interval_ms = 20;
    if (type == 101u) {
        sample_interval_ms = 8;
    } else if (type == 102u) {
        sample_interval_ms = 4;
    } else if (type == 104u) {
        sample_interval_ms = 100;
    }
    if (history->last_sample_ticks != 0 && now - history->last_sample_ticks < sample_interval_ms) {
        return;
    }
    history->last_sample_ticks = now;
    meter_history_push(history->corr_values,
                       FX_METER_CORR_HISTORY_POINTS,
                       &history->corr_head,
                       &history->corr_count,
                       snapshot->corr);
    if (type == 101u) {
        meter_history_push_pair(history->mid_values,
                                history->side_values,
                                FX_METER_MID_SIDE_HISTORY_POINTS,
                                &history->mid_head,
                                &history->mid_count,
                                snapshot->mid_rms,
                                snapshot->side_rms);
    }
    if (type == 103u) {
        meter_history_push_pair(history->peak_values,
                                history->rms_values,
                                FX_METER_LEVEL_HISTORY_POINTS,
                                &history->level_head,
                                &history->level_count,
                                snapshot->peak,
                                snapshot->rms);
    }
    if (type == 104u) {
        meter_history_push_triple(history->lufs_i_values,
                                  history->lufs_s_values,
                                  history->lufs_m_values,
                                  FX_METER_LUFS_HISTORY_POINTS,
                                  &history->lufs_head,
                                  &history->lufs_count,
                                  snapshot->lufs_integrated,
                                  snapshot->lufs_short_term,
                                  snapshot->lufs_momentary);
    }
    if (type == 102u) {
        if (snapshot->vec_point_count > 0) {
            for (int i = 0; i < snapshot->vec_point_count; ++i) {
                meter_history_push(history->vec_x,
                                   FX_METER_VECTOR_HISTORY_POINTS,
                                   &history->vec_head,
                                   &history->vec_count,
                                   snapshot->vec_points_x[i]);
                meter_history_push(history->vec_y,
                                   FX_METER_VECTOR_HISTORY_POINTS,
                                   &history->vec_head,
                                   &history->vec_count,
                                   snapshot->vec_points_y[i]);
            }
        } else {
            meter_history_push(history->vec_x,
                               FX_METER_VECTOR_HISTORY_POINTS,
                               &history->vec_head,
                               &history->vec_count,
                               snapshot->vec_x);
            meter_history_push(history->vec_y,
                               FX_METER_VECTOR_HISTORY_POINTS,
                               &history->vec_head,
                               &history->vec_count,
                               snapshot->vec_y);
        }
    }
}

// effects_panel_meter_detail_render draws a two-column meter detail view.
void effects_panel_meter_detail_render(SDL_Renderer* renderer,
                                       const AppState* state,
                                       const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Rect rect = layout->detail_rect;
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    SDL_Color border = {0};
    SDL_Color fill = {0};
    SDL_Color left_fill = {0};
    SDL_Color right_fill = {0};
    SDL_Color label_color = {0};
    SDL_Color dim_color = {0};
    SDL_Color btn_on = {0};
    SDL_Color btn_off = {0};
    resolve_meter_detail_theme(&border, &fill, &left_fill, &right_fill, &label_color, &dim_color, &btn_on, &btn_off);

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);

    SDL_Rect left_rect = {0, 0, 0, 0};
    SDL_Rect right_rect = {0, 0, 0, 0};
    meter_detail_compute_split_rects(&rect, &left_rect, &right_rect);

    SDL_SetRenderDrawColor(renderer, left_fill.r, left_fill.g, left_fill.b, left_fill.a);
    SDL_RenderFillRect(renderer, &left_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &left_rect);

    SDL_SetRenderDrawColor(renderer, right_fill.r, right_fill.g, right_fill.b, right_fill.a);
    SDL_RenderFillRect(renderer, &right_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &right_rect);

    int open_index = panel->list_open_slot_index;
    const FxSlotUIState* slot = NULL;
    if (open_index >= 0 && open_index < panel->chain_count) {
        slot = &panel->chain[open_index];
    }

    FxTypeId type_id = slot ? slot->type_id : 0;
    const FxTypeUIInfo* type_info = find_type_info(panel, type_id);

    bool show_ms_toggle = false;
    SDL_Rect toggle_ms = {0, 0, 0, 0};
    SDL_Rect toggle_lr = {0, 0, 0, 0};
    if (type_id == 102u) {
        effects_panel_meter_detail_compute_toggle_rects(&rect, &toggle_ms, &toggle_lr);
        show_ms_toggle = (toggle_ms.w > 0 && toggle_lr.w > 0);
    }

    bool show_lufs_toggle = false;
    SDL_Rect toggle_int = {0, 0, 0, 0};
    SDL_Rect toggle_short = {0, 0, 0, 0};
    SDL_Rect toggle_momentary = {0, 0, 0, 0};
    if (type_id == 104u) {
        effects_panel_meter_detail_compute_lufs_toggle_rects(&rect, &toggle_int, &toggle_short, &toggle_momentary);
        show_lufs_toggle = (toggle_int.w > 0 && toggle_short.w > 0 && toggle_momentary.w > 0);
    }

    bool show_spectrogram_toggle = false;
    SDL_Rect toggle_wb = {0, 0, 0, 0};
    SDL_Rect toggle_bw = {0, 0, 0, 0};
    SDL_Rect toggle_heat = {0, 0, 0, 0};
    if (type_id == 105u) {
        effects_panel_meter_detail_compute_spectrogram_toggle_rects(&rect, &toggle_wb, &toggle_bw, &toggle_heat);
        show_spectrogram_toggle = (toggle_wb.w > 0 && toggle_bw.w > 0 && toggle_heat.w > 0);
    }

    EngineFxMeterSnapshot snapshot = {0};
    bool have_snapshot = false;
    if (slot && state->engine) {
        if (panel_targets_track(panel)) {
            int track_index = panel->target_track_index;
            if (track_index < 0) {
                track_index = state->selected_track_index;
            }
            have_snapshot = engine_get_track_fx_meter_snapshot(state->engine,
                                                               track_index,
                                                               slot->id,
                                                               &snapshot);
        } else {
            have_snapshot = engine_get_master_fx_meter_snapshot(state->engine,
                                                                slot->id,
                                                                &snapshot);
        }
    }
    if (state->engine) {
        if (slot) {
            bool is_master = !panel_targets_track(panel);
            int track_index = is_master ? -1 : panel->target_track_index;
            if (!is_master && track_index < 0) {
                track_index = state->selected_track_index;
            }
            engine_set_active_fx_meter(state->engine, is_master, track_index, slot->id);
            if (type_id == 105u) {
                int target = is_master ? -1 : track_index;
                engine_set_fx_spectrogram_target(state->engine, target, slot->id, true);
            } else {
                engine_set_fx_spectrogram_target(state->engine, -1, 0, false);
            }
        } else {
            engine_set_active_fx_meter(state->engine, true, -1, 0);
            engine_set_fx_spectrogram_target(state->engine, -1, 0, false);
        }
    }
    meter_history_update(&((AppState*)state)->effects_panel.meter_history,
                         slot ? slot->id : 0,
                         slot ? slot->type_id : 0,
                         have_snapshot ? &snapshot : NULL);

    int text_x = left_rect.x + 10;
    int text_y = left_rect.y + max_int(8, ui_font_line_height(1.0f) / 2);
    int left_text_w = left_rect.w - 20;
    ui_draw_text_clipped(renderer, text_x, text_y, "Meter Detail", label_color, 1.3f, left_text_w);
    text_y += ui_font_line_height(1.3f) + 8;

    char value_buf[64];
    draw_info_row(renderer, text_x, text_y, left_text_w, "Type", type_info ? type_info->name : "Meter", label_color, dim_color);
    text_y += ui_font_line_height(1.0f) + 4;

    snprintf(value_buf, sizeof(value_buf), "%u", slot ? (unsigned)slot->id : 0u);
    draw_info_row(renderer, text_x, text_y, left_text_w, "Instance", value_buf, label_color, dim_color);
    text_y += ui_font_line_height(1.0f) + 4;

    const char* target_label = panel_targets_track(panel) ? "Track" : "Master";
    draw_info_row(renderer, text_x, text_y, left_text_w, "Target", target_label, label_color, dim_color);
    text_y += ui_font_line_height(1.0f) + 4;

    if (have_snapshot) {
        snprintf(value_buf, sizeof(value_buf), "%.1f dB", linear_to_db(clampf(snapshot.peak, 0.0f, 1.5f)));
        draw_info_row(renderer, text_x, text_y, left_text_w, "Peak", value_buf, label_color, dim_color);
        text_y += ui_font_line_height(1.0f) + 4;
        snprintf(value_buf, sizeof(value_buf), "%.1f dB", linear_to_db(clampf(snapshot.rms, 0.0f, 1.5f)));
        draw_info_row(renderer, text_x, text_y, left_text_w, "RMS", value_buf, label_color, dim_color);
        text_y += ui_font_line_height(1.0f) + 4;
        if (type_id == 104u) {
            snprintf(value_buf, sizeof(value_buf), "%.1f LUFS", snapshot.lufs_integrated);
            draw_info_row(renderer, text_x, text_y, left_text_w, "LUFS I", value_buf, label_color, dim_color);
            text_y += ui_font_line_height(1.0f) + 4;
            snprintf(value_buf, sizeof(value_buf), "%.1f LUFS", snapshot.lufs_short_term);
            draw_info_row(renderer, text_x, text_y, left_text_w, "LUFS S", value_buf, label_color, dim_color);
            text_y += ui_font_line_height(1.0f) + 4;
            snprintf(value_buf, sizeof(value_buf), "%.1f LUFS", snapshot.lufs_momentary);
            draw_info_row(renderer, text_x, text_y, left_text_w, "LUFS M", value_buf, label_color, dim_color);
            text_y += ui_font_line_height(1.0f) + 4;
        }
        snprintf(value_buf, sizeof(value_buf), "%.2f", snapshot.corr);
        draw_info_row(renderer, text_x, text_y, left_text_w, "Corr", value_buf, label_color, dim_color);
        text_y += ui_font_line_height(1.0f) + 4;
        snprintf(value_buf, sizeof(value_buf), "%.1f dB", linear_to_db(snapshot.mid_rms));
        draw_info_row(renderer, text_x, text_y, left_text_w, "Mid", value_buf, label_color, dim_color);
        text_y += ui_font_line_height(1.0f) + 4;
        snprintf(value_buf, sizeof(value_buf), "%.1f dB", linear_to_db(snapshot.side_rms));
        draw_info_row(renderer, text_x, text_y, left_text_w, "Side", value_buf, label_color, dim_color);
        text_y += ui_font_line_height(1.0f) + 4;
        snprintf(value_buf, sizeof(value_buf), "%.2f, %.2f", snapshot.vec_x, snapshot.vec_y);
        draw_info_row(renderer, text_x, text_y, left_text_w, "Vector", value_buf, label_color, dim_color);
    } else {
        draw_info_row(renderer, text_x, text_y, left_text_w, "Status", "Waiting", label_color, dim_color);
    }

    if (!slot) {
        ui_draw_text_clipped(renderer,
                             right_rect.x + 12,
                             right_rect.y + 12,
                             "No meter selected.",
                             dim_color,
                             1.1f,
                             right_rect.w - 24);
        return;
    }

    SDL_Rect meter_rect = right_rect;

    if (type_id == 100u) {
        effects_meter_render_correlation(renderer,
                                         &meter_rect,
                                         have_snapshot ? &snapshot : NULL,
                                         &panel->meter_history,
                                         label_color,
                                         dim_color);
    } else if (type_id == 103u) {
        effects_meter_render_levels(renderer,
                                    &meter_rect,
                                    have_snapshot ? &snapshot : NULL,
                                    &panel->meter_history,
                                    label_color,
                                    dim_color);
    } else if (type_id == 104u) {
        effects_meter_render_lufs(renderer,
                                  &meter_rect,
                                  have_snapshot ? &snapshot : NULL,
                                  &panel->meter_history,
                                  panel->meter_lufs_mode,
                                  label_color,
                                  dim_color);
    } else if (type_id == 101u) {
        effects_meter_render_mid_side(renderer,
                                      &meter_rect,
                                      have_snapshot ? &snapshot : NULL,
                                      &panel->meter_history,
                                      label_color,
                                      dim_color);
    } else if (type_id == 102u) {
        effects_meter_render_vectorscope(renderer,
                                         &meter_rect,
                                         have_snapshot ? &snapshot : NULL,
                                         &panel->meter_history,
                                         panel->meter_scope_mode,
                                         label_color,
                                         dim_color);
    } else if (type_id == 105u) {
        EngineSpectrogramSnapshot spectrogram = {0};
        float frames[ENGINE_SPECTROGRAM_HISTORY * ENGINE_SPECTROGRAM_BINS];
        bool have_spectrogram = false;
        if (state->engine) {
            have_spectrogram = engine_get_fx_spectrogram_snapshot(state->engine,
                                                                  &spectrogram,
                                                                  frames,
                                                                  ENGINE_SPECTROGRAM_HISTORY,
                                                                  ENGINE_SPECTROGRAM_BINS);
        }
        effects_meter_render_spectrogram(renderer,
                                         &meter_rect,
                                         have_spectrogram ? &spectrogram : NULL,
                                         have_spectrogram ? frames : NULL,
                                         panel->meter_spectrogram_mode,
                                         label_color,
                                         dim_color);
    } else {
        ui_draw_text_clipped(renderer,
                             right_rect.x + 12,
                             right_rect.y + 12,
                             "Unsupported meter view.",
                             dim_color,
                             1.1f,
                             right_rect.w - 24);
    }

    if (show_ms_toggle) {
        bool ms_active = panel->meter_scope_mode == FX_METER_SCOPE_MID_SIDE;
        SDL_Color ms_fill = ms_active ? btn_on : btn_off;
        SDL_Color lr_fill = ms_active ? btn_off : btn_on;

        SDL_SetRenderDrawColor(renderer, ms_fill.r, ms_fill.g, ms_fill.b, ms_fill.a);
        SDL_RenderFillRect(renderer, &toggle_ms);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_ms);

        SDL_SetRenderDrawColor(renderer, lr_fill.r, lr_fill.g, lr_fill.b, lr_fill.a);
        SDL_RenderFillRect(renderer, &toggle_lr);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_lr);

        draw_centered_toggle_text(renderer, &toggle_ms, "MS", label_color);
        draw_centered_toggle_text(renderer, &toggle_lr, "LR", label_color);
    }

    if (show_lufs_toggle) {
        bool int_active = panel->meter_lufs_mode == FX_METER_LUFS_INTEGRATED;
        bool short_active = panel->meter_lufs_mode == FX_METER_LUFS_SHORT_TERM;
        SDL_Color int_fill = int_active ? btn_on : btn_off;
        SDL_Color short_fill = short_active ? btn_on : btn_off;
        SDL_Color momentary_fill = (!int_active && !short_active) ? btn_on : btn_off;

        SDL_SetRenderDrawColor(renderer, int_fill.r, int_fill.g, int_fill.b, int_fill.a);
        SDL_RenderFillRect(renderer, &toggle_int);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_int);

        SDL_SetRenderDrawColor(renderer, short_fill.r, short_fill.g, short_fill.b, short_fill.a);
        SDL_RenderFillRect(renderer, &toggle_short);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_short);

        SDL_SetRenderDrawColor(renderer, momentary_fill.r, momentary_fill.g, momentary_fill.b, momentary_fill.a);
        SDL_RenderFillRect(renderer, &toggle_momentary);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_momentary);

        draw_centered_toggle_text(renderer, &toggle_int, "INT", label_color);
        draw_centered_toggle_text(renderer, &toggle_short, "ST", label_color);
        draw_centered_toggle_text(renderer, &toggle_momentary, "M", label_color);
    }

    if (show_spectrogram_toggle) {
        bool wb_active = panel->meter_spectrogram_mode == FX_METER_SPECTROGRAM_WHITE_BLACK;
        bool bw_active = panel->meter_spectrogram_mode == FX_METER_SPECTROGRAM_BLACK_WHITE;
        SDL_Color wb_fill = wb_active ? btn_on : btn_off;
        SDL_Color bw_fill = bw_active ? btn_on : btn_off;
        SDL_Color heat_fill = (!wb_active && !bw_active) ? btn_on : btn_off;

        SDL_SetRenderDrawColor(renderer, wb_fill.r, wb_fill.g, wb_fill.b, wb_fill.a);
        SDL_RenderFillRect(renderer, &toggle_wb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_wb);

        SDL_SetRenderDrawColor(renderer, bw_fill.r, bw_fill.g, bw_fill.b, bw_fill.a);
        SDL_RenderFillRect(renderer, &toggle_bw);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_bw);

        SDL_SetRenderDrawColor(renderer, heat_fill.r, heat_fill.g, heat_fill.b, heat_fill.a);
        SDL_RenderFillRect(renderer, &toggle_heat);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &toggle_heat);

        draw_centered_toggle_text(renderer, &toggle_wb, "W/B", label_color);
        draw_centered_toggle_text(renderer, &toggle_bw, "B/W", label_color);
        draw_centered_toggle_text(renderer, &toggle_heat, "Heat", label_color);
    }
}
