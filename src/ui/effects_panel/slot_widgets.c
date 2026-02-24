#include "ui/effects_panel_widgets.h"
#include "ui/font.h"

#include <math.h>

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
    SDL_SetRenderDrawColor(renderer, 46, 50, 62, 255);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, 90, 95, 110, 255);
    SDL_RenderDrawRect(renderer, rect);
    int fill_w = (int)lroundf((float)(rect->w - 2) * t);
    SDL_Rect fill = {rect->x + 1, rect->y + 1, fill_w, rect->h - 2};
    SDL_SetRenderDrawColor(renderer, 220, 120, 120, 220);
    SDL_RenderFillRect(renderer, &fill);
}

// effects_slot_draw_slider renders a slider track, fill, and handle.
void effects_slot_draw_slider(SDL_Renderer* renderer,
                              const SDL_Rect* rect,
                              float t) {
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

// effects_slot_draw_remove_button renders the remove button for a slot.
void effects_slot_draw_remove_button(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     bool highlighted) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color fill = {50, 55, 66, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color active = {130, 90, 90, 255};
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
    SDL_Color text = {210, 210, 220, 255};
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
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color fill_on = {50, 55, 66, 255};
    SDL_Color fill_off = {180, 60, 60, 220};
    if (highlighted) {
        fill_on = (SDL_Color){80, 100, 130, 255};
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
    float scale = 1.1f;
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - ui_measure_text_width(label, scale)) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}
