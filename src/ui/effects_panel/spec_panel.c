#include "ui/effects_panel_spec.h"

#include "app_state.h"
#include "ui/font.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define FX_SPEC_WIDGET_MIN_WIDTH 120
#define FX_SPEC_WIDGET_MAX_WIDTH 180
#define FX_SPEC_WIDGET_HEIGHT 36
#define FX_SPEC_WIDGET_MIN_HEIGHT 28
#define FX_SPEC_WIDGET_GAP_X 12
#define FX_SPEC_WIDGET_GAP_Y 10
#define FX_SPEC_PANEL_PADDING 8
#define FX_SPEC_CONTROL_HEIGHT 10
#define FX_SPEC_CONTROL_PAD 6
#define FX_SPEC_KNOB_SIZE 36
#define FX_SPEC_TOGGLE_W 26
#define FX_SPEC_TOGGLE_H 14
#define FX_SPEC_DROPDOWN_H 18
#define FX_SPEC_MODE_TOGGLE_SIZE 14
#define FX_SPEC_MODE_TOGGLE_GAP 6
#define FX_SPEC_LEFT_COL_MIN 64
#define FX_SPEC_RIGHT_COL_MIN 28

static int max_int(int a, int b) {
    return (a > b) ? a : b;
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

// spec_panel_group_stats counts group transitions and visible params for a spec panel layout.
static void spec_panel_group_stats(const FxTypeUIInfo* info,
                                   const FxSlotUIState* slot,
                                   int* out_group_count,
                                   int* out_visible_count) {
    if (!info || !slot || !out_group_count || !out_visible_count) {
        return;
    }
    int group_count = 0;
    int visible_count = 0;
    const char* last_group = NULL;
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        const EffectParamSpec* spec = &info->param_specs[p];
        if (spec->flags & FX_PARAM_FLAG_HIDDEN) {
            continue;
        }
        visible_count++;
        const char* group = spec->section ? spec->section : spec->group;
        if (group && (!last_group || strcmp(group, last_group) != 0)) {
            group_count++;
            last_group = group;
        }
    }
    *out_group_count = group_count;
    *out_visible_count = visible_count;
}

// spec_panel_show_group_labels returns true when group headers should be rendered for a slot.
static bool spec_panel_show_group_labels(FxTypeId type_id, int group_count) {
    if (group_count <= 1) {
        return false;
    }
    switch (type_id) {
        case 30u: // BiquadEQ
        case 31u: // EQ_Fixed3
        case 32u: // EQ_Notch
        case 33u: // EQ_Tilt
        case 40u: // SVF
        case 41u: // AutoWah
        case 43u: // TiltEQ
            return false;
        default:
            return true;
    }
}

// Calculates the widget height for a given row count and available vertical space.
static int spec_widget_height_for_rows(int row_count,
                                       int available_h,
                                       int label_h,
                                       int value_h) {
    int control_h = max_int(FX_SPEC_CONTROL_HEIGHT, ui_font_line_height(1.0f) + 2);
    int min_height = label_h + value_h + control_h + FX_SPEC_CONTROL_PAD * 2 + 4;
    if (min_height < FX_SPEC_WIDGET_MIN_HEIGHT) {
        min_height = FX_SPEC_WIDGET_MIN_HEIGHT;
    }
    int height = FX_SPEC_WIDGET_HEIGHT;
    if (row_count > 0 && available_h > 0) {
        int candidate = (available_h - (row_count - 1) * FX_SPEC_WIDGET_GAP_Y) / row_count;
        if (candidate < height) {
            height = candidate;
        }
    }
    if (height < min_height) {
        height = min_height;
    }
    if (height > FX_SPEC_WIDGET_HEIGHT) {
        height = FX_SPEC_WIDGET_HEIGHT;
    }
    return height;
}

// Builds a compressor-specific layout with a fixed row/column arrangement.
static void effects_panel_spec_layout_compressor(const FxSlotUIState* slot,
                                                 const SDL_Rect* body_rect,
                                                 int widget_height,
                                                 float scroll_offset,
                                                 EffectsSpecPanelLayout* out_layout) {
    if (!slot || !body_rect || !out_layout) {
        return;
    }
    const int pad = FX_SPEC_PANEL_PADDING;
    const int gap_x = FX_SPEC_WIDGET_GAP_X;
    const int gap_y = FX_SPEC_WIDGET_GAP_Y;
    int content_w = body_rect->w - pad * 2;
    if (content_w < 0) {
        content_w = 0;
    }
    int columns = 3;
    int needed_w = FX_SPEC_WIDGET_MIN_WIDTH * columns + gap_x * (columns - 1);
    while (columns > 1 && content_w < needed_w) {
        columns -= 1;
        needed_w = FX_SPEC_WIDGET_MIN_WIDTH * columns + gap_x * (columns - 1);
    }
    if (columns < 1) columns = 1;
    int column_w = (content_w - gap_x * (columns - 1)) / columns;
    if (content_w < FX_SPEC_WIDGET_MIN_WIDTH) {
        column_w = content_w;
    } else if (column_w < FX_SPEC_WIDGET_MIN_WIDTH) {
        column_w = FX_SPEC_WIDGET_MIN_WIDTH;
    }
    if (column_w > FX_SPEC_WIDGET_MAX_WIDTH) column_w = FX_SPEC_WIDGET_MAX_WIDTH;
    int row_h = widget_height + gap_y;
    int start_x = body_rect->x + pad;
    int start_y = body_rect->y + pad - (int)lroundf(scroll_offset);
    int label_h = ui_font_line_height(1.1f);
    int value_h = ui_font_line_height(1.0f);

    const uint32_t order[] = {0u, 1u, 4u, 2u, 3u, 5u, 6u};
    const int order_count = (int)(sizeof(order) / sizeof(order[0]));
    for (int i = 0; i < order_count; ++i) {
        uint32_t param_index = order[i];
        if (param_index >= slot->param_count) {
            continue;
        }
        if (out_layout->widget_count >= FX_MAX_PARAMS) {
            return;
        }
        int row = i / columns;
        int col = i % columns;
        int x = start_x + col * (column_w + gap_x);
        int y = start_y + row * row_h;
        FxSpecWidget* widget = &out_layout->widgets[out_layout->widget_count++];
        widget->slot_index = -1;
        widget->param_index = param_index;
        widget->rect = (SDL_Rect){x, y, column_w, widget_height};
        widget->label_rect = (SDL_Rect){x, y, column_w, label_h};
        widget->value_rect = (SDL_Rect){x, y + widget_height - value_h, column_w, value_h};
    }
}

// Returns true if the effect type should use the spec-driven panel layout.
bool effects_panel_spec_enabled(const EffectsPanelState* panel, FxTypeId type_id) {
    if (!panel || !panel->spec_panel_enabled) {
        return false;
    }
    switch (type_id) {
        case 1u:  // Gain
        case 2u:  // DCBlock
        case 3u:  // Pan
        case 4u:  // Mute
        case 5u:  // MonoMakerLow
        case 6u:  // StereoBlend
        case 7u:  // AutoTrim
        case 20u: // Compressor
        case 21u: // Limiter
        case 22u: // Gate
        case 23u: // DeEsser
        case 24u: // SCCompressor
        case 25u: // UpwardComp
        case 26u: // Expander
        case 27u: // TransientShaper
        case 30u: // BiquadEQ
        case 31u: // EQ_Fixed3
        case 32u: // EQ_Notch
        case 33u: // EQ_Tilt
        case 40u: // SVF
        case 41u: // AutoWah
        case 42u: // StereoWidth
        case 43u: // TiltEQ
        case 44u: // Phaser
        case 45u: // FormantFilter
        case 46u: // CombFF
        case 60u: // HardClip
        case 61u: // SoftSaturation
        case 62u: // BitCrusher
        case 63u: // Overdrive
        case 64u: // Waveshaper
        case 65u: // Decimator
        case 70u: // TremoloPan
        case 71u: // Chorus
        case 72u: // Flanger
        case 73u: // Vibrato
        case 74u: // RingMod
        case 75u: // AutoPan
        case 76u: // BarberpolePhaser
        case 50u: // Delay
        case 51u: // PingPongDelay
        case 52u: // MultiTapDelay
        case 53u: // TapeEcho
        case 54u: // DiffusionDelay
        case 90u: // Reverb
        case 91u: // EarlyReflections
        case 92u: // PlateLite
        case 93u: // GatedReverb
        case 100u: // CorrelationMeter
        case 101u: // MidSideMeter
        case 102u: // VectorScope
        case 103u: // PeakRmsMeter
        case 104u: // LufsMeter
        case 105u: // SpectrogramMeter
            return true;
        default:
            return false;
    }
}

int effects_panel_spec_measure_height(const EffectsPanelState* panel,
                                      const FxSlotUIState* slot,
                                      const SDL_Rect* body_rect,
                                      int* out_widget_height) {
    if (!panel || !slot || !body_rect) {
        return 0;
    }
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info) {
        return 0;
    }
    int pad = FX_SPEC_PANEL_PADDING;
    int gap_x = FX_SPEC_WIDGET_GAP_X;
    int label_h = ui_font_line_height(1.1f);
    int value_h = ui_font_line_height(1.0f);
    int content_w = body_rect->w - pad * 2;
    if (content_w < 0) {
        content_w = 0;
    }
    int columns = content_w / (FX_SPEC_WIDGET_MIN_WIDTH + gap_x);
    if (columns < 1) {
        columns = 1;
    }
    int content_h = 0;

    const FxTypeId compressor_id = 20u;
    if (slot->type_id == compressor_id) {
        int min_columns = 1;
        int needed_w = FX_SPEC_WIDGET_MIN_WIDTH * columns + gap_x * (columns - 1);
        while (columns > min_columns && content_w < needed_w) {
            columns -= 1;
            needed_w = FX_SPEC_WIDGET_MIN_WIDTH * columns + gap_x * (columns - 1);
        }
        if (columns < min_columns) {
            columns = min_columns;
        }
        const uint32_t order[] = {0u, 1u, 4u, 2u, 3u, 5u, 6u};
        const int order_count = (int)(sizeof(order) / sizeof(order[0]));
        int row_count = order_count > 0 ? (order_count + columns - 1) / columns : 0;
        int available_h = body_rect->h - pad * 2;
        int widget_h = spec_widget_height_for_rows(row_count, available_h, label_h, value_h);
        content_h = pad * 2 + row_count * widget_h + (row_count > 0 ? (row_count - 1) * FX_SPEC_WIDGET_GAP_Y : 0);
        if (out_widget_height) {
            *out_widget_height = widget_h;
        }
        return content_h;
    }

    int group_label_count = 0;
    int segment_count = 0;
    int segment_sizes[FX_MAX_PARAMS];
    const char* last_group = NULL;
    int group_count = 0;
    int visible_count = 0;
    spec_panel_group_stats(info, slot, &group_count, &visible_count);
    bool show_group_labels = spec_panel_show_group_labels(slot->type_id, group_count);

    if (!show_group_labels) {
        segment_count = (visible_count > 0) ? 1 : 0;
        if (segment_count > 0) {
            segment_sizes[0] = visible_count;
        }
    } else {
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        const EffectParamSpec* spec = &info->param_specs[p];
        if (spec->flags & FX_PARAM_FLAG_HIDDEN) {
            continue;
        }
        const char* group = spec->section ? spec->section : spec->group;
        bool new_group = group && (!last_group || strcmp(group, last_group) != 0);
        if (segment_count == 0 || new_group) {
            if (segment_count < FX_MAX_PARAMS) {
                segment_sizes[segment_count] = 0;
                segment_count++;
            }
        }
        if (segment_count > 0) {
            segment_sizes[segment_count - 1] += 1;
        }
        if (new_group) {
            if (show_group_labels) {
                group_label_count++;
            }
            last_group = group;
        }
    }
    }

    int row_count = 0;
    for (int i = 0; i < segment_count; ++i) {
        int seg = segment_sizes[i];
        if (seg > 0) {
            row_count += (seg + columns - 1) / columns;
        }
    }
    int available_h = body_rect->h - pad * 2 - group_label_count * (label_h + 6);
    int widget_h = spec_widget_height_for_rows(row_count, available_h, label_h, value_h);
    content_h = pad * 2 + group_label_count * (label_h + 6) +
                row_count * widget_h + (row_count > 0 ? (row_count - 1) * FX_SPEC_WIDGET_GAP_Y : 0);
    if (out_widget_height) {
        *out_widget_height = widget_h;
    }
    return content_h;
}

void effects_panel_spec_compute_layout(const AppState* state,
                                       const EffectsPanelState* panel,
                                       const FxSlotUIState* slot,
                                       const SDL_Rect* body_rect,
                                       float scroll_offset,
                                       EffectsSpecPanelLayout* out_layout) {
    if (!state || !panel || !slot || !body_rect || !out_layout) {
        return;
    }
    SDL_zero(*out_layout);
    out_layout->body_rect = *body_rect;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info) {
        return;
    }
    int widget_height = FX_SPEC_WIDGET_HEIGHT;
    effects_panel_spec_measure_height(panel, slot, body_rect, &widget_height);

    const FxTypeId compressor_id = 20u;
    if (slot->type_id == compressor_id) {
        effects_panel_spec_layout_compressor(slot, body_rect, widget_height, scroll_offset, out_layout);
    } else {
        int label_h = ui_font_line_height(1.1f);
        int value_h = ui_font_line_height(1.0f);
        int pad = FX_SPEC_PANEL_PADDING;
        int gap_x = FX_SPEC_WIDGET_GAP_X;
        int gap_y = FX_SPEC_WIDGET_GAP_Y;
        int content_w = body_rect->w - pad * 2;
        if (content_w < 0) content_w = 0;
        int columns = content_w / (FX_SPEC_WIDGET_MIN_WIDTH + gap_x);
        if (columns < 1) columns = 1;
        int column_w = (content_w - gap_x * (columns - 1)) / columns;
        if (content_w < FX_SPEC_WIDGET_MIN_WIDTH) {
            column_w = content_w;
        } else if (column_w < FX_SPEC_WIDGET_MIN_WIDTH) {
            column_w = FX_SPEC_WIDGET_MIN_WIDTH;
        }
        if (column_w > FX_SPEC_WIDGET_MAX_WIDTH) column_w = FX_SPEC_WIDGET_MAX_WIDTH;
        int start_x = body_rect->x + pad;
        int x = start_x;
        int y = body_rect->y + pad - (int)lroundf(scroll_offset);
        int max_x = body_rect->x + body_rect->w - pad;
        const char* last_group = NULL;
        int group_count = 0;
        int visible_count = 0;
        spec_panel_group_stats(info, slot, &group_count, &visible_count);
        bool show_group_labels = spec_panel_show_group_labels(slot->type_id, group_count);

        for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
            const EffectParamSpec* spec = &info->param_specs[p];
            if (spec->flags & FX_PARAM_FLAG_HIDDEN) {
                continue;
            }
            const char* group = spec->section ? spec->section : spec->group;
            if (show_group_labels && group && (!last_group || strcmp(group, last_group) != 0)) {
                if (x != start_x) {
                    x = start_x;
                    y += widget_height + gap_y;
                }
                int group_index = out_layout->group_count;
                if (group_index < FX_MAX_PARAMS) {
                    out_layout->group_labels[group_index] = group;
                    out_layout->group_label_rects[group_index] =
                        (SDL_Rect){body_rect->x + pad,
                                   y,
                                   max_x - (body_rect->x + pad),
                                   label_h};
                    out_layout->group_count++;
                }
                y += label_h + 6;
                x = start_x;
                last_group = group;
            } else if (show_group_labels) {
                last_group = group;
            }

            if (x + column_w > max_x && x > body_rect->x + pad) {
                x = start_x;
                y += widget_height + gap_y;
            }

            if (out_layout->widget_count >= FX_MAX_PARAMS) {
                break;
            }
            FxSpecWidget* widget = &out_layout->widgets[out_layout->widget_count++];
            widget->slot_index = -1;
            widget->param_index = p;
            widget->rect = (SDL_Rect){x, y, column_w, widget_height};
            widget->label_rect = (SDL_Rect){x, y, column_w, label_h};
            widget->value_rect = (SDL_Rect){x, y + widget_height - value_h, column_w, value_h};
            x += column_w + gap_x;
        }
    }

    for (int i = 0; i < out_layout->widget_count; ++i) {
        FxSpecWidget* widget = &out_layout->widgets[i];
        if (widget->param_index >= info->param_count) {
            continue;
        }
        const EffectParamSpec* spec = &info->param_specs[widget->param_index];
        int label_h = widget->label_rect.h;
        int value_h = ui_font_line_height(1.0f);
        int available_control_h = widget->rect.h - FX_SPEC_CONTROL_PAD * 2;
        if (available_control_h < 0) {
            available_control_h = 0;
        }
        FxSpecWidgetType type = FX_SPEC_WIDGET_SLIDER;
        if (spec->ui_hint == FX_PARAM_UI_KNOB) {
            type = FX_SPEC_WIDGET_KNOB;
        } else if (spec->ui_hint == FX_PARAM_UI_TOGGLE) {
            type = FX_SPEC_WIDGET_TOGGLE;
        } else if (spec->ui_hint == FX_PARAM_UI_DROPDOWN) {
            type = FX_SPEC_WIDGET_DROPDOWN;
        }
        widget->type = type;

        int text_h = ui_font_line_height(1.0f);
        int control_h_base = max_int(FX_SPEC_CONTROL_HEIGHT, text_h + 2);
        int toggle_h = max_int(FX_SPEC_TOGGLE_H, text_h + 4);
        int dropdown_h = max_int(FX_SPEC_DROPDOWN_H, text_h + 4);
        int mode_size_base = max_int(FX_SPEC_MODE_TOGGLE_SIZE, ui_font_line_height(0.9f) + 2);
        int mode_gap_base = max_int(FX_SPEC_MODE_TOGGLE_GAP, mode_size_base / 4);
        int control_w = FX_SPEC_RIGHT_COL_MIN;
        int control_h = control_h_base;
        if (type == FX_SPEC_WIDGET_KNOB) {
            control_w = max_int(FX_SPEC_KNOB_SIZE, text_h + 10);
            if (control_w > widget->rect.w) {
                control_w = widget->rect.w;
            }
            control_h = control_w;
        } else if (type == FX_SPEC_WIDGET_TOGGLE) {
            control_w = FX_SPEC_TOGGLE_W;
            control_h = toggle_h;
        } else if (type == FX_SPEC_WIDGET_DROPDOWN) {
            int dropdown_w = 80;
            if (spec->enum_count > 0) {
                for (uint32_t e = 0; e < spec->enum_count; ++e) {
                    const char* opt = spec->enum_labels[e];
                    if (!opt) {
                        continue;
                    }
                    int w = ui_measure_text_width(opt, 1.0f) + 10;
                    if (w > dropdown_w) {
                        dropdown_w = w;
                    }
                }
            }
            control_w = dropdown_w;
            control_h = dropdown_h;
        } else {
            control_w = widget->rect.w - FX_SPEC_LEFT_COL_MIN - FX_SPEC_CONTROL_PAD;
            if (control_w < FX_SPEC_RIGHT_COL_MIN) {
                control_w = FX_SPEC_RIGHT_COL_MIN;
            }
            control_h = control_h_base;
        }

        if (type != FX_SPEC_WIDGET_KNOB && control_h > available_control_h) {
            control_h = available_control_h;
        }
        if (control_h < 0) {
            control_h = 0;
        }
        if (control_w > widget->rect.w) {
            control_w = widget->rect.w;
        }
        if (control_w < 0) {
            control_w = 0;
        }
        if (control_w > widget->rect.w - FX_SPEC_CONTROL_PAD) {
            control_w = widget->rect.w - FX_SPEC_CONTROL_PAD;
        }
        if (control_w < 0) {
            control_w = 0;
        }

        int mode_size = 0;
        int mode_gap = 0;
        if (fx_param_spec_is_syncable(spec)) {
            mode_size = mode_size_base;
            if (mode_size > control_h) {
                mode_size = control_h;
            }
            if (mode_size < 0) {
                mode_size = 0;
            }
            mode_gap = mode_gap_base;
        }
        int right_extra = mode_size > 0 ? (mode_size + mode_gap) : 0;
        int max_control_w = widget->rect.w - FX_SPEC_CONTROL_PAD - right_extra;
        if (max_control_w < 0) {
            max_control_w = 0;
        }
        if (control_w > max_control_w) {
            control_w = max_control_w;
        }

        int left_col_w = widget->rect.w - control_w - right_extra - FX_SPEC_CONTROL_PAD;
        if (left_col_w < FX_SPEC_LEFT_COL_MIN) {
            int adjust = FX_SPEC_LEFT_COL_MIN - left_col_w;
            control_w -= adjust;
            left_col_w = FX_SPEC_LEFT_COL_MIN;
        }
        if (control_w < FX_SPEC_RIGHT_COL_MIN) {
            control_w = FX_SPEC_RIGHT_COL_MIN;
            if (control_w > max_control_w) {
                control_w = max_control_w;
            }
            left_col_w = widget->rect.w - control_w - right_extra - FX_SPEC_CONTROL_PAD;
        }
        if (left_col_w < 0) {
            left_col_w = 0;
        }
        if (control_w < 0) {
            control_w = 0;
        }
        int left_x = widget->rect.x;
        int right_x = left_x + left_col_w + FX_SPEC_CONTROL_PAD;

        widget->label_rect = (SDL_Rect){left_x, widget->rect.y, left_col_w, label_h};
        widget->value_rect = (SDL_Rect){left_x, widget->rect.y + widget->rect.h - value_h, left_col_w, value_h};

        int control_y = widget->rect.y + (widget->rect.h - control_h) / 2;
        int control_x = right_x;
        if (mode_size > 0) {
            int mode_y = control_y + (control_h - mode_size) / 2;
            widget->mode_rect = (SDL_Rect){right_x, mode_y, mode_size, mode_size};
            control_x = right_x + mode_size + mode_gap;
        } else {
            widget->mode_rect = (SDL_Rect){0, 0, 0, 0};
        }
        widget->control_rect = (SDL_Rect){control_x,
                                          control_y,
                                          control_w,
                                          control_h};
    }
}
