#include "ui/daw_ui_button.h"

static CoreThemeColor daw_ui_button_core_color(SDL_Color color) {
    CoreThemeColor out;
    out.r = color.r;
    out.g = color.g;
    out.b = color.b;
    out.a = color.a;
    return out;
}

static SDL_Color daw_ui_button_sdl_color(CoreThemeColor color) {
    return (SDL_Color){color.r, color.g, color.b, color.a};
}

static int daw_ui_button_fill_edge(SDL_Renderer* renderer,
                                   int x,
                                   int y,
                                   int w,
                                   int h,
                                   SDL_Color color) {
    SDL_Rect rect = {x, y, w, h};
    if (!renderer || w <= 0 || h <= 0) {
        return 0;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
    return 1;
}

static int daw_ui_button_draw_outline(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      SDL_Color color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return 0;
    }
    if (!daw_ui_button_fill_edge(renderer, rect->x, rect->y, rect->w, 1, color)) {
        return 0;
    }
    if (rect->h > 1 &&
        !daw_ui_button_fill_edge(renderer, rect->x, rect->y + rect->h - 1, rect->w, 1, color)) {
        return 0;
    }
    if (rect->h > 2 &&
        !daw_ui_button_fill_edge(renderer, rect->x, rect->y + 1, 1, rect->h - 2, color)) {
        return 0;
    }
    if (rect->w > 1 && rect->h > 2 &&
        !daw_ui_button_fill_edge(renderer, rect->x + rect->w - 1, rect->y + 1, 1, rect->h - 2, color)) {
        return 0;
    }
    return 1;
}

void daw_ui_button_spec_init(DawUiButtonSpec* spec, const char* label) {
    kit_ui_button_spec_init(spec, label);
}

void daw_ui_button_state_init(DawUiButtonState* state) {
    kit_ui_button_state_init(state);
}

int daw_ui_button_style_resolve(const DawThemePalette* palette,
                                const DawUiButtonSpec* spec,
                                DawUiButtonStyle* out_style) {
    KitUiButtonTheme theme;
    if (!palette || !spec || !out_style) {
        return 1;
    }
    theme.idle_fill = daw_ui_button_core_color(palette->control_fill);
    theme.selected_fill = daw_ui_button_core_color(palette->control_active_fill);
    theme.hover_fill = daw_ui_button_core_color(palette->control_hover_fill);
    theme.positive_fill = daw_ui_button_core_color(palette->control_active_fill);
    theme.outline_idle = daw_ui_button_core_color(palette->control_border);
    theme.outline_highlight = daw_ui_button_core_color(palette->pane_highlight_border);
    theme.text_primary = daw_ui_button_core_color(palette->text_primary);
    theme.text_muted = daw_ui_button_core_color(palette->text_muted);
    return kit_ui_button_style_resolve(&theme, spec, out_style) != 0 ? 0 : 1;
}

int daw_ui_button_draw_frame(SDL_Renderer* renderer,
                             const SDL_Rect* rect,
                             const DawUiButtonStyle* style) {
    SDL_Color fill;
    SDL_Color outline;

    if (!renderer || !rect || !style || rect->w <= 0 || rect->h <= 0) {
        return 1;
    }
    fill = daw_ui_button_sdl_color(style->fill);
    outline = daw_ui_button_sdl_color(style->outline);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    if (!daw_ui_button_draw_outline(renderer, rect, outline)) {
        return 1;
    }
    return 0;
}
