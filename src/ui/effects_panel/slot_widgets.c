#include "ui/effects_panel_widgets.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>

typedef struct EffectsSlotWidgetTheme {
    SDL_Color track;
    SDL_Color track_border;
    SDL_Color fill;
    SDL_Color handle;
    SDL_Color remove_fill;
    SDL_Color remove_active;
    SDL_Color toggle_on;
    SDL_Color toggle_hover;
    SDL_Color toggle_off;
    SDL_Color mode_base;
    SDL_Color mode_active;
    SDL_Color gr_bg;
    SDL_Color gr_fill;
    SDL_Color text;
} EffectsSlotWidgetTheme;

static void resolve_slot_widget_theme(EffectsSlotWidgetTheme* out) {
    if (!out) {
        return;
    }
    if (daw_shared_theme_resolve_palette(&(DawThemePalette){0})) {
        DawThemePalette palette = {0};
        daw_shared_theme_resolve_palette(&palette);
        out->track = palette.slider_track;
        out->track_border = palette.control_border;
        out->fill = palette.selection_fill;
        out->handle = palette.slider_handle;
        out->remove_fill = palette.control_fill;
        out->remove_active = palette.accent_error;
        out->toggle_on = palette.control_fill;
        out->toggle_hover = palette.pane_highlight_border;
        out->toggle_off = palette.accent_error;
        out->mode_base = palette.control_fill;
        out->mode_active = palette.control_active_fill;
        out->gr_bg = palette.control_fill;
        out->gr_fill = palette.accent_error;
        out->text = palette.text_primary;
        return;
    }
    *out = (EffectsSlotWidgetTheme){
        .track = {70, 74, 86, 255},
        .track_border = {90, 95, 110, 255},
        .fill = {120, 180, 255, 200},
        .handle = {180, 210, 255, 255},
        .remove_fill = {50, 55, 66, 255},
        .remove_active = {130, 90, 90, 255},
        .toggle_on = {50, 55, 66, 255},
        .toggle_hover = {80, 100, 130, 255},
        .toggle_off = {180, 60, 60, 220},
        .mode_base = {52, 56, 64, 255},
        .mode_active = {120, 180, 255, 230},
        .gr_bg = {46, 50, 62, 255},
        .gr_fill = {220, 120, 120, 220},
        .text = {220, 230, 240, 255},
    };
}

// clampf clamps a float to a range.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// effects_slot_draw_gr_meter renders a compact gain reduction meter.
void effects_slot_draw_gr_meter(SDL_Renderer* renderer,
                                const SDL_Rect* rect,
                                float gr_db) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    float reduction = -gr_db;
    if (reduction < 0.0f) reduction = 0.0f;
    if (reduction > 24.0f) reduction = 24.0f;
    float t = reduction / 24.0f;
    EffectsSlotWidgetTheme theme = {0};
    resolve_slot_widget_theme(&theme);
    SDL_SetRenderDrawColor(renderer, theme.gr_bg.r, theme.gr_bg.g, theme.gr_bg.b, theme.gr_bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, theme.track_border.r, theme.track_border.g, theme.track_border.b, theme.track_border.a);
    SDL_RenderDrawRect(renderer, rect);
    int fill_w = (int)lroundf((float)(rect->w - 2) * t);
    SDL_Rect fill = {rect->x + 1, rect->y + 1, fill_w, rect->h - 2};
    SDL_SetRenderDrawColor(renderer, theme.gr_fill.r, theme.gr_fill.g, theme.gr_fill.b, theme.gr_fill.a);
    SDL_RenderFillRect(renderer, &fill);
}

// effects_slot_draw_slider renders a slider track, fill, and handle.
void effects_slot_draw_slider(SDL_Renderer* renderer,
                              const SDL_Rect* rect,
                              float t) {
    if (!renderer || !rect) {
        return;
    }
    EffectsSlotWidgetTheme theme = {0};
    resolve_slot_widget_theme(&theme);
    SDL_Color track = theme.track;
    SDL_Color track_border = theme.track_border;
    SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Rect fill_rect = *rect;
    fill_rect.w = (int)roundf(clampf(t, 0.0f, 1.0f) * (float)rect->w);
    SDL_Color fill_color = theme.fill;
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
    SDL_SetRenderDrawColor(renderer, theme.handle.r, theme.handle.g, theme.handle.b, theme.handle.a);
    SDL_RenderFillRect(renderer, &handle);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle);
}

// effects_slot_draw_remove_button renders the remove button for a slot.
void effects_slot_draw_remove_button(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     bool highlighted) {
    if (!renderer || !rect) {
        return;
    }
    EffectsSlotWidgetTheme theme = {0};
    resolve_slot_widget_theme(&theme);
    SDL_Color fill = theme.remove_fill;
    SDL_Color border = theme.track_border;
    SDL_Color active = theme.remove_active;
    if (highlighted) {
        fill = active;
    }
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    float scale = rect->h >= 18 ? 1.5f : 1.2f;
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - ui_measure_text_width("-", scale)) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    SDL_Color text = theme.text;
    ui_draw_text(renderer, text_x, text_y, "-", text, scale);
}

// effects_slot_draw_enable_toggle renders the enabled/disabled toggle.
void effects_slot_draw_enable_toggle(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     bool enabled,
                                     bool highlighted) {
    if (!renderer || !rect) {
        return;
    }
    EffectsSlotWidgetTheme theme = {0};
    resolve_slot_widget_theme(&theme);
    SDL_Color border = theme.track_border;
    SDL_Color fill_on = theme.toggle_on;
    SDL_Color fill_off = theme.toggle_off;
    if (highlighted) {
        border = theme.toggle_hover;
    }
    SDL_Color fill = enabled ? fill_on : fill_off;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    if (!enabled) {
        SDL_SetRenderDrawColor(renderer, fill_off.r, fill_off.g, fill_off.b, fill_off.a);
        SDL_RenderFillRect(renderer, rect);
    }
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
}

// effects_slot_draw_mode_toggle renders the native/beat mode selector.
void effects_slot_draw_mode_toggle(SDL_Renderer* renderer,
                                   const SDL_Rect* rect,
                                   FxParamMode mode) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    EffectsSlotWidgetTheme theme = {0};
    resolve_slot_widget_theme(&theme);
    SDL_Color base = theme.mode_base;
    SDL_Color active = theme.mode_active;
    SDL_Color border = theme.track_border;
    SDL_Color text = theme.text;
    SDL_Color fill = (mode == FX_PARAM_MODE_NATIVE) ? base : active;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    const char* label = (mode == FX_PARAM_MODE_NATIVE) ? "N" : "B";
    float scale = 1.1f;
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - ui_measure_text_width(label, scale)) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}
