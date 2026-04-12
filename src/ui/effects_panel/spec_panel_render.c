#include "ui/effects_panel_spec.h"

#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct EffectsSpecTheme {
    SDL_Color border;
    SDL_Color fill;
    SDL_Color active;
    SDL_Color knob_ring;
    SDL_Color knob_indicator;
} EffectsSpecTheme;

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

static void resolve_spec_theme(EffectsSpecTheme* out) {
    if (!out) {
        return;
    }

    {
        DawThemePalette palette = {0};
        if (daw_shared_theme_resolve_palette(&palette)) {
            out->border = palette.control_border;
            out->fill = palette.control_fill;
            out->active = palette.selection_fill;
            out->knob_ring = palette.slider_track;
            out->knob_indicator = palette.slider_handle;
            return;
        }
    }

    *out = (EffectsSpecTheme){
        .border = {80, 86, 102, 255},
        .fill = {36, 40, 50, 255},
        .active = {130, 170, 230, 255},
        .knob_ring = {80, 86, 102, 255},
        .knob_indicator = {150, 190, 240, 255},
    };
}

static bool rect_intersects(const SDL_Rect* a, const SDL_Rect* b) {
    if (!a || !b) {
        return false;
    }
    int ax2 = a->x + a->w;
    int ay2 = a->y + a->h;
    int bx2 = b->x + b->w;
    int by2 = b->y + b->h;
    return (a->x < bx2 && ax2 > b->x && a->y < by2 && ay2 > b->y);
}

static SDL_Rect spec_body_clip_rect(const SDL_Rect* body_rect) {
    SDL_Rect clip = body_rect ? *body_rect : (SDL_Rect){0, 0, 0, 0};
    const int inset_y = 6;
    if (clip.h > inset_y * 2) {
        clip.y += inset_y;
        clip.h -= inset_y * 2;
    }
    return clip;
}

static void draw_knob(SDL_Renderer* renderer, const SDL_Rect* rect, float t, const EffectsSpecTheme* theme) {
    if (!renderer || !rect) {
        return;
    }
    int cx = rect->x + rect->w / 2;
    int cy = rect->y + rect->h / 2;
    int r = rect->w < rect->h ? rect->w / 2 : rect->h / 2;
    if (r < 2) {
        return;
    }
    const int segments = 16;
    SDL_Color ring = theme ? theme->knob_ring : (SDL_Color){80, 86, 102, 255};
    SDL_Color indicator = theme ? theme->knob_indicator : (SDL_Color){150, 190, 240, 255};
    SDL_SetRenderDrawColor(renderer, ring.r, ring.g, ring.b, ring.a);
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i * (2.0f * (float)M_PI / (float)segments);
        float a1 = (float)(i + 1) * (2.0f * (float)M_PI / (float)segments);
        int x0 = cx + (int)lroundf(cosf(a0) * r);
        int y0 = cy + (int)lroundf(sinf(a0) * r);
        int x1 = cx + (int)lroundf(cosf(a1) * r);
        int y1 = cy + (int)lroundf(sinf(a1) * r);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }
    float angle = (0.75f + t * 1.5f) * (float)M_PI;
    int ix = cx + (int)lroundf(cosf(angle) * (r - 2));
    int iy = cy + (int)lroundf(sinf(angle) * (r - 2));
    SDL_SetRenderDrawColor(renderer, indicator.r, indicator.g, indicator.b, indicator.a);
    SDL_RenderDrawLine(renderer, cx, cy, ix, iy);
}

static void format_beat_label(float beats, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    struct {
        float beats;
        const char* label;
    } kNotes[] = {
        {4.0f,    "4/1"},
        {3.0f,    "3/1"},
        {2.0f,    "2/1"},
        {1.5f,    "1/2."},
        {1.0f,    "1/1"},
        {0.75f,   "3/4"},
        {0.5f,    "1/2"},
        {1.0f / 3.0f, "1/2T"},
        {0.25f,   "1/4"},
        {0.1875f, "1/4."},
        {1.0f / 6.0f, "1/4T"},
        {0.125f,  "1/8"},
        {0.09375f,"1/8."},
        {1.0f / 12.0f, "1/8T"},
        {0.0625f, "1/16"},
        {3.0f / 64.0f, "1/16."},
        {1.0f / 24.0f, "1/16T"},
        {0.03125f,"1/32"}
    };
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

static float widget_display_value(const FxSlotUIState* slot,
                                  const EffectParamSpec* spec,
                                  uint32_t param_index,
                                  const TempoState* tempo) {
    float value = slot->param_values[param_index];
    FxParamMode mode = slot->param_mode[param_index];
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        float beat_min = 0.0f;
        float beat_max = 0.0f;
        if (!fx_param_spec_get_beat_bounds(spec, tempo, &beat_min, &beat_max)) {
            return value;
        }
        value = slot->param_beats[param_index];
        if (fabsf(value) < 1e-6f) {
            value = fx_param_spec_native_to_beats(spec, slot->param_values[param_index], tempo);
        }
        if (value < beat_min) value = beat_min;
        if (value > beat_max) value = beat_max;
    }
    return value;
}

void effects_panel_spec_render(SDL_Renderer* renderer,
                               const AppState* state,
                               const FxSlotUIState* slot,
                               const EffectsSpecPanelLayout* layout,
                               SDL_Color label_color,
                               SDL_Color value_color) {
    if (!renderer || !state || !slot || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info) {
        return;
    }
    EffectsSpecTheme theme = {0};
    resolve_spec_theme(&theme);
    SDL_Color border = theme.border;
    SDL_Color fill = theme.fill;
    SDL_Color active = theme.active;
    SDL_Rect clip_rect = spec_body_clip_rect(&layout->body_rect);

    for (int g = 0; g < layout->group_count; ++g) {
        const char* label = layout->group_labels[g] ? layout->group_labels[g] : "";
        SDL_Rect rect = layout->group_label_rects[g];
        if (!rect_intersects(&rect, &clip_rect)) {
            continue;
        }
        ui_draw_text_clipped(renderer, rect.x, rect.y, label, label_color, 1.1f, rect.w);
    }

    for (int i = 0; i < layout->widget_count; ++i) {
        const FxSpecWidget* widget = &layout->widgets[i];
        if (!rect_intersects(&widget->rect, &clip_rect)) {
            continue;
        }
        const EffectParamSpec* spec = &info->param_specs[widget->param_index];
        const char* label = (spec->display_name && spec->display_name[0]) ? spec->display_name
                                                                           : info->param_names[widget->param_index];

        float display_value = widget_display_value(slot, spec, widget->param_index, &state->tempo);
        float t = 0.0f;
        if (slot->param_mode[widget->param_index] != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
            float beat_min = 0.0f;
            float beat_max = 0.0f;
            if (fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max)) {
                t = (display_value - beat_min) / (beat_max - beat_min);
            }
        } else {
            t = fx_param_map_value_to_ui(spec, display_value);
        }
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &widget->rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &widget->rect);

        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &widget->control_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &widget->control_rect);

        if (widget->type == FX_SPEC_WIDGET_SLIDER) {
            int fill_w = (int)lroundf((float)widget->control_rect.w * t);
            SDL_Rect bar = widget->control_rect;
            bar.w = fill_w;
            SDL_SetRenderDrawColor(renderer, active.r, active.g, active.b, active.a);
            SDL_RenderFillRect(renderer, &bar);
        } else if (widget->type == FX_SPEC_WIDGET_KNOB) {
            draw_knob(renderer, &widget->control_rect, t, &theme);
        } else if (widget->type == FX_SPEC_WIDGET_TOGGLE) {
            if (display_value >= 0.5f) {
                SDL_SetRenderDrawColor(renderer, active.r, active.g, active.b, active.a);
                SDL_RenderFillRect(renderer, &widget->control_rect);
            }
        } else if (widget->type == FX_SPEC_WIDGET_DROPDOWN) {
            if (spec->enum_count > 0 && spec->enum_labels[0]) {
                int idx = (int)lroundf(display_value);
                if (idx < 0) idx = 0;
                if (idx >= (int)spec->enum_count) idx = (int)spec->enum_count - 1;
                const char* opt = spec->enum_labels[idx];
                int opt_x = widget->control_rect.x + 4;
                int opt_y = widget->control_rect.y + (widget->control_rect.h - ui_font_line_height(1.0f)) / 2;
                int opt_w = widget->control_rect.w - 8;
                if (opt_w > 0) {
                    ui_draw_text_clipped(renderer,
                                         opt_x,
                                         opt_y,
                                         opt ? opt : "",
                                         label_color,
                                         1.0f,
                                         opt_w);
                }
            }
        }

        int label_x = widget->label_rect.x + 2;
        int label_w = widget->label_rect.w - 4;
        if (label_w > 0) {
            ui_draw_text_clipped(renderer,
                                 label_x,
                                 widget->label_rect.y,
                                 label,
                                 label_color,
                                 1.1f,
                                 label_w);
        }

        if (widget->mode_rect.w > 0 && widget->mode_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderDrawRect(renderer, &widget->mode_rect);
            const char* mode_label = slot->param_mode[widget->param_index] == FX_PARAM_MODE_NATIVE ? "" : "B";
            if (mode_label[0]) {
                int mode_scale_h = ui_font_line_height(0.9f);
                int mode_y = widget->mode_rect.y + (widget->mode_rect.h - mode_scale_h) / 2;
                ui_draw_text_clipped(renderer,
                                     widget->mode_rect.x + 1,
                                     mode_y,
                                     mode_label,
                                     label_color,
                                     0.9f,
                                     widget->mode_rect.w - 2);
            }
        }

        char value_line[64];
        format_value_label(spec, display_value, slot->param_mode[widget->param_index], value_line, sizeof(value_line));
        ui_draw_text_clipped(renderer,
                             widget->value_rect.x,
                             widget->value_rect.y,
                             value_line,
                             value_color,
                             1.0f,
                             widget->value_rect.w);
    }
}

bool effects_panel_spec_hit_test(const EffectsSpecPanelLayout* layout,
                                 const SDL_Point* point,
                                 int* out_index,
                                 bool* out_mode_toggle) {
    if (!layout || !point || !out_index || !out_mode_toggle) {
        return false;
    }
    *out_index = -1;
    *out_mode_toggle = false;
    SDL_Rect clip_rect = spec_body_clip_rect(&layout->body_rect);
    if (!SDL_PointInRect(point, &clip_rect)) {
        return false;
    }
    for (int i = 0; i < layout->widget_count; ++i) {
        const FxSpecWidget* widget = &layout->widgets[i];
        if (!rect_intersects(&widget->rect, &clip_rect)) {
            continue;
        }
        if (widget->mode_rect.w > 0 && widget->mode_rect.h > 0 &&
            SDL_PointInRect(point, &widget->mode_rect)) {
            *out_index = i;
            *out_mode_toggle = true;
            return true;
        }
        if (SDL_PointInRect(point, &widget->control_rect)) {
            *out_index = i;
            *out_mode_toggle = false;
            return true;
        }
    }
    return false;
}

float effects_panel_spec_value_from_point(const AppState* state,
                                          const FxSlotUIState* slot,
                                          const EffectsSpecPanelLayout* layout,
                                          int widget_index,
                                          int mouse_x,
                                          int mouse_y) {
    if (!state || !slot || !layout) {
        return 0.0f;
    }
    if (widget_index < 0 || widget_index >= layout->widget_count) {
        return 0.0f;
    }
    const FxSpecWidget* widget = &layout->widgets[widget_index];
    const EffectsPanelState* panel = &state->effects_panel;
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info) {
        return 0.0f;
    }
    const EffectParamSpec* spec = &info->param_specs[widget->param_index];
    FxParamMode mode = slot->param_mode[widget->param_index];
    float t = 0.0f;
    if (widget->type == FX_SPEC_WIDGET_KNOB) {
        if (widget->control_rect.h <= 0) {
            return 0.0f;
        }
        t = 1.0f - ((float)(mouse_y - widget->control_rect.y) / (float)widget->control_rect.h);
    } else {
        if (widget->control_rect.w <= 0) {
            return 0.0f;
        }
        t = (float)(mouse_x - widget->control_rect.x) / (float)widget->control_rect.w;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        float beat_min = 0.0f;
        float beat_max = 0.0f;
        if (!fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max)) {
            return fx_param_map_ui_to_value(spec, t);
        }
        float value = beat_min + t * (beat_max - beat_min);
        value = fx_param_quantize_beats(value);
        if (value < beat_min) value = beat_min;
        if (value > beat_max) value = beat_max;
        return value;
    }
    return fx_param_map_ui_to_value(spec, t);
}
