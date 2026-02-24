#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/layout.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FX_PANEL_SLIDER_HEIGHT 18
#define FX_PANEL_OVERLAY_WIDTH 260
#define FX_PANEL_OVERLAY_HEADER_HEIGHT 24
#define FX_PANEL_OVERLAY_PADDING 8

typedef struct {
    const char* name;
    FxTypeId    id_min;
    FxTypeId    id_max;
} FxCategorySpec;

// Ensures storage for remembering last-open FX per track.
static void effects_panel_ensure_last_open_tracks(EffectsPanelState* panel, int track_count);
// Resets preview history/state for a slot and sets the default open state.
static void effects_panel_preview_reset(EffectsPanelPreviewSlotState* preview, FxInstId fx_id, bool open_default);

static const FxCategorySpec kCategorySpecs[] = {
    {"Basics",        1u,  19u},
    {"Dynamics",      20u, 29u},
    {"EQ",            30u, 39u},
    {"Filter & Tone", 40u, 49u},
    {"Delay",         50u, 59u},
    {"Distortion",    60u, 69u},
    {"Modulation",    70u, 79u},
    {"Reverb",        90u, 99u},
    {"Metering",      100u, 109u},
};

static void zero_layout(EffectsPanelLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

// Resets preview history/state for a slot and sets the default open state.
static void effects_panel_preview_reset(EffectsPanelPreviewSlotState* preview, FxInstId fx_id, bool open_default) {
    if (!preview) {
        return;
    }
    preview->fx_id = fx_id;
    preview->open = open_default;
    preview->history_write = 0;
    preview->history_filled = false;
    for (int i = 0; i < FX_PANEL_PREVIEW_HISTORY; ++i) {
        preview->history[i] = 0.0f;
    }
}

// effects_panel_slot_supports_preview returns true when the slot type renders a preview panel.
static bool effects_panel_slot_supports_preview(FxTypeId type_id) {
    switch (type_id) {
        case 7u:
        case 20u:
        case 21u:
        case 22u:
        case 23u:
        case 60u:
        case 61u:
        case 62u:
        case 63u:
        case 64u:
        case 65u:
            return true;
        default:
            return false;
    }
}

// effects_panel_sync_meter_modes_from_slot maps meter params into meter detail UI mode state.
static void effects_panel_sync_meter_modes_from_slot(EffectsPanelState* panel, const FxSlotUIState* slot) {
    if (!panel || !slot) {
        return;
    }
    if (slot->type_id == 102u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        panel->meter_scope_mode = mode == 0 ? FX_METER_SCOPE_MID_SIDE : FX_METER_SCOPE_LEFT_RIGHT;
        return;
    }
    if (slot->type_id == 104u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        if (mode <= 0) {
            panel->meter_lufs_mode = FX_METER_LUFS_INTEGRATED;
        } else if (mode == 1) {
            panel->meter_lufs_mode = FX_METER_LUFS_SHORT_TERM;
        } else {
            panel->meter_lufs_mode = FX_METER_LUFS_MOMENTARY;
        }
        return;
    }
    if (slot->type_id == 105u && slot->param_count > 2) {
        int mode = (int)lroundf(slot->param_values[2]);
        if (mode <= 0) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_WHITE_BLACK;
        } else if (mode == 1) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_BLACK_WHITE;
        } else {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_HEAT;
        }
    }
}

static void compute_list_layout(EffectsPanelState* panel,
                                int content_x,
                                int content_y,
                                int content_w,
                                int content_h,
                                EffectsPanelLayout* layout) {
    if (!panel || !layout) {
        return;
    }
    int body_y = content_y + FX_PANEL_HEADER_HEIGHT;
    int body_h = content_h - FX_PANEL_HEADER_HEIGHT;
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
    int toggle_size = FX_PANEL_LIST_ROW_HEIGHT - 6;
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

    int button_gap = FX_PANEL_SNAPSHOT_BUTTON_GAP;
    int button_w = (left_inner_w - button_gap) / 2;
    if (button_w < 0) button_w = 0;
    layout->track_snapshot.mute_rect = (SDL_Rect){left_inner_x, footer_y, button_w, footer_h};
    layout->track_snapshot.solo_rect = (SDL_Rect){left_inner_x + button_w + button_gap, footer_y, button_w, footer_h};

    int list_top = snapshot_y + slider_h + FX_PANEL_SNAPSHOT_LIST_GAP;
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
    int row_full_h = FX_PANEL_LIST_ROW_HEIGHT + FX_PANEL_LIST_ROW_GAP;
    int list_content_h = total_rows > 0 ? (total_rows * row_full_h - FX_PANEL_LIST_ROW_GAP) : 0;
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
                        FX_PANEL_LIST_ROW_HEIGHT};
        int toggle_size = FX_PANEL_LIST_ROW_HEIGHT - 6;
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
        row_y += FX_PANEL_LIST_ROW_HEIGHT + FX_PANEL_LIST_ROW_GAP;
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

// Builds a fallback param spec when no explicit metadata is provided.
static void fill_fallback_param_spec(EffectParamSpec* spec, const char* name, float def_value) {
    if (!spec) {
        return;
    }
    SDL_zero(*spec);
    spec->id = name;
    spec->display_name = name;
    spec->type = FX_PARAM_TYPE_FLOAT;
    spec->unit = FX_PARAM_UNIT_GENERIC;
    spec->curve = FX_PARAM_CURVE_LINEAR;
    spec->ui_hint = FX_PARAM_UI_SLIDER;
    spec->flags = FX_PARAM_FLAG_AUTOMATABLE;
    spec->min_value = 0.0f;
    spec->max_value = 1.0f;
    spec->default_value = def_value;
    if (def_value < spec->min_value) {
        spec->min_value = def_value;
    }
    if (def_value > spec->max_value) {
        spec->max_value = def_value;
    }
    if (fabsf(spec->max_value - spec->min_value) < 1e-6f) {
        spec->max_value = spec->min_value + 1.0f;
    }
}

static void effects_panel_build_categories(EffectsPanelState* panel) {
    if (!panel) {
        return;
    }

    panel->category_count = 0;
    bool assigned[FX_PANEL_MAX_TYPES];
    memset(assigned, 0, sizeof(assigned));

    int spec_count = (int)(sizeof(kCategorySpecs) / sizeof(kCategorySpecs[0]));
    for (int s = 0; s < spec_count && panel->category_count < FX_PANEL_MAX_CATEGORIES; ++s) {
        const FxCategorySpec* spec = &kCategorySpecs[s];
        FxCategoryUIInfo* cat = &panel->categories[panel->category_count];
        SDL_zero(*cat);
        strncpy(cat->name, spec->name, sizeof(cat->name) - 1);
        cat->name[sizeof(cat->name) - 1] = '\0';

        for (int t = 0; t < panel->type_count; ++t) {
            FxTypeId type_id = panel->types[t].type_id;
            if (type_id >= spec->id_min && type_id <= spec->id_max) {
                if (cat->type_count < FX_PANEL_MAX_TYPES) {
                    cat->type_indices[cat->type_count++] = t;
                    assigned[t] = true;
                }
            }
        }

        if (cat->type_count > 0) {
            panel->category_count++;
        }
    }

    FxCategoryUIInfo others;
    SDL_zero(others);
    strncpy(others.name, "Other", sizeof(others.name) - 1);
    others.name[sizeof(others.name) - 1] = '\0';

    for (int t = 0; t < panel->type_count; ++t) {
        if (!assigned[t] && others.type_count < FX_PANEL_MAX_TYPES) {
            others.type_indices[others.type_count++] = t;
        }
    }

    if (others.type_count > 0 && panel->category_count < FX_PANEL_MAX_CATEGORIES) {
        panel->categories[panel->category_count++] = others;
    }

    if (panel->category_count == 0 && panel->type_count > 0) {
        FxCategoryUIInfo all;
        SDL_zero(all);
        strncpy(all.name, "All Effects", sizeof(all.name) - 1);
        all.name[sizeof(all.name) - 1] = '\0';
        for (int t = 0; t < panel->type_count && t < FX_PANEL_MAX_TYPES; ++t) {
            all.type_indices[all.type_count++] = t;
        }
        panel->categories[panel->category_count++] = all;
    }

    if (panel->category_count == 0) {
        panel->active_category_index = -1;
    } else if (panel->active_category_index >= panel->category_count) {
        panel->active_category_index = -1;
    }
}

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

static void eq_curve_copy_settings(EqCurveState* dst, const EqCurveState* src) {
    if (!dst || !src) {
        return;
    }
    dst->low_cut = src->low_cut;
    dst->high_cut = src->high_cut;
    for (int i = 0; i < 4; ++i) {
        dst->bands[i] = src->bands[i];
    }
}

static void eq_curve_clear_transient(EqCurveState* curve) {
    if (!curve) {
        return;
    }
    curve->selected_band = -1;
    curve->selected_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_band = -1;
    curve->hover_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_toggle_band = -1;
    curve->hover_toggle_low = false;
    curve->hover_toggle_high = false;
}

static void eq_curve_store_for_view(EffectsPanelState* panel,
                                    EffectsPanelEqDetailView view_mode,
                                    EffectsPanelTarget target,
                                    int track_index) {
    if (!panel) {
        return;
    }
    if (view_mode == EQ_DETAIL_VIEW_TRACK &&
        target == FX_PANEL_TARGET_TRACK &&
        track_index >= 0 &&
        track_index < panel->eq_curve_tracks_count &&
        panel->eq_curve_tracks) {
        eq_curve_copy_settings(&panel->eq_curve_tracks[track_index], &panel->eq_curve);
    } else {
        eq_curve_copy_settings(&panel->eq_curve_master, &panel->eq_curve);
    }
}

static void eq_curve_load_for_view(EffectsPanelState* panel,
                                   EffectsPanelEqDetailView view_mode,
                                   EffectsPanelTarget target,
                                   int track_index) {
    if (!panel) {
        return;
    }
    if (view_mode == EQ_DETAIL_VIEW_TRACK &&
        target == FX_PANEL_TARGET_TRACK &&
        track_index >= 0 &&
        track_index < panel->eq_curve_tracks_count &&
        panel->eq_curve_tracks) {
        eq_curve_copy_settings(&panel->eq_curve, &panel->eq_curve_tracks[track_index]);
    } else {
        eq_curve_copy_settings(&panel->eq_curve, &panel->eq_curve_master);
    }
    eq_curve_clear_transient(&panel->eq_curve);
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
    eq_curve_store_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
    panel->eq_detail.view_mode = next_view;
    eq_curve_load_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
    panel->eq_detail.spectrum_ready = false;
}

// Clears the meter history so meter detail views start from a fresh timeline.
void effects_panel_reset_meter_history(AppState* state) {
    if (!state) {
        return;
    }
    SDL_zero(state->effects_panel.meter_history);
}

void effects_panel_ensure_eq_curve_tracks(AppState* state, int track_count) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (track_count <= 0) {
        free(panel->eq_curve_tracks);
        panel->eq_curve_tracks = NULL;
        panel->eq_curve_tracks_count = 0;
        return;
    }
    if (panel->eq_curve_tracks && panel->eq_curve_tracks_count == track_count) {
        return;
    }
    EqCurveState* next = (EqCurveState*)calloc((size_t)track_count, sizeof(EqCurveState));
    if (!next) {
        return;
    }
    int copy_count = panel->eq_curve_tracks_count < track_count ? panel->eq_curve_tracks_count : track_count;
    for (int i = 0; i < copy_count; ++i) {
        next[i] = panel->eq_curve_tracks[i];
    }
    for (int i = copy_count; i < track_count; ++i) {
        eq_curve_set_defaults(&next[i]);
    }
    free(panel->eq_curve_tracks);
    panel->eq_curve_tracks = next;
    panel->eq_curve_tracks_count = track_count;
}

static bool effects_panel_update_target(AppState* state) {
    if (!state) {
        return false;
    }
    EffectsPanelState* panel = &state->effects_panel;
    EffectsPanelTarget prev_target = panel->target;
    int prev_track = panel->target_track_index;
    char prev_label[sizeof(panel->target_label)];
    strncpy(prev_label, panel->target_label, sizeof(prev_label) - 1);
    prev_label[sizeof(prev_label) - 1] = '\0';

    int track_count = 0;
    if (state->engine) {
        track_count = engine_get_track_count(state->engine);
        effects_panel_ensure_eq_curve_tracks(state, track_count);
        effects_panel_ensure_last_open_tracks(panel, track_count);
    }

    FxInstId prev_open_id = 0;
    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        prev_open_id = panel->chain[panel->list_open_slot_index].id;
    }

    panel->target = FX_PANEL_TARGET_MASTER;
    panel->target_track_index = -1;
    const char* label = "Master";
    char label_buf[sizeof(panel->target_label)];
    label_buf[0] = '\0';

    if (state->engine) {
        int sel_track = state->selected_track_index;
        int track_count = engine_get_track_count(state->engine);
        if (sel_track >= 0 && sel_track < track_count) {
            panel->target = FX_PANEL_TARGET_TRACK;
            panel->target_track_index = sel_track;
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks && sel_track >= 0 && sel_track < track_count) {
                const EngineTrack* track = &tracks[sel_track];
                if (track->name[0] != '\0') {
                    label = track->name;
                } else {
                    snprintf(label_buf, sizeof(label_buf), "Track %d", sel_track + 1);
                    label = label_buf;
                }
            }
        }
    }

    strncpy(panel->target_label, label, sizeof(panel->target_label) - 1);
    panel->target_label[sizeof(panel->target_label) - 1] = '\0';

    bool label_changed = strncmp(prev_label, panel->target_label, sizeof(prev_label)) != 0;
    bool target_changed = label_changed || prev_target != panel->target || prev_track != panel->target_track_index;
    if (target_changed) {
        if (prev_open_id != 0) {
            if (prev_target == FX_PANEL_TARGET_MASTER) {
                panel->last_open_master_fx_id = prev_open_id;
            } else if (prev_target == FX_PANEL_TARGET_TRACK &&
                       prev_track >= 0 &&
                       prev_track < panel->last_open_track_fx_count) {
                panel->last_open_track_fx_ids[prev_track] = prev_open_id;
            }
        }
        eq_curve_store_for_view(panel, panel->eq_detail.view_mode, prev_target, prev_track);
        if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
            (panel->target != FX_PANEL_TARGET_TRACK || panel->target_track_index < 0)) {
            panel->eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
        }
        eq_curve_load_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
        panel->list_open_slot_index = -1;
        panel->pending_open_fx_id = 0;
        if (panel->target == FX_PANEL_TARGET_MASTER) {
            panel->pending_open_fx_id = panel->last_open_master_fx_id;
        } else if (panel->target == FX_PANEL_TARGET_TRACK &&
                   panel->target_track_index >= 0 &&
                   panel->target_track_index < panel->last_open_track_fx_count) {
            panel->pending_open_fx_id = panel->last_open_track_fx_ids[panel->target_track_index];
        }
    }
    return target_changed;
}

// Ensures storage for remembering last-open FX per track.
static void effects_panel_ensure_last_open_tracks(EffectsPanelState* panel, int track_count) {
    if (!panel) {
        return;
    }
    if (track_count <= 0) {
        free(panel->last_open_track_fx_ids);
        panel->last_open_track_fx_ids = NULL;
        panel->last_open_track_fx_count = 0;
        return;
    }
    if (panel->last_open_track_fx_ids && panel->last_open_track_fx_count == track_count) {
        return;
    }
    FxInstId* next = (FxInstId*)calloc((size_t)track_count, sizeof(FxInstId));
    if (!next) {
        return;
    }
    int copy_count = panel->last_open_track_fx_count < track_count ? panel->last_open_track_fx_count : track_count;
    for (int i = 0; i < copy_count; ++i) {
        next[i] = panel->last_open_track_fx_ids[i];
    }
    free(panel->last_open_track_fx_ids);
    panel->last_open_track_fx_ids = next;
    panel->last_open_track_fx_count = track_count;
}

static void draw_button(SDL_Renderer* renderer, const SDL_Rect* rect, bool highlighted, const char* label, float scale) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color base = {48, 52, 62, 255};
    SDL_Color highlight = {120, 160, 220, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color text = {220, 220, 230, 255};

    SDL_Color fill = highlighted ? highlight : base;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    if (label) {
        int text_h = ui_font_line_height(scale);
        int text_x = rect->x + FX_PANEL_HEADER_BUTTON_PAD_X;
        int text_y = rect->y + (rect->h - text_h) / 2;
        ui_draw_text(renderer, text_x, text_y, label, text, scale);
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
    eq_curve_set_defaults(&state->effects_panel.eq_curve_master);
    eq_curve_set_defaults(&state->effects_panel.eq_curve);
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
                    fill_fallback_param_spec(spec, pname, desc.param_defaults[p]);
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

void effects_panel_sync_from_engine(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    int track_count = engine_get_track_count(state->engine);
    effects_panel_ensure_eq_curve_tracks(state, track_count);
    effects_panel_ensure_last_open_tracks(panel, track_count);
    bool target_changed = effects_panel_update_target(state);
    FxInstId selected_id = 0;
    if (!target_changed &&
        panel->selected_slot_index >= 0 &&
        panel->selected_slot_index < panel->chain_count) {
        selected_id = panel->chain[panel->selected_slot_index].id;
    }
    FxInstId open_id = 0;
    if (panel->pending_open_fx_id != 0) {
        open_id = panel->pending_open_fx_id;
    } else if (!target_changed &&
               panel->list_open_slot_index >= 0 &&
               panel->list_open_slot_index < panel->chain_count) {
        open_id = panel->chain[panel->list_open_slot_index].id;
    }
    FxMasterSnapshot snap;
    bool ok = false;
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        ok = engine_fx_track_snapshot(state->engine, panel->target_track_index, &snap);
    } else {
        ok = engine_fx_master_snapshot(state->engine, &snap);
    }
    if (!ok) {
        panel->chain_count = 0;
        panel->selected_slot_index = -1;
        return;
    }
    panel->chain_count = snap.count;
    for (int i = 0; i < snap.count && i < FX_MASTER_MAX; ++i) {
        FxSlotUIState* slot = &panel->chain[i];
        SDL_zero(*slot);
        slot->id = snap.items[i].id;
        slot->type_id = snap.items[i].type;
        slot->enabled = snap.items[i].enabled;
        slot->param_count = snap.items[i].param_count;
        if (slot->param_count > FX_MAX_PARAMS) {
            slot->param_count = FX_MAX_PARAMS;
        }
        for (uint32_t p = 0; p < slot->param_count; ++p) {
            slot->param_values[p] = snap.items[i].params[p];
            slot->param_mode[p] = snap.items[i].param_mode[p];
            slot->param_beats[p] = snap.items[i].param_beats[p];
        }
        bool open_default = slot->type_id == 20u;
        if (panel->preview_slots[i].fx_id != slot->id) {
            effects_panel_preview_reset(&panel->preview_slots[i], slot->id, open_default);
        }
    }
    for (int i = snap.count; i < FX_MASTER_MAX; ++i) {
        panel->chain[i].id = 0;
        panel->chain[i].param_count = 0;
        if (panel->preview_slots[i].fx_id != 0) {
            effects_panel_preview_reset(&panel->preview_slots[i], 0, false);
        }
    }
    if (panel->highlighted_slot_index >= panel->chain_count) {
        panel->highlighted_slot_index = -1;
    }
    if (panel->hovered_toggle_slot_index >= panel->chain_count) {
        panel->hovered_toggle_slot_index = -1;
    }
    if (panel->active_slot_index >= panel->chain_count) {
        panel->active_slot_index = -1;
    }
    if (panel->selected_slot_index >= panel->chain_count) {
        panel->selected_slot_index = -1;
    }
    if (panel->list_open_slot_index >= panel->chain_count) {
        panel->list_open_slot_index = -1;
    }
    if (selected_id != 0) {
        for (int i = 0; i < panel->chain_count; ++i) {
            if (panel->chain[i].id == selected_id) {
                panel->selected_slot_index = i;
                break;
            }
        }
    }
    if (open_id != 0) {
        for (int i = 0; i < panel->chain_count; ++i) {
            if (panel->chain[i].id == open_id) {
                panel->list_open_slot_index = i;
                break;
            }
        }
    }
    panel->pending_open_fx_id = 0;
    if (panel->restore_pending) {
        int sel = panel->restore_selected_index;
        int open = panel->restore_open_index;
        if (sel < 0 || sel >= panel->chain_count) {
            sel = -1;
        }
        if (open < 0 || open >= panel->chain_count) {
            open = -1;
        }
        panel->selected_slot_index = sel;
        panel->list_open_slot_index = panel->view_mode == FX_PANEL_VIEW_LIST ? open : -1;
        panel->restore_pending = false;
    }

    bool previews_open = true;
    bool preview_found = false;
    for (int i = 0; i < panel->chain_count; ++i) {
        if (!effects_panel_slot_supports_preview(panel->chain[i].type_id)) {
            continue;
        }
        preview_found = true;
        if (!panel->preview_slots[i].open) {
            previews_open = false;
            break;
        }
    }
    panel->preview_all_open = preview_found && previews_open;

    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        FxInstId active_id = panel->chain[panel->list_open_slot_index].id;
        if (panel->target == FX_PANEL_TARGET_MASTER) {
            panel->last_open_master_fx_id = active_id;
        } else if (panel->target == FX_PANEL_TARGET_TRACK &&
                   panel->target_track_index >= 0 &&
                   panel->target_track_index < panel->last_open_track_fx_count) {
            panel->last_open_track_fx_ids[panel->target_track_index] = active_id;
        }
        if (panel->list_detail_mode == FX_LIST_DETAIL_METER) {
            effects_panel_sync_meter_modes_from_slot(panel, &panel->chain[panel->list_open_slot_index]);
        }
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
    float title_scale = FX_PANEL_TITLE_SCALE;
    int text_w = ui_measure_text_width(target_line, title_scale);
    int text_h = ui_font_line_height(title_scale);
    int padding_x = 10;
    int padding_y = 6;
    int target_w = text_w + padding_x;
    int target_h = text_h + padding_y;
    int target_x = mixer->rect.x + mixer->rect.w - FX_PANEL_MARGIN - target_w;
    int target_y = content_y + (FX_PANEL_HEADER_HEIGHT - target_h) / 2;
    layout->target_label_rect = (SDL_Rect){target_x, target_y, target_w, target_h};

    float button_scale = FX_PANEL_BUTTON_SCALE;
    int button_h = FX_PANEL_HEADER_BUTTON_HEIGHT;
    int button_y = content_y + (FX_PANEL_HEADER_HEIGHT - button_h) / 2;
    int toggle_w_list = ui_measure_text_width("List", button_scale);
    int toggle_w_rack = ui_measure_text_width("Rack", button_scale);
    int toggle_w = (toggle_w_list > toggle_w_rack ? toggle_w_list : toggle_w_rack) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    layout->view_toggle_rect = (SDL_Rect){content_x, button_y, toggle_w, button_h};

    const char* spec_label = "Spec";
    int spec_w = ui_measure_text_width(spec_label, button_scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    int spec_x = layout->view_toggle_rect.x + layout->view_toggle_rect.w + FX_PANEL_HEADER_BUTTON_GAP;
    layout->spec_toggle_rect = (SDL_Rect){spec_x, button_y, spec_w, button_h};

    const char* add_label = "Add FX";
    int add_w = ui_measure_text_width(add_label, button_scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    int add_x = layout->spec_toggle_rect.x + layout->spec_toggle_rect.w + FX_PANEL_HEADER_BUTTON_GAP;
    layout->dropdown_button_rect = (SDL_Rect){add_x, button_y, add_w, button_h};

    const char* preview_label = "Preview";
    int preview_w = ui_measure_text_width(preview_label, button_scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    int preview_x = layout->dropdown_button_rect.x + layout->dropdown_button_rect.w + FX_PANEL_HEADER_BUTTON_GAP;
    layout->preview_toggle_rect = (SDL_Rect){preview_x, button_y, preview_w, button_h};

    int body_y = content_y + FX_PANEL_HEADER_HEIGHT;
    int body_h = content_h - FX_PANEL_HEADER_HEIGHT;
    if (body_h <= 0) {
        body_h = content_h / 2;
    }

    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        compute_list_layout(panel_mut, content_x, content_y, content_w, content_h, layout);
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
                                            FX_PANEL_HEADER_HEIGHT,
                                            FX_PANEL_INNER_MARGIN,
                                            FX_PANEL_PARAM_GAP,
                                            &layout->slots[i]);
                start_x += column_w + FX_PANEL_COLUMN_GAP;
            }
        }
    }

    layout->overlay_visible = false;
    layout->overlay_item_count = 0;
    layout->overlay_total_items = 0;
    layout->overlay_visible_count = 0;
    layout->overlay_has_scrollbar = false;
    const bool overlay_active = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);
    if (!overlay_active) {
        return;
    }

    int overlay_total_items = 0;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        overlay_total_items = panel->category_count;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
            overlay_total_items = panel->categories[panel->active_category_index].type_count;
        }
    }

    int overlay_w = FX_PANEL_OVERLAY_WIDTH;
    if (overlay_w > content_w) {
        overlay_w = content_w;
    }
    int overlay_x = layout->dropdown_button_rect.x;
    if (overlay_x + overlay_w > content_x + content_w) {
        overlay_x = content_x + content_w - overlay_w;
    }
    int overlay_y = layout->dropdown_button_rect.y + layout->dropdown_button_rect.h + 6;
    int available_h = (mixer->rect.y + mixer->rect.h) - FX_PANEL_MARGIN - overlay_y;
    if (available_h < FX_PANEL_OVERLAY_HEADER_HEIGHT + FX_PANEL_DROPDOWN_ITEM_HEIGHT) {
        return;
    }

    int max_visible_items = (available_h - FX_PANEL_OVERLAY_HEADER_HEIGHT) / FX_PANEL_DROPDOWN_ITEM_HEIGHT;
    if (max_visible_items <= 0) {
        return;
    }
    if (overlay_total_items <= 0) {
        layout->overlay_visible = false;
        return;
    }

    int display_capacity = overlay_total_items;
    if (display_capacity < 1) display_capacity = 1;
    if (display_capacity > max_visible_items) {
        display_capacity = max_visible_items;
    }

    int max_scroll = overlay_total_items - display_capacity;
    if (max_scroll < 0) max_scroll = 0;
    int scroll_index = panel->overlay_scroll_index;
    if (scroll_index > max_scroll) {
        scroll_index = max_scroll;
    }
    if (scroll_index < 0) {
        scroll_index = 0;
    }

    int first_index = scroll_index;
    int remaining = overlay_total_items - first_index;
    if (remaining < 0) remaining = 0;
    int visible_items = remaining;
    if (visible_items > display_capacity) {
        visible_items = display_capacity;
    }
    if (visible_items < 0) {
        visible_items = 0;
    }

    SDL_Rect overlay_rect = {
        overlay_x,
        overlay_y,
        overlay_w,
        FX_PANEL_OVERLAY_HEADER_HEIGHT + display_capacity * FX_PANEL_DROPDOWN_ITEM_HEIGHT + FX_PANEL_OVERLAY_PADDING
    };

    layout->overlay_visible = true;
    layout->overlay_rect = overlay_rect;
    layout->overlay_header_rect = (SDL_Rect){overlay_rect.x, overlay_rect.y, overlay_rect.w, FX_PANEL_OVERLAY_HEADER_HEIGHT};
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        layout->overlay_back_rect = (SDL_Rect){
            overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
            overlay_rect.y + 6,
            24,
            FX_PANEL_OVERLAY_HEADER_HEIGHT - 12
        };
    } else {
        layout->overlay_back_rect = (SDL_Rect){0, 0, 0, 0};
    }

    layout->overlay_total_items = overlay_total_items;
    layout->overlay_visible_count = visible_items;
    layout->overlay_item_count = visible_items;

    int item_y = overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 4;
    const int scrollbar_w = 8;
    bool has_scrollbar = (overlay_total_items > display_capacity);
    layout->overlay_has_scrollbar = has_scrollbar;
    int item_width = overlay_rect.w - 2 * FX_PANEL_OVERLAY_PADDING - (has_scrollbar ? (scrollbar_w + 4) : 0);
    if (item_width < 80) item_width = overlay_rect.w - 2 * FX_PANEL_OVERLAY_PADDING;

    for (int i = 0; i < visible_items; ++i) {
        layout->overlay_item_rects[i] = (SDL_Rect){
            overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
            item_y,
            item_width,
            FX_PANEL_DROPDOWN_ITEM_HEIGHT
        };
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = first_index + i;
            layout->overlay_item_order[i] = (cat_index >= 0 && cat_index < panel->category_count) ? cat_index : -1;
        } else {
            const FxCategoryUIInfo* cat = NULL;
            if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
                cat = &panel->categories[panel->active_category_index];
            }
            int type_index = -1;
            if (cat) {
                int cat_offset = first_index + i;
                if (cat_offset >= 0 && cat_offset < cat->type_count) {
                    type_index = cat->type_indices[cat_offset];
                }
            }
            layout->overlay_item_order[i] = type_index;
        }
        item_y += FX_PANEL_DROPDOWN_ITEM_HEIGHT;
    }

    if (has_scrollbar) {
        int track_x = overlay_rect.x + overlay_rect.w - FX_PANEL_OVERLAY_PADDING - scrollbar_w;
        int track_y = overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 4;
        int track_h = display_capacity * FX_PANEL_DROPDOWN_ITEM_HEIGHT;
        layout->overlay_scrollbar_track = (SDL_Rect){track_x, track_y, scrollbar_w, track_h};
        float visible_ratio = (overlay_total_items > 0) ? (float)visible_items / (float)overlay_total_items : 1.0f;
        if (visible_ratio < 0.05f) visible_ratio = 0.05f;
        int thumb_h = (int)(track_h * visible_ratio);
        if (thumb_h < 10) thumb_h = 10;
        int max_scroll_index = overlay_total_items - visible_items;
        float scroll_ratio = (max_scroll_index > 0) ? (float)scroll_index / (float)max_scroll_index : 0.0f;
        if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
        int thumb_y = track_y + (int)((track_h - thumb_h) * scroll_ratio);
        if (thumb_y > track_y + track_h - thumb_h) thumb_y = track_y + track_h - thumb_h;
        layout->overlay_scrollbar_thumb = (SDL_Rect){track_x, thumb_y, scrollbar_w, thumb_h};
    } else {
        layout->overlay_scrollbar_track = (SDL_Rect){0,0,0,0};
        layout->overlay_scrollbar_thumb = (SDL_Rect){0,0,0,0};
    }
}

static void render_overlay(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout || !layout->overlay_visible) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color bg = {26, 26, 32, 240};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color header_bg = {48, 52, 62, 255};
    SDL_Color label = {210, 210, 220, 255};
    SDL_Color hover = {80, 110, 160, 255};

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_rect);

    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_header_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_header_rect);

    const char* title = "Add Effect";
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS && panel->active_category_index >= 0 &&
        panel->active_category_index < panel->category_count) {
        title = panel->categories[panel->active_category_index].name;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        title = "Select Category";
    }
    int header_text_y = layout->overlay_header_rect.y +
                        (layout->overlay_header_rect.h - ui_font_line_height(2.0f)) / 2;
    ui_draw_text(renderer,
                 layout->overlay_header_rect.x + FX_PANEL_OVERLAY_PADDING + (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS ? 32 : 8),
                 header_text_y,
                 title,
                 label,
                 2);

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        SDL_Color back_color = {70, 90, 120, 255};
        SDL_SetRenderDrawColor(renderer, back_color.r, back_color.g, back_color.b, back_color.a);
        SDL_RenderFillRect(renderer, &layout->overlay_back_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_back_rect);
        int back_text_y = layout->overlay_back_rect.y +
                          (layout->overlay_back_rect.h - ui_font_line_height(2.0f)) / 2;
        ui_draw_text(renderer,
                     layout->overlay_back_rect.x + 6,
                     back_text_y,
                     "<",
                     label,
                     2);
    }

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        bool invalid_cat = (panel->active_category_index < 0 || panel->active_category_index >= panel->category_count);
        bool empty_cat = false;
        if (!invalid_cat) {
            const FxCategoryUIInfo* cat = &panel->categories[panel->active_category_index];
            empty_cat = (cat->type_count == 0);
        }
        if (invalid_cat || empty_cat || layout->overlay_item_count == 0) {
            const char* msg = "No effects in this category.";
            ui_draw_text(renderer,
                         layout->overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
                         layout->overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 12,
                         msg,
                         label,
                         2);
            return;
        }
    }

    for (int i = 0; i < layout->overlay_item_count; ++i) {
        SDL_Rect item_rect = layout->overlay_item_rects[i];
        bool is_hover = false;
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            if (layout->overlay_item_order[i] == panel->hovered_category_index) {
                is_hover = true;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            if (layout->overlay_item_order[i] == panel->hovered_effect_index) {
                is_hover = true;
            }
        }
        if (is_hover) {
            SDL_SetRenderDrawColor(renderer, hover.r, hover.g, hover.b, hover.a);
            SDL_RenderFillRect(renderer, &item_rect);
        }

        const char* item_label = "";
        char buffer[96];
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = layout->overlay_item_order[i];
            if (cat_index >= 0 && cat_index < panel->category_count) {
                const FxCategoryUIInfo* cat = &panel->categories[cat_index];
                snprintf(buffer, sizeof(buffer), "%s (%d)", cat->name, cat->type_count);
                item_label = buffer;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            int type_index = layout->overlay_item_order[i];
            if (type_index >= 0 && type_index < panel->type_count) {
                item_label = panel->types[type_index].name;
            }
        }
        int item_text_y = item_rect.y + (item_rect.h - ui_font_line_height(1.5f)) / 2;
        ui_draw_text(renderer, item_rect.x + 8, item_text_y, item_label, label, 1.5f);
    }

    if (layout->overlay_has_scrollbar) {
        SDL_Color track = {52, 56, 64, 220};
        SDL_Color thumb = {120, 160, 220, 255};
        SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, thumb.r, thumb.g, thumb.b, thumb.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_thumb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_thumb);
    }
}

void effects_panel_render(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color label_color = {210, 210, 220, 255};
    SDL_Color text_dim = {160, 170, 190, 255};

    SDL_SetRenderDrawColor(renderer, 28, 30, 38, 255);
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
    SDL_SetRenderDrawColor(renderer, 40, 44, 54, 200);
    SDL_RenderFillRect(renderer, &title_rect);
    SDL_SetRenderDrawColor(renderer, 90, 95, 110, 255);
    SDL_RenderDrawRect(renderer, &title_rect);
    int text_h = ui_font_line_height(title_scale);
    int text_y = title_rect.y + (title_rect.h - text_h) / 2;
    ui_draw_text(renderer, title_rect.x + 6, text_y, target_line, label_color, title_scale);
    if (editor && editor->editing) {
        const char* prefix = "Track FX: ";
        int prefix_w = ui_measure_text_width(prefix, title_scale);
        char temp[ENGINE_CLIP_NAME_MAX + 32];
        snprintf(temp, sizeof(temp), "%.*s", editor->cursor, editor->buffer);
        int caret_x = title_rect.x + 6 + prefix_w + ui_measure_text_width(temp, title_scale);
        if (caret_x > title_rect.x + title_rect.w) {
            caret_x = title_rect.x + title_rect.w;
        }
        int caret_h = ui_font_line_height(title_scale);
        SDL_Rect caret = {caret_x, text_y, 2, caret_h};
        SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        SDL_RenderFillRect(renderer, &caret);
    }

    // View mode + add button
    const char* toggle_label = panel->view_mode == FX_PANEL_VIEW_STACK ? "List" : "Rack";
    draw_button(renderer, &layout->view_toggle_rect, false, toggle_label, FX_PANEL_BUTTON_SCALE);
    draw_button(renderer, &layout->spec_toggle_rect, panel->spec_panel_enabled, "Spec", FX_PANEL_BUTTON_SCALE);
    bool button_active = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);
    draw_button(renderer, &layout->dropdown_button_rect, button_active, "Add FX", FX_PANEL_BUTTON_SCALE);
    draw_button(renderer, &layout->preview_toggle_rect, panel->preview_toggle_hovered, "Preview", FX_PANEL_BUTTON_SCALE);

    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        effects_panel_render_list(renderer, state, layout);
        render_overlay(renderer, state, layout);
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

    render_overlay(renderer, state, layout);
}
