#include "app_state.h"
#include "effects/param_utils.h"
#include "ui/effects_panel_preview.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/effects_panel_spec.h"
#include "ui/font.h"

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
    int header_h = header_height - 4;
    if (header_h < 16) header_h = header_height;
    int header_y = column_rect->y + 2;
    out_layout->header_rect = (SDL_Rect){column_rect->x, header_y, column_rect->w, header_h};
    int button_size = header_h - 6;
    if (button_size < 12) button_size = 12;
    if (button_size > header_h) button_size = header_h;
    int button_y = header_y + (header_h - button_size) / 2;
    out_layout->remove_rect = (SDL_Rect){column_rect->x + column_rect->w - 6 - button_size, button_y, button_size, button_size};
    out_layout->toggle_rect = (SDL_Rect){out_layout->remove_rect.x - 6 - button_size, button_y, button_size, button_size};
    out_layout->body_rect = (SDL_Rect){
        column_rect->x + inner_margin / 2,
        column_rect->y + header_height,
        column_rect->w - inner_margin,
        column_rect->h - header_height - inner_margin / 2};
    if (out_layout->body_rect.w < 0) out_layout->body_rect.w = 0;
    if (out_layout->body_rect.h < 0) out_layout->body_rect.h = 0;

    bool preview_active = effects_slot_preview_mode(panel->chain[slot_index].type_id) != FX_PREVIEW_NONE;
    if (preview_active) {
        EffectsPanelPreviewSlotState* preview = &panel->preview_slots[slot_index];
        int preview_h = preview->open ? FX_PANEL_PREVIEW_HEIGHT : FX_PANEL_PREVIEW_COLLAPSED_HEIGHT;
        if (preview_h > out_layout->body_rect.h) {
            preview_h = out_layout->body_rect.h;
        }
        out_layout->preview_rect = (SDL_Rect){
            out_layout->body_rect.x,
            out_layout->body_rect.y + out_layout->body_rect.h - preview_h,
            out_layout->body_rect.w,
            preview_h
        };
        out_layout->preview_toggle_rect = (SDL_Rect){
            out_layout->preview_rect.x + 6,
            out_layout->preview_rect.y + 2,
            out_layout->preview_rect.w - 12,
            FX_PANEL_PREVIEW_COLLAPSED_HEIGHT - 4
        };
        if (out_layout->preview_toggle_rect.h > out_layout->preview_rect.h - 4) {
            out_layout->preview_toggle_rect.h = out_layout->preview_rect.h - 4;
        }
        out_layout->body_rect.h -= preview_h + FX_PANEL_PREVIEW_GAP;
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
    const int min_block_h = 14;
    const int max_block_h = 24;
    int param_gap = default_param_gap < 4 ? 4 : default_param_gap;
    int block_h = min_block_h;
    if (body_h_inner > 0) {
        int numerator = body_h_inner - (int)(slot->param_count > 0 ? (int)(slot->param_count - 1) * param_gap : 0);
        if (numerator < min_block_h) numerator = min_block_h;
        block_h = numerator / (int)(slot->param_count > 0 ? (int)slot->param_count : 1);
        if (block_h < min_block_h) block_h = min_block_h;
        if (block_h > max_block_h) block_h = max_block_h;
    }
    out_layout->block_height = block_h;
    out_layout->param_gap = param_gap;
    int total_needed_h = (int)slot->param_count * block_h +
                         (slot->param_count > 0 ? (int)(slot->param_count - 1) * param_gap : 0);
    int bottom_pad = block_h / 2 + 6; // ensure last slider can scroll fully into view
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
    const float text_scale = 1.3f;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        const char* pname = (info && info->param_names[p]) ? info->param_names[p] : "Param";
        int w = ui_measure_text_width(pname, text_scale);
        if (w > max_label_w) {
            max_label_w = w;
        }
    }
    max_label_w += 10;
    int value_w = 64;
    int slider_x = column_rect->x + inner_margin + max_label_w + 8;
    int slider_w = column_rect->w - (inner_margin * 2) - max_label_w - value_w - 16;
    if (slider_w < 60) slider_w = 60;

    int param_y = column_rect->y + header_height + inner_margin / 2 - (int)runtime->scroll;
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        int label_h = ui_font_line_height(text_scale);
        SDL_Rect label_rect = {
            column_rect->x + inner_margin,
            param_y + (block_h - label_h) / 2,
            max_label_w,
            label_h
        };
        const EffectParamSpec* spec = info ? &info->param_specs[p] : NULL;
        bool tempo_syncable = fx_param_spec_is_syncable(spec);
        const int mode_w = 20;
        const int mode_gap = 6;
        const int slider_h = 14;
        int slider_w_param = slider_w;
        if (tempo_syncable) {
            slider_w_param -= (mode_w + mode_gap);
            if (slider_w_param < 48) {
                slider_w_param = 48;
            }
        }
        SDL_Rect slider_rect = {
            slider_x,
            param_y + (block_h - slider_h) / 2,
            slider_w_param,
            slider_h
        };
        int val_h = ui_font_line_height(text_scale);
        SDL_Rect mode_rect = {0, 0, 0, 0};
        int value_x = slider_rect.x + slider_rect.w + 8;
        if (tempo_syncable) {
            mode_rect = (SDL_Rect){
                slider_rect.x + slider_rect.w + 4,
                param_y + (block_h - slider_h) / 2,
                mode_w,
                slider_h
            };
            value_x = mode_rect.x + mode_rect.w + mode_gap;
        }
        SDL_Rect value_rect = {
            value_x,
            param_y + (block_h - val_h) / 2,
            value_w,
            val_h
        };
        out_layout->label_rects[p] = label_rect;
        out_layout->slider_rects[p] = slider_rect;
        out_layout->value_rects[p] = value_rect;
        out_layout->mode_rects[p] = mode_rect;
        param_y += block_h + param_gap;
    }

    out_layout->scrollbar_track = (SDL_Rect){
        column_rect->x + column_rect->w - inner_margin / 2 - 8,
        out_layout->body_rect.y + 2,
        8,
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
