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

// Replaces rectangular chrome with one shared rounded frame, preserving product colors.
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
    KitUiButtonAppearance appearance;
    kit_ui_button_appearance_preset(KIT_UI_BUTTON_APPEARANCE_COMPACT_ROUNDED,&appearance);
    float radius = kit_ui_corner_radius_clamp(appearance.corner_radius,rect->w,rect->h);
    SDL_SetRenderDrawColor(renderer,outline.r,outline.g,outline.b,outline.a);
    SDL_FRect outer = {rect->x,rect->y,rect->w,rect->h};
    vk_renderer_fill_rounded_rect((VkRenderer*)renderer,&outer,radius);
    KitRenderRect inner=kit_ui_rect_inset((KitRenderRect){rect->x,rect->y,rect->w,rect->h},appearance.border_thickness);
    SDL_FRect inset={inner.x,inner.y,inner.width,inner.height};
    SDL_SetRenderDrawColor(renderer,fill.r,fill.g,fill.b,fill.a);
    vk_renderer_fill_rounded_rect((VkRenderer*)renderer,&inset,
        kit_ui_corner_radius_for_inset(radius,appearance.border_thickness));
    return 0;
}
