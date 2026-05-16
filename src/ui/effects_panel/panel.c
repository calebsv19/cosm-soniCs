#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/effects_panel_overlay.h"
#include "ui/effects_panel_state_helpers.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/layout.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FX_PANEL_SLIDER_HEIGHT 18
static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int fx_panel_header_height(void) {
    int title_h = ui_font_line_height(FX_PANEL_TITLE_SCALE);
    int button_h = ui_font_line_height(FX_PANEL_BUTTON_SCALE) + FX_PANEL_HEADER_BUTTON_PAD_Y * 2;
    int needed = max_int(title_h + 8, button_h + 6);
    return max_int(FX_PANEL_HEADER_HEIGHT, needed);
}

static int fx_panel_button_height(void) {
    int needed = ui_font_line_height(FX_PANEL_BUTTON_SCALE) + FX_PANEL_HEADER_BUTTON_PAD_Y * 2;
    return max_int(FX_PANEL_HEADER_BUTTON_HEIGHT, needed);
}

static int fx_panel_button_width(const char* label, float scale) {
    int width = ui_measure_text_width(label ? label : "", scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    int min_width = ui_measure_text_width("W", scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2 + 2;
    return max_int(width, min_width);
}

static int fx_panel_list_row_height(void) {
    int needed = ui_font_line_height(FX_PANEL_LIST_TEXT_SCALE) + 6;
    return max_int(FX_PANEL_LIST_ROW_HEIGHT, needed);
}

static int fx_panel_list_row_gap(void) {
    int row_h = fx_panel_list_row_height();
    return max_int(FX_PANEL_LIST_ROW_GAP, row_h / 6);
}

static void resolve_effects_panel_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .timeline_fill = {28, 30, 38, 255},
            .inspector_fill = {26, 26, 32, 240},
            .pane_border = {90, 95, 110, 255},
            .control_fill = {48, 52, 62, 255},
            .control_hover_fill = {70, 90, 120, 255},
            .control_active_fill = {120, 160, 220, 255},
            .control_border = {90, 95, 110, 255},
            .text_primary = {220, 220, 230, 255},
            .text_muted = {160, 170, 190, 255},
            .selection_fill = {80, 110, 160, 255},
            .slider_track = {52, 56, 64, 220},
            .slider_handle = {120, 160, 220, 255}
        };
    }
}

static void zero_layout(EffectsPanelLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

static void compute_list_layout(EffectsPanelState* panel,
                                int content_x,
                                int content_y,
                                int content_w,
                                int content_h,
                                int header_h,
                                int list_row_h,
                                int list_row_gap,
                                EffectsPanelLayout* layout) {
    if (!panel || !layout) {
        return;
    }
    int body_y = content_y + header_h;
    int body_h = content_h - header_h;
    if (body_h <= 0) {
        return;
    }

    int list_w = (int)((float)content_w * 0.18f);
    if (list_w < 140) list_w = 140;
    if (list_w < 0) list_w = 0;
    int gap = 12;
    if (list_w + gap > content_w) {
        gap = 6;
    }

    int max_name_w = 0;
    for (int i = 0; i < panel->chain_count; ++i) {
        const FxSlotUIState* slot = &panel->chain[i];
        const char* name = "Effect";
        for (int t = 0; t < panel->type_count; ++t) {
            if (panel->types[t].type_id == slot->type_id) {
                name = panel->types[t].name;
                break;
            }
        }
        int w = ui_measure_text_width(name, 1.3f);
        if (w > max_name_w) {
            max_name_w = w;
        }
    }
    int toggle_size = list_row_h - 6;
    if (toggle_size < 10) toggle_size = 10;
    int min_needed = FX_PANEL_LIST_PAD * 3 + max_name_w + toggle_size + 16;
    if (min_needed > list_w) {
        list_w = min_needed;
    }
    if (list_w > content_w - 180) list_w = content_w - 180;
    if (list_w < 0) list_w = 0;

    int detail_w = content_w - list_w - gap;
    if (detail_w < 0) detail_w = 0;

    layout->list_rect = (SDL_Rect){content_x, body_y, list_w, body_h};
    layout->detail_rect = (SDL_Rect){content_x + list_w + gap, body_y, detail_w, body_h};

    layout->list_row_count = 0;
    int list_inner_x = layout->list_rect.x + FX_PANEL_LIST_PAD;
    int list_inner_w = layout->list_rect.w - FX_PANEL_LIST_PAD * 2;
    if (list_inner_w < 0) list_inner_w = 0;
    int row_y = layout->list_rect.y + FX_PANEL_LIST_PAD;

    int meter_column_w = FX_PANEL_SNAPSHOT_METER_WIDTH;
    int meter_gap = FX_PANEL_SNAPSHOT_METER_GAP;
    int min_left_w = 80;
    int left_inner_w = list_inner_w - meter_column_w - meter_gap;
    if (left_inner_w < min_left_w) {
        meter_column_w = list_inner_w / 4;
        if (meter_column_w < 0) meter_column_w = 0;
        left_inner_w = list_inner_w - meter_column_w - meter_gap;
        if (left_inner_w < min_left_w) {
            meter_column_w = 0;
            left_inner_w = list_inner_w;
        }
    }
    int left_inner_x = list_inner_x;
    int meter_column_x = left_inner_x + left_inner_w + meter_gap;

    int eq_h = FX_PANEL_SNAPSHOT_EQ_HEIGHT;
    int label_h = FX_PANEL_SNAPSHOT_LABEL_HEIGHT;
    int slider_h = FX_PANEL_SNAPSHOT_SLIDER_HEIGHT;
    int slider_hit_h = FX_PANEL_SNAPSHOT_SLIDER_HIT_HEIGHT;
    int footer_h = FX_PANEL_SNAPSHOT_FOOTER_HEIGHT;
    int footer_y = layout->list_rect.y + layout->list_rect.h - FX_PANEL_LIST_PAD - footer_h;
    int snapshot_y = row_y;
    layout->track_snapshot.container_rect = layout->list_rect;
    layout->track_snapshot.eq_rect = (SDL_Rect){list_inner_x, snapshot_y, list_inner_w, eq_h};
    snapshot_y += eq_h + (FX_PANEL_SNAPSHOT_GAP / 2);
    layout->track_snapshot.gain_label_rect = (SDL_Rect){left_inner_x, snapshot_y, left_inner_w, label_h};
    snapshot_y += label_h;
    layout->track_snapshot.gain_rect = (SDL_Rect){left_inner_x, snapshot_y, left_inner_w, slider_h};
    layout->track_snapshot.gain_hit_rect = (SDL_Rect){
        left_inner_x,
        snapshot_y - (slider_hit_h - slider_h) / 2,
        left_inner_w,
        slider_hit_h
    };
    snapshot_y += slider_h + FX_PANEL_SNAPSHOT_GAP;
    layout->track_snapshot.pan_label_rect = (SDL_Rect){left_inner_x, snapshot_y, left_inner_w, label_h};
    snapshot_y += label_h;
    layout->track_snapshot.pan_rect = (SDL_Rect){left_inner_x, snapshot_y, left_inner_w, slider_h};
    layout->track_snapshot.pan_hit_rect = (SDL_Rect){
        left_inner_x,
        snapshot_y - (slider_hit_h - slider_h) / 2,
        left_inner_w,
        slider_hit_h
    };
    snapshot_y += slider_h + FX_PANEL_SNAPSHOT_GAP;
    layout->track_snapshot.instrument_button_rect =
        (SDL_Rect){left_inner_x, snapshot_y, left_inner_w, footer_h};
    int menu_bottom = footer_y - FX_PANEL_LIST_PAD;
    midi_preset_browser_compute_layout(layout->track_snapshot.instrument_button_rect,
                                       menu_bottom,
                                       panel->track_snapshot.instrument_menu_scroll_row,
                                       (EngineInstrumentPresetCategoryId)panel->track_snapshot.instrument_menu_expanded_category,
                                       &layout->track_snapshot.instrument_browser);
    layout->track_snapshot.instrument_menu_rect = layout->track_snapshot.instrument_browser.menu_rect;

    int button_gap = FX_PANEL_SNAPSHOT_BUTTON_GAP;
    int button_w = (left_inner_w - button_gap) / 2;
    if (button_w < 0) button_w = 0;
    layout->track_snapshot.mute_rect = (SDL_Rect){left_inner_x, footer_y, button_w, footer_h};
    layout->track_snapshot.solo_rect = (SDL_Rect){left_inner_x + button_w + button_gap, footer_y, button_w, footer_h};

    int list_top = snapshot_y + footer_h + FX_PANEL_SNAPSHOT_LIST_GAP;
    int list_bottom = footer_y - FX_PANEL_LIST_PAD;
    if (list_bottom < list_top) {
        list_bottom = list_top;
    }
    layout->track_snapshot.list_rect = (SDL_Rect){
        left_inner_x,
        list_top,
        left_inner_w,
        list_bottom - list_top
    };
    layout->track_snapshot.list_clip_rect = layout->track_snapshot.list_rect;

    int total_rows = panel->chain_count;
    if (total_rows > FX_MASTER_MAX) {
        total_rows = FX_MASTER_MAX;
    }
    int row_full_h = list_row_h + list_row_gap;
    int list_content_h = total_rows > 0 ? (total_rows * row_full_h - list_row_gap) : 0;
    int visible_h = layout->track_snapshot.list_rect.h;
    if (visible_h < 0) visible_h = 0;
    float max_scroll = (list_content_h > visible_h) ? (float)(list_content_h - visible_h) : 0.0f;
    if (panel->track_snapshot.list_scroll > max_scroll) {
        panel->track_snapshot.list_scroll = max_scroll;
    }
    if (panel->track_snapshot.list_scroll < 0.0f) {
        panel->track_snapshot.list_scroll = 0.0f;
    }
    panel->track_snapshot.list_scroll_max = max_scroll;

    int row_w = left_inner_w - (FX_PANEL_LIST_SCROLLBAR_HIT_WIDTH + 6);
    if (row_w < 0) row_w = 0;
    row_y = list_top - (int)lroundf(panel->track_snapshot.list_scroll);
    for (int i = 0; i < total_rows; ++i) {
        SDL_Rect row = {left_inner_x,
                        row_y,
                        row_w,
                        list_row_h};
        int toggle_size = list_row_h - 6;
        if (toggle_size < 8) toggle_size = 8;
        int toggle_w = toggle_size - 3;
        if (toggle_w < 6) toggle_w = 6;
        SDL_Rect toggle_rect = {row.x + row.w - FX_PANEL_LIST_PAD - toggle_w,
                                row.y + (row.h - toggle_size) / 2,
                                toggle_w,
                                toggle_size};
        layout->list_row_rects[i] = row;
        layout->list_toggle_rects[i] = toggle_rect;
        layout->list_row_count++;
        row_y += list_row_h + list_row_gap;
    }

    if (panel->track_snapshot.list_scroll_max > 0.0f && layout->track_snapshot.list_rect.h > 0) {
        SDL_Rect track = layout->track_snapshot.list_rect;
        track.x = layout->track_snapshot.list_rect.x + layout->track_snapshot.list_rect.w - FX_PANEL_LIST_SCROLLBAR_WIDTH;
        track.w = FX_PANEL_LIST_SCROLLBAR_WIDTH;
        layout->track_snapshot.list_scroll_track = track;
        float ratio = (float)visible_h / (float)list_content_h;
        int thumb_h = (int)lroundf(ratio * (float)track.h);
        if (thumb_h < FX_PANEL_LIST_SCROLLBAR_MIN_THUMB) {
            thumb_h = FX_PANEL_LIST_SCROLLBAR_MIN_THUMB;
        }
        if (thumb_h > track.h) {
            thumb_h = track.h;
        }
        float t = panel->track_snapshot.list_scroll_max > 0.0f
                      ? panel->track_snapshot.list_scroll / panel->track_snapshot.list_scroll_max
                      : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int thumb_y = track.y + (int)lroundf(t * (float)(track.h - thumb_h));
        layout->track_snapshot.list_scroll_thumb = (SDL_Rect){track.x, thumb_y, track.w, thumb_h};
        int hit_w = FX_PANEL_LIST_SCROLLBAR_HIT_WIDTH;
        int hit_x = track.x + (track.w - hit_w) / 2;
        layout->track_snapshot.list_scroll_thumb_hit = (SDL_Rect){hit_x, thumb_y, hit_w, thumb_h};
    } else {
        layout->track_snapshot.list_scroll_track = (SDL_Rect){0, 0, 0, 0};
        layout->track_snapshot.list_scroll_thumb = (SDL_Rect){0, 0, 0, 0};
        layout->track_snapshot.list_scroll_thumb_hit = (SDL_Rect){0, 0, 0, 0};
    }

    int meter_top = snapshot_y;
    int meter_bottom = footer_y + footer_h;
    int meter_h = meter_bottom - meter_top;
    if (meter_column_w > 0 && meter_h > 0) {
        int label_w = 22;
        int bar_w = meter_column_w - label_w - FX_PANEL_SNAPSHOT_METER_LABEL_GAP;
        if (bar_w < 6) {
            bar_w = meter_column_w;
            label_w = 0;
        }
        layout->track_snapshot.meter_rect = (SDL_Rect){meter_column_x, meter_top, bar_w, meter_h};
        layout->track_snapshot.meter_clip_rect = (SDL_Rect){
            meter_column_x,
            meter_top,
            bar_w,
            FX_PANEL_SNAPSHOT_METER_CLIP_HEIGHT
        };
        for (int i = 0; i < FX_PANEL_METER_TICK_COUNT; ++i) {
            layout->track_snapshot.meter_tick_rects[i] = (SDL_Rect){0, 0, 0, 0};
            layout->track_snapshot.meter_label_rects[i] = (SDL_Rect){0, 0, 0, 0};
        }
        if (label_w > 0) {
            int label_x = meter_column_x + bar_w + FX_PANEL_SNAPSHOT_METER_LABEL_GAP;
            int label_h = 12;
            static const float kMeterTicks[FX_PANEL_METER_TICK_COUNT] = {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f};
            for (int i = 0; i < FX_PANEL_METER_TICK_COUNT; ++i) {
                float db = kMeterTicks[i];
                float t = (db - FX_PANEL_METER_DB_MIN) / (FX_PANEL_METER_DB_MAX - FX_PANEL_METER_DB_MIN);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                int y = meter_top + (int)lroundf((1.0f - t) * (float)meter_h);
                layout->track_snapshot.meter_tick_rects[i] = (SDL_Rect){
                    meter_column_x + bar_w - 4,
                    y - 1,
                    4,
                    2
                };
                layout->track_snapshot.meter_label_rects[i] = (SDL_Rect){
                    label_x,
                    y - label_h / 2,
                    label_w,
                    label_h
                };
            }
        }
    } else {
        layout->track_snapshot.meter_rect = (SDL_Rect){0, 0, 0, 0};
        layout->track_snapshot.meter_clip_rect = (SDL_Rect){0, 0, 0, 0};
        for (int i = 0; i < FX_PANEL_METER_TICK_COUNT; ++i) {
            layout->track_snapshot.meter_tick_rects[i] = (SDL_Rect){0, 0, 0, 0};
            layout->track_snapshot.meter_label_rects[i] = (SDL_Rect){0, 0, 0, 0};
        }
    }
}

void effects_panel_set_eq_detail_view(AppState* state, int view_mode) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    EffectsPanelEqDetailView next_view = (EffectsPanelEqDetailView)view_mode;
    if (next_view == EQ_DETAIL_VIEW_TRACK &&
        (panel->target != FX_PANEL_TARGET_TRACK || panel->target_track_index < 0)) {
        next_view = EQ_DETAIL_VIEW_MASTER;
    }
    if (panel->eq_detail.view_mode == next_view) {
        return;
    }
    effects_panel_eq_curve_store_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
    panel->eq_detail.view_mode = next_view;
    effects_panel_eq_curve_load_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
    panel->eq_detail.spectrum_ready = false;
}

// Clears the meter history so meter detail views start from a fresh timeline.
void effects_panel_reset_meter_history(AppState* state) {
    if (!state) {
        return;
    }
    SDL_zero(state->effects_panel.meter_history);
}

static void draw_button(SDL_Renderer* renderer,
                        const SDL_Rect* rect,
                        bool highlighted,
                        const char* label,
                        float scale,
                        const DawThemePalette* theme) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color base = theme ? theme->control_fill : (SDL_Color){48, 52, 62, 255};
    SDL_Color highlight = theme ? theme->control_active_fill : (SDL_Color){120, 160, 220, 255};
    SDL_Color border = theme ? theme->control_border : (SDL_Color){90, 95, 110, 255};
    SDL_Color text = theme ? theme->text_primary : (SDL_Color){220, 220, 230, 255};

    SDL_Color fill = base;
    SDL_Color draw_border = highlighted ? highlight : border;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, draw_border.r, draw_border.g, draw_border.b, draw_border.a);
    SDL_RenderDrawRect(renderer, rect);
    if (label) {
        int text_h = ui_font_line_height(scale);
        int text_w = ui_measure_text_width(label, scale);
        int text_x = rect->x + FX_PANEL_HEADER_BUTTON_PAD_X;
        int text_y = rect->y + (rect->h - text_h) / 2;
        int max_w = rect->w - FX_PANEL_HEADER_BUTTON_PAD_X * 2;
        if (text_w <= max_w) {
            ui_draw_text(renderer, text_x, text_y, label, text, scale);
        } else if (max_w > 0) {
            ui_draw_text_clipped(renderer, text_x, text_y, label, text, scale, max_w);
        }
    }
}

void effects_panel_init(AppState* state) {
    if (!state) {
        return;
    }
    SDL_zero(state->effects_panel);
    state->effects_panel.overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    state->effects_panel.target = FX_PANEL_TARGET_MASTER;
    state->effects_panel.target_track_index = -1;
    strncpy(state->effects_panel.target_label, "Master", sizeof(state->effects_panel.target_label) - 1);
    state->effects_panel.target_label[sizeof(state->effects_panel.target_label) - 1] = '\0';
    state->effects_panel.view_mode = FX_PANEL_VIEW_STACK;
    state->effects_panel.spec_panel_enabled = true;
    state->effects_panel.hovered_category_index = -1;
    state->effects_panel.hovered_effect_index = -1;
    state->effects_panel.active_category_index = -1;
    state->effects_panel.highlighted_slot_index = -1;
    state->effects_panel.hovered_toggle_slot_index = -1;
    state->effects_panel.selected_slot_index = -1;
    state->effects_panel.preview_toggle_hovered = false;
    state->effects_panel.focused = false;
    state->effects_panel.active_slot_index = -1;
    state->effects_panel.active_param_index = -1;
    state->effects_panel.list_open_slot_index = -1;
    state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EFFECT;
    state->effects_panel.list_last_click_ticks = 0;
    state->effects_panel.list_last_click_index = -1;
    state->effects_panel.restore_pending = false;
    state->effects_panel.restore_selected_index = -1;
    state->effects_panel.restore_open_index = -1;
    state->effects_panel.overlay_scroll_index = 0;
    state->effects_panel.title_last_click_ticks = 0;
    state->effects_panel.preview_all_open = false;
    for (int i = 0; i < FX_MASTER_MAX; ++i) {
        effects_slot_reset_runtime(&state->effects_panel.slot_runtime[i]);
        effects_panel_preview_reset(&state->effects_panel.preview_slots[i], 0, false);
    }
    state->effects_panel.param_scroll_drag_slot = -1;
    state->effects_panel.track_snapshot.gain = 1.0f;
    state->effects_panel.track_snapshot.pan = 0.0f;
    state->effects_panel.track_snapshot.list_scroll = 0.0f;
    state->effects_panel.track_snapshot.list_scroll_max = 0.0f;
    state->effects_panel.track_snapshot.list_scroll_dragging = false;
    state->effects_panel.track_snapshot.list_scroll_drag_offset = 0.0f;
    state->effects_panel.track_snapshot.instrument_menu_open = false;
    state->effects_panel.track_snapshot.instrument_menu_scroll_row = 0;
    state->effects_panel.track_snapshot.instrument_menu_expanded_category = -1;
    state->effects_panel.eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
    state->effects_panel.eq_detail.spectrum_ready = false;
    state->effects_panel.eq_detail.last_track_index = -1;
    state->effects_panel.eq_detail.spectrum_norm_lo = ENGINE_SPECTRUM_DB_FLOOR;
    state->effects_panel.eq_detail.spectrum_norm_hi = ENGINE_SPECTRUM_DB_CEIL;
    state->effects_panel.eq_detail.spectrum_norm_ready = false;
    state->effects_panel.eq_detail.spectrum_hold_frames = 0;
    state->effects_panel.eq_detail.pending_apply = false;
    state->effects_panel.eq_detail.last_apply_ticks = 0;
    SDL_zero(state->effects_panel.meter_history);
    state->effects_panel.meter_scope_mode = FX_METER_SCOPE_MID_SIDE;
    state->effects_panel.meter_lufs_mode = FX_METER_LUFS_SHORT_TERM;
    state->effects_panel.meter_spectrogram_mode = FX_METER_SPECTROGRAM_WHITE_BLACK;
    state->effects_panel.last_open_master_fx_id = 0;
    state->effects_panel.last_open_track_fx_ids = NULL;
    state->effects_panel.last_open_track_fx_count = 0;
    state->effects_panel.pending_open_fx_id = 0;
    effects_panel_eq_curve_set_defaults(&state->effects_panel.eq_curve_master);
    effects_panel_eq_curve_set_defaults(&state->effects_panel.eq_curve);
    state->effects_panel.eq_curve_tracks = NULL;
    state->effects_panel.eq_curve_tracks_count = 0;
}

void effects_panel_refresh_catalog(AppState* state) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (!state->engine) {
        panel->type_count = 0;
        panel->category_count = 0;
        panel->initialized = true;
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
        panel->overlay_scroll_index = 0;
        return;
    }
    const FxRegistryEntry* entries = NULL;
    int count = 0;
    if (!engine_fx_get_registry(state->engine, &entries, &count) || !entries) {
        panel->type_count = 0;
        panel->category_count = 0;
        panel->initialized = true;
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
        panel->overlay_scroll_index = 0;
        return;
    }
    if (count > FX_PANEL_MAX_TYPES) {
        count = FX_PANEL_MAX_TYPES;
    }
    panel->type_count = count;
    for (int i = 0; i < count; ++i) {
        FxTypeUIInfo* info = &panel->types[i];
        SDL_zero(*info);
        info->type_id = entries[i].id;
        if (entries[i].name) {
            strncpy(info->name, entries[i].name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
        }
        FxDesc desc = {0};
        if (engine_fx_registry_get_desc(state->engine, info->type_id, &desc)) {
            const EffectParamSpec* specs = NULL;
            uint32_t spec_count = 0;
            engine_fx_registry_get_param_specs(state->engine, info->type_id, &specs, &spec_count);
            if (desc.name) {
                strncpy(info->name, desc.name, sizeof(info->name) - 1);
                info->name[sizeof(info->name) - 1] = '\0';
            }
            info->param_count = desc.num_params <= FX_MAX_PARAMS ? desc.num_params : FX_MAX_PARAMS;
            for (uint32_t p = 0; p < info->param_count; ++p) {
                const char* pname = desc.param_names[p] ? desc.param_names[p] : "param";
                EffectParamSpec* spec = &info->param_specs[p];
                if (specs && p < spec_count) {
                    *spec = specs[p];
                } else {
                    effects_panel_fill_fallback_param_spec(spec, pname, desc.param_defaults[p]);
                }
                const char* label = spec->display_name ? spec->display_name : pname;
                strncpy(info->param_names[p], label, sizeof(info->param_names[p]) - 1);
                info->param_names[p][sizeof(info->param_names[p]) - 1] = '\0';
                info->param_defaults[p] = spec->default_value;
            }
        }
    }
    effects_panel_build_categories(panel);
    panel->initialized = true;
    panel->overlay_scroll_index = 0;
    if (panel->category_count == 0) {
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    }
}

void effects_panel_compute_layout(const AppState* state, EffectsPanelLayout* layout) {
    if (!state || !layout) {
        return;
    }
    zero_layout(layout);
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) {
        return;
    }
    layout->panel_rect = mixer->rect;

    int content_x = mixer->rect.x + FX_PANEL_MARGIN;
    int content_y = mixer->rect.y + FX_PANEL_MARGIN;
    int content_w = mixer->rect.w - 2 * FX_PANEL_MARGIN;
    int content_h = mixer->rect.h - 2 * FX_PANEL_MARGIN;
    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    EffectsPanelState* panel_mut = (EffectsPanelState*)&state->effects_panel;
    const EffectsPanelState* panel = panel_mut;
    if (panel_mut->param_scroll_drag_slot >= panel->chain_count) {
        panel_mut->param_scroll_drag_slot = -1;
    }
    const TrackNameEditor* editor = &state->track_name_editor;
    const char* target_label = (panel->target_label[0] != '\0') ? panel->target_label : "Master";
    if (editor && editor->editing && editor->buffer[0] != '\0') {
        target_label = editor->buffer;
        panel_mut->target = FX_PANEL_TARGET_TRACK;
        panel_mut->target_track_index = editor->track_index;
    }
    char target_line[128];
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        snprintf(target_line, sizeof(target_line), "Track FX: %s", target_label);
    } else {
        snprintf(target_line, sizeof(target_line), "Master FX");
    }
    int header_h = fx_panel_header_height();
    float title_scale = FX_PANEL_TITLE_SCALE;
    int text_w = ui_measure_text_width(target_line, title_scale);
    int text_h = ui_font_line_height(title_scale);
    int target_pad_x = max_int(10, text_h / 2);
    int target_pad_y = max_int(3, text_h / 4);
    int target_w = text_w + target_pad_x * 2;
    int target_h = text_h + target_pad_y * 2;
    if (target_h > header_h - 2) {
        target_h = max_int(0, header_h - 2);
    }

    float button_scale = FX_PANEL_BUTTON_SCALE;
    int button_h = fx_panel_button_height();
    int button_gap = max_int(FX_PANEL_HEADER_BUTTON_GAP, button_h / 3);
    int button_y = content_y + (header_h - button_h) / 2;
    int toggle_w = fx_panel_button_width("Rack", button_scale);
    int toggle_list_w = fx_panel_button_width("List", button_scale);
    if (toggle_list_w > toggle_w) {
        toggle_w = toggle_list_w;
    }
    layout->view_toggle_rect = (SDL_Rect){content_x, button_y, toggle_w, button_h};

    const char* spec_label = "Spec";
    int spec_w = fx_panel_button_width(spec_label, button_scale);
    int spec_x = layout->view_toggle_rect.x + layout->view_toggle_rect.w + button_gap;
    layout->spec_toggle_rect = (SDL_Rect){spec_x, button_y, spec_w, button_h};

    const char* add_label = "Add FX";
    int add_w = fx_panel_button_width(add_label, button_scale);
    int add_x = layout->spec_toggle_rect.x + layout->spec_toggle_rect.w + button_gap;
    layout->dropdown_button_rect = (SDL_Rect){add_x, button_y, add_w, button_h};

    const char* preview_label = "Preview";
    int preview_w = fx_panel_button_width(preview_label, button_scale);
    int preview_x = layout->dropdown_button_rect.x + layout->dropdown_button_rect.w + button_gap;
    layout->preview_toggle_rect = (SDL_Rect){preview_x, button_y, preview_w, button_h};

    int target_x = content_x + content_w - target_w;
    int target_min_x = preview_x + preview_w + button_gap;
    if (target_x < target_min_x) {
        target_x = target_min_x;
        target_w = content_x + content_w - target_x;
    }
    if (target_w < 0) {
        target_w = 0;
    }
    int target_y = content_y + (header_h - target_h) / 2;
    layout->target_label_rect = (SDL_Rect){target_x, target_y, target_w, target_h};

    int body_y = content_y + header_h;
    int body_h = content_h - header_h;
    if (body_h <= 0) {
        body_h = content_h / 2;
    }

    int list_row_h = fx_panel_list_row_height();
    int list_row_gap = fx_panel_list_row_gap();
    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        compute_list_layout(panel_mut,
                            content_x,
                            content_y,
                            content_w,
                            content_h,
                            header_h,
                            list_row_h,
                            list_row_gap,
                            layout);
    } else {
        int column_count = panel->chain_count;
        layout->column_count = column_count;

        if (column_count > FX_MASTER_MAX) {
            column_count = FX_MASTER_MAX;
        }

        if (column_count > 0) {
            int total_gaps = (column_count - 1) * FX_PANEL_COLUMN_GAP;
            int column_w = (content_w - total_gaps);
            if (column_w < 0) column_w = 0;
            column_w = column_count > 0 ? column_w / column_count : content_w;

            int start_x = content_x;
            for (int i = 0; i < column_count; ++i) {
                SDL_Rect col = {start_x, body_y, column_w, body_h};
                effects_slot_compute_layout(panel_mut,
                                            i,
                                            &col,
                                            header_h,
                                            FX_PANEL_INNER_MARGIN,
                                            FX_PANEL_PARAM_GAP,
                                            &layout->slots[i]);
                start_x += column_w + FX_PANEL_COLUMN_GAP;
            }
        }
    }

    effects_panel_compute_overlay_layout(state,
                                         panel,
                                         &mixer->rect,
                                         content_x,
                                         content_w,
                                         &layout->dropdown_button_rect,
                                         layout);
}

void effects_panel_render(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    DawThemePalette theme = {0};
    if (!renderer || !state || !layout) {
        return;
    }
    resolve_effects_panel_theme(&theme);
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color label_color = theme.text_primary;
    SDL_Color text_dim = theme.text_muted;

    SDL_SetRenderDrawColor(renderer, theme.timeline_fill.r, theme.timeline_fill.g, theme.timeline_fill.b, theme.timeline_fill.a);
    SDL_RenderFillRect(renderer, &layout->panel_rect);

    if (panel->title_debug_last_click) {
        SDL_Log("FX title clicked (target=%s, track=%d)", panel->target == FX_PANEL_TARGET_TRACK ? "track" : "master", panel->target_track_index);
        ((EffectsPanelState*)panel)->title_debug_last_click = false;
    }

    const TrackNameEditor* editor = &state->track_name_editor;
    const char* target_label = (panel->target_label[0] != '\0') ? panel->target_label : "Master";
    if (panel->target == FX_PANEL_TARGET_TRACK &&
        editor &&
        editor->editing &&
        editor->track_index == panel->target_track_index &&
        editor->buffer[0] != '\0') {
        target_label = editor->buffer;
    }
    char target_line[128];
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        snprintf(target_line, sizeof(target_line), "Track FX: %s", target_label);
    } else {
        snprintf(target_line, sizeof(target_line), "Master FX");
    }
    float title_scale = FX_PANEL_TITLE_SCALE;
    SDL_Rect title_rect = layout->target_label_rect.w > 0 ? layout->target_label_rect
                                                          : (SDL_Rect){layout->panel_rect.x + FX_PANEL_MARGIN,
                                                                       layout->panel_rect.y + FX_PANEL_MARGIN + 6,
                                                                       ui_measure_text_width(target_line, title_scale) + 12,
                                                                       ui_font_line_height(title_scale) + 8};
    SDL_SetRenderDrawColor(renderer, theme.control_fill.r, theme.control_fill.g, theme.control_fill.b, 200);
    SDL_RenderFillRect(renderer, &title_rect);
    SDL_SetRenderDrawColor(renderer, theme.control_border.r, theme.control_border.g, theme.control_border.b, theme.control_border.a);
    SDL_RenderDrawRect(renderer, &title_rect);
    int text_h = ui_font_line_height(title_scale);
    int text_y = title_rect.y + (title_rect.h - text_h) / 2;
    int title_text_x = title_rect.x + 6;
    int title_text_w = title_rect.w - 12;
    if (title_text_w > 0) {
        ui_draw_text_clipped(renderer, title_text_x, text_y, target_line, label_color, title_scale, title_text_w);
    }
    if (editor && editor->editing) {
        const char* prefix = "Track FX: ";
        int prefix_w = ui_measure_text_width(prefix, title_scale);
        char temp[ENGINE_CLIP_NAME_MAX + 32];
        snprintf(temp, sizeof(temp), "%.*s", editor->cursor, editor->buffer);
        int caret_x = title_text_x + prefix_w + ui_measure_text_width(temp, title_scale);
        if (caret_x > title_rect.x + title_rect.w - 1) {
            caret_x = title_rect.x + title_rect.w - 1;
        }
        int caret_h = ui_font_line_height(title_scale);
        SDL_Rect caret = {caret_x, text_y, 2, caret_h};
        SDL_SetRenderDrawColor(renderer, theme.text_primary.r, theme.text_primary.g, theme.text_primary.b, theme.text_primary.a);
        SDL_RenderFillRect(renderer, &caret);
    }

    // View mode + add button
    const char* toggle_label = panel->view_mode == FX_PANEL_VIEW_STACK ? "List" : "Rack";
    draw_button(renderer, &layout->view_toggle_rect, false, toggle_label, FX_PANEL_BUTTON_SCALE, &theme);
    draw_button(renderer, &layout->spec_toggle_rect, panel->spec_panel_enabled, "Spec", FX_PANEL_BUTTON_SCALE, &theme);
    bool button_active = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);
    draw_button(renderer, &layout->dropdown_button_rect, button_active, "Add FX", FX_PANEL_BUTTON_SCALE, &theme);
    draw_button(renderer, &layout->preview_toggle_rect, panel->preview_toggle_hovered, "Preview", FX_PANEL_BUTTON_SCALE, &theme);

    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        effects_panel_render_list(renderer, state, layout);
        effects_panel_render_overlay(renderer, state, layout);
        return;
    }

    if (panel->chain_count == 0) {
        char msg[160];
        snprintf(msg,
                 sizeof(msg),
                 "No effects on %s yet. Use 'Add FX' to insert one.",
                 target_label);
        int msg_x = layout->panel_rect.x + FX_PANEL_MARGIN;
        int msg_y = layout->panel_rect.y + FX_PANEL_MARGIN + 48;
        ui_draw_text(renderer, msg_x, msg_y, msg, text_dim, 2);
    }

    for (int i = 0; i < layout->column_count && i < panel->chain_count; ++i) {
        effects_slot_render(renderer,
                            state,
                            i,
                            &layout->slots[i],
                            panel->highlighted_slot_index == i,
                            panel->hovered_toggle_slot_index == i,
                            panel->selected_slot_index == i,
                            label_color,
                            text_dim);
    }

    effects_panel_render_overlay(renderer, state, layout);
}
