#include "app_state.h"
#include "effects/param_utils.h"
#include "ui/effects_panel_slot.h"
#include "ui/font.h"

#include <math.h>
void effects_slot_reset_runtime(EffectsSlotRuntime* runtime) {
    if (!runtime) {
        return;
    }
    runtime->scroll = 0.0f;
    runtime->scroll_max = 0.0f;
    runtime->dragging = false;
    runtime->drag_start_y = 0;
    runtime->drag_start_val = 0.0f;
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

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void draw_slider(SDL_Renderer* renderer, const SDL_Rect* rect, float t) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color track = {70, 74, 86, 255};
    SDL_Color track_border = {90, 95, 110, 255};
    SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Rect fill_rect = *rect;
    fill_rect.w = (int)roundf(clampf(t, 0.0f, 1.0f) * (float)rect->w);
    SDL_Color fill_color = {120, 180, 255, 200};
    SDL_SetRenderDrawColor(renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
    SDL_RenderFillRect(renderer, &fill_rect);

    SDL_Rect handle = {
        rect->x + fill_rect.w - 4,
        rect->y - 3,
        8,
        rect->h + 6,
    };
    if (handle.x < rect->x - 4) {
        handle.x = rect->x - 4;
    }
    if (handle.x + handle.w > rect->x + rect->w + 4) {
        handle.x = rect->x + rect->w - 4;
    }
    SDL_SetRenderDrawColor(renderer, 180, 210, 255, 255);
    SDL_RenderFillRect(renderer, &handle);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle);
}

static void format_beat_label(float beats, char* out, size_t out_size) {
    static const struct {
        float beats;
        const char* label;
    } kNotes[] = {
        {4.0f,    "4/1"},
        {2.0f,    "2/1"},
        {1.0f,    "1/1"},
        {0.75f,   "3/4"},
        {0.5f,    "1/2"},
        {0.333f,  "1/2T"},
        {0.25f,   "1/4"},
        {0.1875f, "1/4."},
        {0.167f,  "1/4T"},
        {0.125f,  "1/8"},
        {0.09375f,"1/8."},
        {0.083f,  "1/8T"},
        {0.0625f, "1/16"},
        {0.0468f, "1/16."},
        {0.0416f, "1/16T"},
        {0.03125f,"1/32"}
    };
    float best_diff = 1e9f;
    const char* best = NULL;
    for (size_t i = 0; i < sizeof(kNotes)/sizeof(kNotes[0]); ++i) {
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

static void format_value_label(const char* param_name,
                               float value,
                               FxParamMode mode,
                               FxParamKind kind,
                               char* out,
                               size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_kind_is_syncable(kind)) {
        format_beat_label(value, out, out_size);
        return;
    }
    char lower[64];
    for (size_t i = 0; i + 1 < sizeof(lower); ++i) {
        if (!param_name || param_name[i] == '\0') {
            lower[i] = '\0';
            break;
        }
        lower[i] = (char)tolower((unsigned char)param_name[i]);
        lower[i + 1] = '\0';
    }
    if (strstr(lower, "ms")) {
        snprintf(out, out_size, "%.1f ms", value);
    } else if (strstr(lower, "hz") || strstr(lower, "freq")) {
        snprintf(out, out_size, "%.1f Hz", value);
    } else if (strstr(lower, "db")) {
        snprintf(out, out_size, "%.1f dB", value);
    } else if (fabsf(value) >= 100.0f) {
        snprintf(out, out_size, "%.0f", value);
    } else {
        snprintf(out, out_size, "%.2f", value);
    }
}

static void draw_remove_button(SDL_Renderer* renderer, const SDL_Rect* rect, bool highlighted) {
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
    ui_draw_text(renderer, rect->x + 8, rect->y + (rect->h - 14) / 2, "-", text, 2);
}

static void draw_enable_toggle(SDL_Renderer* renderer, const SDL_Rect* rect, bool enabled, bool highlighted) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color border = highlighted ? (SDL_Color){140, 180, 230, 255} : (SDL_Color){90, 95, 110, 255};
    SDL_Color fill_off = {180, 60, 60, 220};
    if (!enabled) {
        SDL_SetRenderDrawColor(renderer, fill_off.r, fill_off.g, fill_off.b, fill_off.a);
        SDL_RenderFillRect(renderer, rect);
    }
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
}

static void draw_mode_toggle(SDL_Renderer* renderer, const SDL_Rect* rect, FxParamMode mode) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color base = {52, 56, 64, 255};
    SDL_Color active = {120, 180, 255, 230};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color text = {220, 230, 240, 255};
    SDL_Color fill = (mode == FX_PARAM_MODE_NATIVE) ? base : active;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    const char* label = (mode == FX_PARAM_MODE_NATIVE) ? "N" : "B";
    ui_draw_text(renderer, rect->x + 5, rect->y + (rect->h - 12) / 2, label, text, 1.3f);
}

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
    out_layout->header_rect = (SDL_Rect){column_rect->x, column_rect->y, column_rect->w, header_height - 8};
    out_layout->remove_rect = (SDL_Rect){column_rect->x + column_rect->w - 28, column_rect->y + 6, 22, 22};
    out_layout->toggle_rect = (SDL_Rect){out_layout->remove_rect.x - 26, out_layout->remove_rect.y, 22, 22};
    out_layout->body_rect = (SDL_Rect){
        column_rect->x + inner_margin / 2,
        column_rect->y + header_height,
        column_rect->w - inner_margin,
        column_rect->h - header_height - inner_margin / 2};
    if (out_layout->body_rect.w < 0) out_layout->body_rect.w = 0;
    if (out_layout->body_rect.h < 0) out_layout->body_rect.h = 0;

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
    int total_needed_h = (int)slot->param_count * block_h + (slot->param_count > 0 ? (int)(slot->param_count - 1) * param_gap : 0);
    int bottom_pad = block_h / 2 + 6; // ensure last slider can scroll fully into view
    int padded_needed_h = total_needed_h + bottom_pad;
    runtime->scroll_max = padded_needed_h > body_h_inner ? (float)(padded_needed_h - body_h_inner) : 0.0f;
    if (runtime->scroll < 0.0f) runtime->scroll = 0.0f;
    if (runtime->scroll > runtime->scroll_max) runtime->scroll = runtime->scroll_max;

    int max_label_w = 0;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
        const char* pname = (info && info->param_names[p]) ? info->param_names[p] : "Param";
        int w = ui_measure_text_width(pname, 1.0f);
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
        int label_h = ui_font_line_height(1.0f);
        SDL_Rect label_rect = {
            column_rect->x + inner_margin,
            param_y + (block_h - label_h) / 2,
            max_label_w,
            label_h
        };
        bool tempo_syncable = info && fx_param_kind_is_syncable(info->param_kind[p]);
        const int mode_w = 20;
        const int mode_gap = 6;
        int slider_w_param = slider_w;
        if (tempo_syncable) {
            slider_w_param -= (mode_w + mode_gap);
            if (slider_w_param < 48) {
                slider_w_param = 48;
            }
        }
        SDL_Rect slider_rect = {
            slider_x,
            param_y + (block_h - 18) / 2,
            slider_w_param,
            18
        };
        int val_h = ui_font_line_height(1.0f);
        SDL_Rect mode_rect = {0, 0, 0, 0};
        int value_x = slider_rect.x + slider_rect.w + 8;
        if (tempo_syncable) {
            mode_rect = (SDL_Rect){
                slider_rect.x + slider_rect.w + 4,
                param_y + (block_h - 18) / 2,
                mode_w,
                18
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
    if (runtime->scroll_max > 0.5f && out_layout->scrollbar_track.h > 0) {
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

void effects_slot_render(SDL_Renderer* renderer,
                         const struct AppState* state,
                         int slot_index,
                         const EffectsSlotLayout* slot_layout,
                         bool remove_highlight,
                         bool toggle_highlight,
                         bool selected,
                         SDL_Color label_color,
                         SDL_Color text_dim) {
    if (!renderer || !state || !slot_layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    const FxSlotUIState* slot = &panel->chain[slot_index];
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);

    SDL_Color box_bg = {32, 34, 42, 255};
    SDL_Color box_border = {80, 85, 100, 255};
    SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
    SDL_RenderFillRect(renderer, &slot_layout->column_rect);
    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &slot_layout->column_rect);
    if (selected) {
        SDL_SetRenderDrawColor(renderer, 120, 160, 220, 180);
        SDL_RenderDrawRect(renderer, &slot_layout->column_rect);
    }

    SDL_Rect header = slot_layout->header_rect;
    SDL_SetRenderDrawColor(renderer, 44, 48, 58, 255);
    SDL_RenderFillRect(renderer, &header);
    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &header);
    const char* fx_name = info ? info->name : "Effect";
    ui_draw_text(renderer, header.x + 8, header.y + 8, fx_name, label_color, 2);

    draw_enable_toggle(renderer, &slot_layout->toggle_rect, slot->enabled, toggle_highlight);
    draw_remove_button(renderer, &slot_layout->remove_rect, remove_highlight);

    SDL_Rect body_clip = slot_layout->body_rect;
    if (body_clip.w > 0 && body_clip.h > 0) {
        SDL_Rect prev_clip;
        SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &prev_clip);
        SDL_RenderSetClipRect(renderer, &body_clip);

        for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
            SDL_Rect label_rect = slot_layout->label_rects[p];
            SDL_Rect slider_rect = slot_layout->slider_rects[p];
            SDL_Rect value_rect = slot_layout->value_rects[p];
            if (slider_rect.y > body_clip.y + body_clip.h ||
                slider_rect.y + slider_rect.h < body_clip.y) {
                continue;
            }

            const char* pname = info ? info->param_names[p] : "Param";
            FxParamMode mode = slot->param_mode[p];
            FxParamKind kind = info ? info->param_kind[p] : FX_PARAM_KIND_GENERIC;
            bool tempo_syncable = fx_param_kind_is_syncable(kind);
            float min_v = info ? info->param_min[p] : 0.0f;
            float max_v = info ? info->param_max[p] : 1.0f;
            float value = slot->param_values[p];
            const float beat_min = 1.0f / 64.0f;
            const float beat_max = 8.0f;
            if (mode != FX_PARAM_MODE_NATIVE && tempo_syncable) {
                min_v = beat_min;
                max_v = beat_max;
                value = slot->param_beats[p];
                if (fabsf(value) < 1e-6f) {
                    value = fx_param_native_to_beats(kind, slot->param_values[p], &state->tempo);
                }
                if (value < beat_min) value = beat_min;
                if (value > beat_max) value = beat_max;
            }
            if (min_v > max_v) {
                float tmp = min_v;
                min_v = max_v;
                max_v = tmp;
            }
            if (fabsf(max_v - min_v) < 1e-6f) {
                max_v = min_v + 1.0f;
            }
            float t = (value - min_v) / (max_v - min_v);
            draw_slider(renderer, &slider_rect, t);

            char label_line[96];
            snprintf(label_line, sizeof(label_line), "%s", pname);
            ui_draw_text(renderer, label_rect.x, label_rect.y, label_line, label_color, 1.5f);

            char value_line[64];
            format_value_label(pname, value, mode, kind, value_line, sizeof(value_line));
            ui_draw_text(renderer, value_rect.x, value_rect.y, value_line, text_dim, 1.5f);
            SDL_Rect mode_rect = slot_layout->mode_rects[p];
            if (tempo_syncable && mode_rect.w > 0 && mode_rect.h > 0) {
                draw_mode_toggle(renderer, &mode_rect, mode);
            }
        }

        SDL_RenderSetClipRect(renderer, had_clip ? &prev_clip : NULL);
    }

    const EffectsSlotRuntime* runtime = &panel->slot_runtime[slot_index];
    if (runtime->scroll_max > 0.5f && slot_layout->scrollbar_track.h > 0) {
        SDL_Color track = {52, 56, 64, 180};
        SDL_Color thumb = {130, 170, 230, 220};
        SDL_Color border = {80, 85, 100, 200};
        SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
        SDL_RenderFillRect(renderer, &slot_layout->scrollbar_track);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &slot_layout->scrollbar_track);
        SDL_SetRenderDrawColor(renderer, thumb.r, thumb.g, thumb.b, thumb.a);
        SDL_RenderFillRect(renderer, &slot_layout->scrollbar_thumb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &slot_layout->scrollbar_thumb);
    }
}
