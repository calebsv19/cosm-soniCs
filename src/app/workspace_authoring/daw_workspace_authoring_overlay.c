#include "app/workspace_authoring/daw_workspace_authoring_overlay.h"

#include <stdio.h>
#include <string.h>

#include "core_font.h"
#include "core_theme.h"
#include "kit_workspace_authoring_ui.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

static SDL_Color daw_authoring_alpha(SDL_Color color, Uint8 alpha) {
    color.a = alpha;
    return color;
}

static SDL_Rect daw_authoring_rect_from_core(CorePaneRect rect) {
    SDL_Rect out = {
        (int)(rect.x + 0.5f),
        (int)(rect.y + 0.5f),
        (int)(rect.width + 0.5f),
        (int)(rect.height + 0.5f)
    };
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static SDL_Rect daw_authoring_rect_from_kit(KitRenderRect rect) {
    SDL_Rect out = {
        (int)(rect.x + 0.5f),
        (int)(rect.y + 0.5f),
        (int)(rect.width + 0.5f),
        (int)(rect.height + 0.5f)
    };
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static int daw_authoring_rect_visible(const SDL_Rect *rect) {
    return rect && rect->w > 1 && rect->h > 1;
}

static void daw_authoring_draw_rect(SDL_Renderer *renderer,
                                    const SDL_Rect *rect,
                                    SDL_Color fill,
                                    SDL_Color border) {
    if (!renderer || !daw_authoring_rect_visible(rect)) return;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
}

static const char *daw_authoring_pane_module_label(int pane_index) {
    switch (pane_index) {
        case 0:
            return "module: transport / runtime controls";
        case 1:
            return "module: timeline canvas / clip lanes";
        case 2:
            return "module: inspector / effects panel";
        case 3:
            return "module: media library browser";
        default:
            return "module: unbound";
    }
}

static const char *daw_authoring_pane_title(const Pane *pane, int pane_index) {
    if (pane && pane->title && pane->title[0]) return pane->title;
    switch (pane_index) {
        case 0:
            return "TRANSPORT";
        case 1:
            return "TIMELINE";
        case 2:
            return "INSPECTOR";
        case 3:
            return "LIBRARY";
        default:
            return "PANE";
    }
}

static void daw_authoring_draw_pane_label(SDL_Renderer *renderer,
                                          const SDL_Rect *pane_rect,
                                          int pane_index,
                                          const char *pane_title,
                                          const DawThemePalette *theme) {
    SDL_Rect tag;
    SDL_Rect module_tag;
    SDL_Color tag_fill;
    SDL_Color tag_border;
    SDL_Color text;
    SDL_Color muted;
    char label[96];
    const int pad = 8;
    const int tag_h = 24;
    int label_w;

    if (!renderer || !pane_rect || !theme || !daw_authoring_rect_visible(pane_rect)) return;

    snprintf(label, sizeof(label), "P%d %s", pane_index + 1, pane_title ? pane_title : "PANE");
    label_w = ui_measure_text_width(label, 1.0f);
    if (label_w < 0) label_w = 0;

    tag = (SDL_Rect){
        pane_rect->x + pad,
        pane_rect->y + pad,
        label_w + 18,
        tag_h
    };
    if (tag.w < 132) tag.w = 132;
    if (tag.w > pane_rect->w - (pad * 2)) tag.w = pane_rect->w - (pad * 2);
    if (!daw_authoring_rect_visible(&tag)) return;

    tag_fill = daw_authoring_alpha(theme->control_fill, 238u);
    tag_border = daw_authoring_alpha(theme->accent_primary, 235u);
    text = theme->text_primary;
    muted = theme->text_muted;

    daw_authoring_draw_rect(renderer, &tag, tag_fill, tag_border);
    ui_draw_text_clipped(renderer,
                         tag.x + 7,
                         tag.y + 4,
                         label,
                         text,
                         1.0f,
                         tag.w - 12);

    if (pane_rect->h < 68 || pane_rect->w < 180) return;
    module_tag = tag;
    module_tag.y += tag.h + 4;
    module_tag.w = 300;
    if (module_tag.w > pane_rect->w - (pad * 2)) module_tag.w = pane_rect->w - (pad * 2);
    if (!daw_authoring_rect_visible(&module_tag)) return;

    daw_authoring_draw_rect(renderer,
                            &module_tag,
                            daw_authoring_alpha(theme->control_fill, 206u),
                            daw_authoring_alpha(theme->pane_border, 216u));
    ui_draw_text_clipped(renderer,
                         module_tag.x + 7,
                         module_tag.y + 4,
                         daw_authoring_pane_module_label(pane_index),
                         muted,
                         1.0f,
                         module_tag.w - 12);
}

static void daw_authoring_draw_pane_inventory(SDL_Renderer *renderer,
                                              AppState *state,
                                              const DawThemePalette *theme) {
    SDL_Color fill;
    SDL_Color border;

    if (!renderer || !state || !theme) return;
    fill = daw_authoring_alpha(theme->pane_highlight_fill, 48u);
    border = daw_authoring_alpha(theme->pane_highlight_border, 238u);

    for (int i = 0; i < state->pane_count; ++i) {
        const Pane *pane = &state->panes[i];
        if (!pane->visible || !daw_authoring_rect_visible(&pane->rect)) continue;
        daw_authoring_draw_rect(renderer, &pane->rect, fill, border);
        daw_authoring_draw_pane_label(renderer,
                                      &pane->rect,
                                      i,
                                      daw_authoring_pane_title(pane, i),
                                      theme);
    }
}

static void daw_authoring_draw_status(SDL_Renderer *renderer,
                                      AppState *state,
                                      const DawThemePalette *theme) {
    SDL_Rect rect;
    char line[160];

    if (!renderer || !state || !theme || state->window_width <= 0) return;
    rect = (SDL_Rect){12, 46, 430, 74};
    if (rect.w > state->window_width - 24) rect.w = state->window_width - 24;
    if (!daw_authoring_rect_visible(&rect)) return;

    daw_authoring_draw_rect(renderer,
                            &rect,
                            daw_authoring_alpha(theme->control_fill, 226u),
                            daw_authoring_alpha(theme->pane_border, 235u));
    ui_draw_text_clipped(renderer,
                         rect.x + 10,
                         rect.y + 8,
                         "Workspace Authoring: Pane",
                         theme->text_primary,
                         1.0f,
                         rect.w - 20);
    snprintf(line,
             sizeof(line),
             "panes:%d overlay:%s events:%u captured:%u",
             state->pane_count,
             daw_workspace_authoring_host_pane_overlay_active(&state->workspace_authoring)
                 ? "pane"
                 : "font/theme",
             state->workspace_authoring.consumed_event_count,
             state->workspace_authoring.captured_runtime_event_count);
    ui_draw_text_clipped(renderer, rect.x + 10, rect.y + 32, line, theme->text_muted, 1.0f, rect.w - 20);
    ui_draw_text_clipped(renderer,
                         rect.x + 10,
                         rect.y + 52,
                         "Tab cycles | Enter applies | Esc cancels",
                         theme->text_muted,
                         1.0f,
                         rect.w - 20);
}

static void daw_authoring_draw_button(SDL_Renderer *renderer,
                                      const KitWorkspaceAuthoringOverlayButton *button,
                                      const DawThemePalette *theme) {
    SDL_Rect rect;
    SDL_Color fill;
    SDL_Color border;
    SDL_Color text;

    if (!renderer || !button || !theme || !button->visible) return;

    rect = daw_authoring_rect_from_core(button->rect);
    fill = daw_authoring_alpha(theme->control_fill, 238u);
    border = daw_authoring_alpha(theme->control_border, 245u);
    text = theme->text_primary;

    if (button->id == KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_APPLY) {
        fill = daw_authoring_alpha(theme->accent_primary, 232u);
    } else if (button->id == KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_CANCEL) {
        fill = daw_authoring_alpha(theme->accent_warning, 232u);
    } else if (button->id == KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_MODE) {
        fill = daw_authoring_alpha(theme->pane_highlight_border, 228u);
    } else if (!button->enabled) {
        fill.a = 122u;
        border.a = 150u;
        text.a = 160u;
    }

    daw_authoring_draw_rect(renderer, &rect, fill, border);
    ui_draw_text_clipped(renderer,
                         rect.x + 7,
                         rect.y + 3,
                         button->label ? button->label : "",
                         text,
                         1.0f,
                         rect.w - 12);
}

static int daw_authoring_font_theme_button_selected(KitWorkspaceAuthoringFontThemeButtonId button_id,
                                                    const char *current_font_preset,
                                                    const char *current_theme_preset) {
    CoreFontPresetId font_id;
    CoreThemePresetId theme_id;
    const char *name;
    if (kit_workspace_authoring_ui_font_theme_button_font_preset_id(button_id, &font_id)) {
        name = core_font_preset_name(font_id);
        return name && current_font_preset && strcmp(name, current_font_preset) == 0;
    }
    if (kit_workspace_authoring_ui_font_theme_button_theme_preset_id(button_id, &theme_id)) {
        name = core_theme_preset_name(theme_id);
        return name && current_theme_preset && strcmp(name, current_theme_preset) == 0;
    }
    return 0;
}

static void daw_authoring_draw_font_theme_button(SDL_Renderer *renderer,
                                                 KitWorkspaceAuthoringFontThemeButtonId button_id,
                                                 KitRenderRect kit_rect,
                                                 const DawThemePalette *theme,
                                                 const char *current_font_preset,
                                                 const char *current_theme_preset) {
    SDL_Rect rect;
    SDL_Color fill;
    SDL_Color border;
    SDL_Color text;
    int selected;
    uint8_t enabled;
    if (!renderer || !theme || button_id == KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_NONE) return;

    rect = daw_authoring_rect_from_kit(kit_rect);
    enabled = kit_workspace_authoring_ui_font_theme_button_enabled(button_id);
    selected = daw_authoring_font_theme_button_selected(button_id,
                                                       current_font_preset,
                                                       current_theme_preset);
    fill = selected
        ? daw_authoring_alpha(theme->accent_primary, 232u)
        : daw_authoring_alpha(theme->control_fill, 226u);
    border = daw_authoring_alpha(selected ? theme->accent_primary : theme->control_border, 244u);
    text = theme->text_primary;
    if (!enabled) {
        fill.a = 96u;
        border.a = 110u;
        text.a = 135u;
    }

    daw_authoring_draw_rect(renderer, &rect, fill, border);
    ui_draw_text_clipped(renderer,
                         rect.x + 8,
                         rect.y + 4,
                         kit_workspace_authoring_ui_font_theme_button_label(button_id),
                         text,
                         1.0f,
                         rect.w - 14);
}

static void daw_authoring_draw_section_text(SDL_Renderer *renderer,
                                            KitRenderRect kit_rect,
                                            const char *title,
                                            const char *detail,
                                            const DawThemePalette *theme) {
    SDL_Rect rect;
    if (!renderer || !theme || !title) return;
    rect = daw_authoring_rect_from_kit(kit_rect);
    ui_draw_text_clipped(renderer,
                         rect.x + 14,
                         rect.y + 12,
                         title,
                         theme->text_primary,
                         1.0f,
                         rect.w - 28);
    if (detail && detail[0]) {
        ui_draw_text_clipped(renderer,
                             rect.x + 14,
                             rect.y + 36,
                             detail,
                             theme->text_muted,
                             1.0f,
                             rect.w - 28);
    }
}

static void daw_authoring_draw_font_theme_overlay(SDL_Renderer *renderer,
                                                  AppState *state,
                                                  const DawThemePalette *theme) {
    KitWorkspaceAuthoringFontThemeLayout layout;
    SDL_Rect screen;
    SDL_Rect panel;
    SDL_Rect section;
    SDL_Rect chip;
    SDL_Color screen_fill;
    SDL_Color section_fill;
    SDL_Color border;
    char font_preset[64] = "unknown";
    char theme_preset[64] = "unknown";
    char detail[192];
    int step;
    int pct;
    uint32_t i;
    const DawWorkspaceAuthoringHostState *host;
    if (!renderer || !state || !theme || state->window_width <= 0 || state->window_height <= 0) return;
    if (!kit_workspace_authoring_ui_font_theme_build_layout(NULL,
                                                            state->window_width,
                                                            state->window_height,
                                                            &layout)) {
        return;
    }

    host = &state->workspace_authoring;
    (void)daw_shared_font_current_preset(font_preset, sizeof(font_preset));
    (void)daw_shared_theme_current_preset(theme_preset, sizeof(theme_preset));
    step = daw_shared_font_zoom_step();
    pct = 100 + (step * 10);
    if (pct < 60) pct = 60;
    if (pct > 180) pct = 180;

    screen = (SDL_Rect){0, 0, state->window_width, state->window_height};
    panel = daw_authoring_rect_from_kit(layout.panel);
    screen_fill = daw_authoring_alpha(theme->timeline_fill, 250u);
    section_fill = daw_authoring_alpha(theme->control_fill, 208u);
    border = daw_authoring_alpha(theme->pane_border, 236u);

    SDL_SetRenderDrawColor(renderer, screen_fill.r, screen_fill.g, screen_fill.b, screen_fill.a);
    SDL_RenderFillRect(renderer, &screen);
    daw_authoring_draw_rect(renderer, &panel, screen_fill, border);

    ui_draw_text_clipped(renderer,
                         panel.x + 10,
                         panel.y + 10,
                         "Font/Theme Overlay",
                         theme->text_primary,
                         1.0f,
                         panel.w - 20);

    section = daw_authoring_rect_from_kit(layout.font_preset_section);
    daw_authoring_draw_rect(renderer, &section, section_fill, border);
    snprintf(detail, sizeof(detail), "Font Preset: %s", font_preset);
    daw_authoring_draw_section_text(renderer, layout.font_preset_section, detail, "", theme);
    for (i = 0u; i < layout.font_preset_button_count; ++i) {
        KitWorkspaceAuthoringFontThemeButtonId id =
            (KitWorkspaceAuthoringFontThemeButtonId)(
                KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_FONT_PRESET_DAW_DEFAULT + i);
        daw_authoring_draw_font_theme_button(renderer,
                                             id,
                                             layout.font_preset_buttons[i],
                                             theme,
                                             font_preset,
                                             theme_preset);
    }

    section = daw_authoring_rect_from_kit(layout.text_size_section);
    daw_authoring_draw_rect(renderer, &section, section_fill, border);
    snprintf(detail, sizeof(detail), "Text Size step:%d (%d%%)", step, pct);
    daw_authoring_draw_section_text(renderer, layout.text_size_section, detail, "", theme);
    daw_authoring_draw_font_theme_button(renderer,
                                         KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_DEC,
                                         layout.text_size_dec_button,
                                         theme,
                                         font_preset,
                                         theme_preset);
    daw_authoring_draw_font_theme_button(renderer,
                                         KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_INC,
                                         layout.text_size_inc_button,
                                         theme,
                                         font_preset,
                                         theme_preset);
    daw_authoring_draw_font_theme_button(renderer,
                                         KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_RESET,
                                         layout.text_size_reset_button,
                                         theme,
                                         font_preset,
                                         theme_preset);
    chip = daw_authoring_rect_from_kit(layout.text_size_value_chip);
    daw_authoring_draw_rect(renderer,
                            &chip,
                            daw_authoring_alpha(theme->accent_primary, 226u),
                            daw_authoring_alpha(theme->control_border, 244u));
    ui_draw_text_clipped(renderer,
                         chip.x + 8,
                         chip.y + 4,
                         detail,
                         theme->text_primary,
                         1.0f,
                         chip.w - 14);

    section = daw_authoring_rect_from_kit(layout.theme_preset_section);
    daw_authoring_draw_rect(renderer, &section, section_fill, border);
    snprintf(detail, sizeof(detail), "Theme Preset: %s", theme_preset);
    daw_authoring_draw_section_text(renderer,
                                    layout.theme_preset_section,
                                    detail,
                                    "Click a preset to preview live; Apply persistence is S4.",
                                    theme);
    for (i = 0u; i < layout.theme_preset_button_count; ++i) {
        KitWorkspaceAuthoringFontThemeButtonId id =
            (KitWorkspaceAuthoringFontThemeButtonId)(
                KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_THEME_PRESET_DAW_DEFAULT + i);
        daw_authoring_draw_font_theme_button(renderer,
                                             id,
                                             layout.theme_preset_buttons[i],
                                             theme,
                                             font_preset,
                                             theme_preset);
    }

    section = daw_authoring_rect_from_kit(layout.custom_theme_section);
    daw_authoring_draw_rect(renderer, &section, section_fill, border);
    daw_authoring_draw_section_text(
        renderer,
        layout.custom_theme_section,
        "Custom Presets",
        host->font_theme_status[0]
            ? host->font_theme_status
            : "DAW exposes custom theme slots as stubs until the theme editor is promoted.",
        theme);
    for (i = 0u; i < layout.custom_theme_button_count; ++i) {
        KitWorkspaceAuthoringFontThemeButtonId id =
            (KitWorkspaceAuthoringFontThemeButtonId)(
                KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_CUSTOM_THEME_CREATE_STUB + i);
        daw_authoring_draw_font_theme_button(renderer,
                                             id,
                                             layout.custom_theme_buttons[i],
                                             theme,
                                             font_preset,
                                             theme_preset);
    }
}

static void daw_authoring_draw_controls(SDL_Renderer *renderer,
                                        AppState *state,
                                        const DawThemePalette *theme) {
    KitWorkspaceAuthoringOverlayButton buttons[4];
    uint32_t count;

    if (!renderer || !state || !theme || state->window_width <= 0) return;
    count = kit_workspace_authoring_ui_build_overlay_buttons(
        state->window_width,
        daw_workspace_authoring_host_active(&state->workspace_authoring),
        daw_workspace_authoring_host_pane_overlay_active(&state->workspace_authoring),
        buttons,
        (uint32_t)(sizeof(buttons) / sizeof(buttons[0])));

    for (uint32_t i = 0; i < count; ++i) {
        daw_authoring_draw_button(renderer, &buttons[i], theme);
    }
}

void daw_workspace_authoring_overlay_render(SDL_Renderer *renderer, AppState *state) {
    DawThemePalette theme = {0};

    if (!renderer || !state || !daw_workspace_authoring_host_active(&state->workspace_authoring)) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(&theme)) {
        theme = (DawThemePalette){
            .pane_border = {92, 100, 116, 255},
            .pane_highlight_fill = {90, 130, 190, 255},
            .pane_highlight_border = {140, 174, 224, 255},
            .text_primary = {232, 236, 244, 255},
            .text_muted = {166, 174, 188, 255},
            .control_fill = {24, 28, 36, 255},
            .control_border = {102, 112, 132, 255},
            .accent_primary = {108, 146, 210, 255},
            .accent_warning = {190, 154, 86, 255},
        };
    }

    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
    if (daw_workspace_authoring_host_pane_overlay_active(&state->workspace_authoring)) {
        daw_authoring_draw_pane_inventory(renderer, state, &theme);
        daw_authoring_draw_status(renderer, state, &theme);
    } else if (daw_workspace_authoring_host_font_theme_overlay_active(&state->workspace_authoring)) {
        daw_authoring_draw_font_theme_overlay(renderer, state, &theme);
    }
    daw_authoring_draw_controls(renderer, state, &theme);
}
