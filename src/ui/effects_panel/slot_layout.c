#include "app_state.h"
#include "effects/param_utils.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_preview.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/effects_panel_spec.h"
#include "ui/font.h"

#include <math.h>
#include <stdio.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int slot_header_height_min(void) {
    int text_h = ui_font_line_height(FX_PANEL_BUTTON_SCALE);
    int needed = text_h + 8;
    return max_int(FX_PANEL_HEADER_HEIGHT, needed);
}

static int slot_inner_margin_min(void) {
    int needed = ui_font_line_height(1.0f) / 2 + 8;
    return max_int(FX_PANEL_INNER_MARGIN, needed);
}

static int slot_param_gap_min(void) {
    int needed = ui_font_line_height(1.0f) / 3 + 4;
    return max_int(FX_PANEL_PARAM_GAP, needed);
}

static int slot_slider_height(void) {
    int needed = ui_font_line_height(1.0f) + 2;
    return max_int(14, needed);
}

static int slot_mode_size(void) {
    int text_h = ui_font_line_height(1.0f);
    int text_w = ui_measure_text_width("B", 1.0f);
    int needed = max_int(text_h, text_w) + 4;
    return max_int(16, needed);
}

static int slot_preview_toggle_height(void) {
    int needed = ui_font_line_height(1.0f) + 4;
    return max_int(FX_PANEL_PREVIEW_COLLAPSED_HEIGHT - 4, needed);
}

static int slot_preview_collapsed_height(void) {
    int needed = slot_preview_toggle_height() + 4;
    return max_int(FX_PANEL_PREVIEW_COLLAPSED_HEIGHT, needed);
}

static int slot_preview_open_height(void) {
    int needed = ui_font_line_height(0.9f) + slot_preview_toggle_height() + 20;
    return max_int(FX_PANEL_PREVIEW_HEIGHT, needed);
}

static void format_beat_label(float beats, char* out, size_t out_size) {
    static const struct {
        float beats;
        const char* label;
    } kNotes[] = {
        {4.0f, "4/1"},
        {2.0f, "2/1"},
        {1.0f, "1/1"},
        {0.75f, "3/4"},
        {0.5f, "1/2"},
        {1.0f / 3.0f, "1/2T"},
        {0.25f, "1/4"},
        {0.1875f, "1/4."},
        {1.0f / 6.0f, "1/4T"},
        {0.125f, "1/8"},
        {0.09375f, "1/8."},
        {1.0f / 12.0f, "1/8T"},
        {0.0625f, "1/16"},
        {3.0f / 64.0f, "1/16."},
        {1.0f / 24.0f, "1/16T"},
        {0.03125f, "1/32"}};
    float best_diff = 1e9f;
    const char* best = NULL;
    for (size_t i = 0; i < sizeof(kNotes) / sizeof(kNotes[0]); ++i) {
        float diff = fabsf(beats - kNotes[i].beats);
        if (diff < best_diff) {
            best_diff = diff;
            best = kNotes[i].label;
        }
    }
    if (best && best_diff < 0.02f) {
        snprintf(out, out_size, "%s", best);
    } else {
        snprintf(out, out_size, "%.3f b", beats);
    }
}

static void format_value_label(const EffectParamSpec* spec,
                               float value,
                               FxParamMode mode,
                               char* out,
                               size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        format_beat_label(value, out, out_size);
        return;
    }
    fx_param_format_value(spec, value, out, out_size);
}

static int measure_value_width(const FxSlotUIState* slot,
                               uint32_t param_index,
                               const EffectParamSpec* spec,
                               float text_scale) {
    if (!slot || param_index >= FX_MAX_PARAMS) {
        return 0;
    }
    FxParamMode mode = slot->param_mode[param_index];
    char label[64];
    int max_w = 0;
    const float current_value = slot->param_values[param_index];
    format_value_label(spec, current_value, mode, label, sizeof(label));
    max_w = ui_measure_text_width(label, text_scale);

    if (spec) {
        format_value_label(spec, spec->min_value, FX_PARAM_MODE_NATIVE, label, sizeof(label));
        max_w = max_int(max_w, ui_measure_text_width(label, text_scale));
        format_value_label(spec, spec->max_value, FX_PARAM_MODE_NATIVE, label, sizeof(label));
        max_w = max_int(max_w, ui_measure_text_width(label, text_scale));
    }

    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        const float beat_samples[] = {slot->param_beats[param_index], 0.25f, 1.0f, 4.0f};
        for (size_t i = 0; i < sizeof(beat_samples) / sizeof(beat_samples[0]); ++i) {
            format_beat_label(beat_samples[i], label, sizeof(label));
            max_w = max_int(max_w, ui_measure_text_width(label, text_scale));
        }
    }

    max_w = max_int(max_w, ui_measure_text_width("0", text_scale));
    return max_w + 8;
}

// find_type_info returns the UI type metadata for a given effect id.
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

// effects_slot_compute_layout calculates rects and scroll bounds for a slot's UI.
void effects_slot_compute_layout(struct EffectsPanelState* panel,
                                 int slot_index,
                                 const SDL_Rect* column_rect,
                                 int header_height,
                                 int inner_margin,
                                 int default_param_gap,
                                 EffectsSlotLayout* out_layout) {
    if (!panel || !column_rect || !out_layout || slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    SDL_zero(*out_layout);
    out_layout->column_rect = *column_rect;
    int header_h = max_int(header_height, slot_header_height_min());
    int margin_px = max_int(inner_margin, slot_inner_margin_min());
    int param_gap_floor = max_int(default_param_gap, slot_param_gap_min());

    int header_y = column_rect->y;
    out_layout->header_rect = (SDL_Rect){column_rect->x, header_y, column_rect->w, header_h};
    int button_pad = max_int(3, header_h / 6);
    int button_size = header_h - button_pad * 2;
    button_size = clamp_int(button_size, 12, max_int(12, header_h - 2));
    int button_y = header_y + (header_h - button_size) / 2;
    int button_gap = max_int(6, button_size / 3);
    out_layout->remove_rect = (SDL_Rect){
        column_rect->x + column_rect->w - button_gap - button_size,
        button_y,
        button_size,
        button_size};
    out_layout->toggle_rect = (SDL_Rect){
        out_layout->remove_rect.x - button_gap - button_size,
        button_y,
        button_size,
        button_size};

    int body_top_gap = max_int(2, margin_px / 4);
    int body_bottom_gap = max_int(2, margin_px / 4);
    out_layout->body_rect = (SDL_Rect){
        column_rect->x + margin_px / 2,
        column_rect->y + header_h + body_top_gap,
        column_rect->w - margin_px,
        column_rect->h - header_h - body_top_gap - body_bottom_gap};
    if (out_layout->body_rect.w < 0) out_layout->body_rect.w = 0;
    if (out_layout->body_rect.h < 0) out_layout->body_rect.h = 0;

    bool preview_active = effects_slot_preview_mode(panel->chain[slot_index].type_id) != FX_PREVIEW_NONE;
    if (preview_active) {
        EffectsPanelPreviewSlotState* preview = &panel->preview_slots[slot_index];
        int preview_h = preview->open ? slot_preview_open_height() : slot_preview_collapsed_height();
        if (preview_h > out_layout->body_rect.h) {
            preview_h = out_layout->body_rect.h;
        }
        out_layout->preview_rect = (SDL_Rect){
            out_layout->body_rect.x,
            out_layout->body_rect.y + out_layout->body_rect.h - preview_h,
            out_layout->body_rect.w,
            preview_h
        };
        int toggle_h = slot_preview_toggle_height();
        int toggle_pad_x = max_int(6, margin_px / 2);
        int toggle_pad_y = max_int(2, toggle_h / 5);
        out_layout->preview_toggle_rect = (SDL_Rect){
            out_layout->preview_rect.x + toggle_pad_x,
            out_layout->preview_rect.y + toggle_pad_y,
            out_layout->preview_rect.w - toggle_pad_x * 2,
            toggle_h
        };
        int preview_inner_h = out_layout->preview_rect.h - toggle_pad_y * 2;
        if (preview_inner_h < 0) {
            preview_inner_h = 0;
        }
        if (out_layout->preview_toggle_rect.h > preview_inner_h) {
            out_layout->preview_toggle_rect.h = preview_inner_h;
        }
        int preview_gap = max_int(FX_PANEL_PREVIEW_GAP, ui_font_line_height(1.0f) / 3 + 2);
        out_layout->body_rect.h -= preview_h + preview_gap;
        if (out_layout->body_rect.h < 0) {
            out_layout->body_rect.h = 0;
        }
    } else {
        out_layout->preview_rect = (SDL_Rect){0, 0, 0, 0};
        out_layout->preview_toggle_rect = (SDL_Rect){0, 0, 0, 0};
    }

    FxSlotUIState* slot = &panel->chain[slot_index];
    out_layout->param_count = slot->param_count;
    out_layout->block_height = 0;
    out_layout->param_gap = default_param_gap;
    out_layout->scrollbar_track = (SDL_Rect){0,0,0,0};
    out_layout->scrollbar_thumb = (SDL_Rect){0,0,0,0};
    EffectsSlotRuntime* runtime = &panel->slot_runtime[slot_index];
    if (slot->param_count == 0) {
        effects_slot_reset_runtime(runtime);
        return;
    }

    int body_h_inner = out_layout->body_rect.h;
    if (body_h_inner < 0) body_h_inner = 0;
    const float label_scale = 1.3f;
    const float value_scale = 1.3f;
    int label_h = ui_font_line_height(label_scale);
    int value_h = ui_font_line_height(value_scale);
    int slider_h = slot_slider_height();
    int mode_size = slot_mode_size();
    int row_content_h = max_int(max_int(label_h, value_h), max_int(slider_h, mode_size));
    int row_pad_y = max_int(2, row_content_h / 5);
    int min_block_h = row_content_h + row_pad_y * 2;
    int preferred_block_h = min_block_h + max_int(2, row_pad_y / 2);
    int param_gap = max_int(param_gap_floor, max_int(4, min_block_h / 5));
    int block_h = preferred_block_h;
    if (body_h_inner > 0) {
        int numerator = body_h_inner - (int)(slot->param_count > 0 ? (int)(slot->param_count - 1) * param_gap : 0);
        int available_per = numerator / (int)(slot->param_count > 0 ? (int)slot->param_count : 1);
        if (available_per < min_block_h) {
            block_h = min_block_h;
        } else if (available_per > preferred_block_h) {
            block_h = preferred_block_h;
        } else {
            block_h = available_per;
        }
    }
    out_layout->block_height = block_h;
    out_layout->param_gap = param_gap;
    int total_needed_h = (int)slot->param_count * block_h +
                         (slot->param_count > 0 ? (int)(slot->param_count - 1) * param_gap : 0);
    int bottom_pad = max_int(block_h / 2 + 6, row_pad_y + 8); // ensure last slider can scroll fully into view
    int padded_needed_h = total_needed_h + bottom_pad;
    if (effects_panel_spec_enabled(panel, slot->type_id)) {
        int widget_height = 0;
        int content_height = effects_panel_spec_measure_height(panel, slot, &out_layout->body_rect, &widget_height);
        if (content_height > 0) {
            if (widget_height <= 0) {
                widget_height = block_h;
            }
            bottom_pad = widget_height / 2 + 6;
            padded_needed_h = content_height + bottom_pad;
        }
    }
    runtime->scroll_max = padded_needed_h > body_h_inner ? (float)(padded_needed_h - body_h_inner) : 0.0f;
    if (runtime->scroll < 0.0f) runtime->scroll = 0.0f;
    if (runtime->scroll > runtime->scroll_max) runtime->scroll = runtime->scroll_max;

    int max_label_w = 0;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    int max_value_w = 0;
    int min_label_w = ui_measure_text_width("P", label_scale) + 6;
    int min_value_w = ui_measure_text_width("0", value_scale) + 8;
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        const char* pname = (info && info->param_names[p]) ? info->param_names[p] : "Param";
        int w = ui_measure_text_width(pname, label_scale) + 10;
        if (w > max_label_w) {
            max_label_w = w;
        }
        const EffectParamSpec* spec = info ? &info->param_specs[p] : NULL;
        int value_w = measure_value_width(slot, p, spec, value_scale);
        if (value_w > max_value_w) {
            max_value_w = value_w;
        }
    }
    int scrollbar_w = 8;
    int scrollbar_gap = max_int(4, margin_px / 3);
    int body_left = out_layout->body_rect.x + 2;
    int body_right = out_layout->body_rect.x + out_layout->body_rect.w - 2;
    int content_right = body_right - scrollbar_w - scrollbar_gap;
    if (content_right <= body_left + 40) {
        content_right = body_right;
    }
    int content_w = content_right - body_left;
    if (content_w < 0) {
        content_w = 0;
    }
    if (max_label_w < min_label_w) {
        max_label_w = min_label_w;
    }
    int max_label_allowed = content_w > 0 ? (content_w * 2) / 5 : max_label_w;
    if (max_label_allowed > 0) {
        max_label_w = min_int(max_label_w, max_label_allowed);
    }
    int value_w = max_int(max_value_w, min_value_w);
    int max_value_allowed = content_w > 0 ? content_w / 3 : value_w;
    if (max_value_allowed > 0) {
        value_w = min_int(value_w, max_value_allowed);
    }
    int label_gap = max_int(6, margin_px / 3);
    int value_gap = max_int(6, margin_px / 3);
    int mode_gap = max_int(4, margin_px / 4);
    int slider_min_w = max_int(48, ui_measure_text_width("00", value_scale) + 10);

    int param_y = out_layout->body_rect.y - (int)runtime->scroll;
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        int row_label_w = max_label_w;
        int row_value_w = value_w;
        SDL_Rect label_rect = {
            body_left,
            param_y + (block_h - label_h) / 2,
            row_label_w,
            label_h
        };
        const EffectParamSpec* spec = info ? &info->param_specs[p] : NULL;
        bool tempo_syncable = fx_param_spec_is_syncable(spec);
        int row_mode_w = tempo_syncable ? mode_size : 0;
        int fixed_w = row_label_w + label_gap + row_value_w + value_gap + (row_mode_w > 0 ? row_mode_w + mode_gap : 0);
        int slider_w_param = content_w - fixed_w;
        if (slider_w_param < slider_min_w) {
            int deficit = slider_min_w - slider_w_param;
            int shrink_label = min_int(deficit, row_label_w - min_label_w);
            row_label_w -= shrink_label;
            deficit -= shrink_label;
            int shrink_value = min_int(deficit, row_value_w - min_value_w);
            row_value_w -= shrink_value;
            fixed_w = row_label_w + label_gap + row_value_w + value_gap + (row_mode_w > 0 ? row_mode_w + mode_gap : 0);
            slider_w_param = content_w - fixed_w;
        }
        if (slider_w_param < 32) {
            slider_w_param = 32;
        }
        label_rect.w = row_label_w;
        int slider_x = body_left + row_label_w + label_gap;
        if (tempo_syncable) {
            int max_for_sync = content_right - slider_x - value_gap - mode_gap - row_mode_w - row_value_w;
            if (slider_w_param > max_for_sync) {
                slider_w_param = max_for_sync;
            }
        } else {
            int max_for_native = content_right - slider_x - value_gap - row_value_w;
            if (slider_w_param > max_for_native) {
                slider_w_param = max_for_native;
            }
        }
        if (slider_w_param < 24) {
            slider_w_param = 24;
        }
        SDL_Rect slider_rect = {
            slider_x,
            param_y + (block_h - slider_h) / 2,
            slider_w_param,
            slider_h
        };
        int val_h = value_h;
        SDL_Rect mode_rect = {0, 0, 0, 0};
        int value_x = slider_rect.x + slider_rect.w + value_gap;
        if (tempo_syncable) {
            mode_rect = (SDL_Rect){
                slider_rect.x + slider_rect.w + value_gap,
                param_y + (block_h - row_mode_w) / 2,
                row_mode_w,
                row_mode_w
            };
            value_x = mode_rect.x + mode_rect.w + mode_gap;
        }
        SDL_Rect value_rect = {
            value_x,
            param_y + (block_h - val_h) / 2,
            row_value_w,
            val_h
        };
        out_layout->label_rects[p] = label_rect;
        out_layout->slider_rects[p] = slider_rect;
        out_layout->value_rects[p] = value_rect;
        out_layout->mode_rects[p] = mode_rect;
        param_y += block_h + param_gap;
    }

    out_layout->scrollbar_track = (SDL_Rect){
        out_layout->body_rect.x + out_layout->body_rect.w - scrollbar_w,
        out_layout->body_rect.y + 2,
        scrollbar_w,
        out_layout->body_rect.h - 4
    };
    if (runtime->scroll_max > 0.5f && out_layout->scrollbar_track.h > 0 && padded_needed_h > 0) {
        int track_h = out_layout->scrollbar_track.h;
        int thumb_h = (int)((float)track_h * ((float)body_h_inner / (float)padded_needed_h));
        if (thumb_h < 12) thumb_h = 12;
        if (thumb_h > track_h) thumb_h = track_h;
        int thumb_y = out_layout->scrollbar_track.y;
        int travel = track_h - thumb_h;
        if (travel < 1) travel = 1;
        thumb_y += (int)((runtime->scroll / runtime->scroll_max) * (float)travel);
        out_layout->scrollbar_thumb = (SDL_Rect){
            out_layout->scrollbar_track.x,
            thumb_y,
            out_layout->scrollbar_track.w,
            thumb_h
        };
    } else {
        out_layout->scrollbar_thumb = (SDL_Rect){0,0,0,0};
    }
}
