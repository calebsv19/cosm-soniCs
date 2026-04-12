#include "ui/effects_panel_overlay.h"

#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"

#include <stdio.h>

#define FX_PANEL_OVERLAY_WIDTH 260
#define FX_PANEL_OVERLAY_HEADER_HEIGHT 24
#define FX_PANEL_OVERLAY_PADDING 8

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static int fx_panel_overlay_header_height(void) {
    int needed = ui_font_line_height(2.0f) + 8;
    return max_int(FX_PANEL_OVERLAY_HEADER_HEIGHT, needed);
}

static int fx_panel_overlay_item_height(void) {
    int needed = ui_font_line_height(1.5f) + 8;
    return max_int(FX_PANEL_DROPDOWN_ITEM_HEIGHT, needed);
}

static int fx_panel_overlay_padding(void) {
    int needed = ui_font_line_height(1.0f) / 3 + 4;
    return max_int(FX_PANEL_OVERLAY_PADDING, needed);
}

static void resolve_effects_panel_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .timeline_fill = {28, 30, 38, 255},
            .inspector_fill = {26, 26, 32, 240},
            .pane_border = {90, 95, 110, 255},
            .control_fill = {48, 52, 62, 255},
            .control_hover_fill = {70, 90, 120, 255},
            .control_active_fill = {120, 160, 220, 255},
            .control_border = {90, 95, 110, 255},
            .text_primary = {220, 220, 230, 255},
            .text_muted = {160, 170, 190, 255},
            .selection_fill = {80, 110, 160, 255},
            .slider_track = {52, 56, 64, 220},
            .slider_handle = {120, 160, 220, 255}
        };
    }
}

void effects_panel_compute_overlay_layout(const AppState* state,
                                          const EffectsPanelState* panel,
                                          const SDL_Rect* mixer_rect,
                                          int content_x,
                                          int content_w,
                                          const SDL_Rect* dropdown_button_rect,
                                          EffectsPanelLayout* layout) {
    if (!state || !panel || !mixer_rect || !dropdown_button_rect || !layout) {
        return;
    }

    layout->overlay_visible = false;
    layout->overlay_item_count = 0;
    layout->overlay_total_items = 0;
    layout->overlay_visible_count = 0;
    layout->overlay_has_scrollbar = false;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_CLOSED) {
        return;
    }

    int overlay_total_items = 0;
    bool overlay_effect_layer = false;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        overlay_total_items = panel->category_count;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        overlay_effect_layer = true;
        if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
            overlay_total_items = panel->categories[panel->active_category_index].type_count;
        }
    }
    int overlay_header_h = fx_panel_overlay_header_height();
    int overlay_item_h = fx_panel_overlay_item_height();
    int overlay_pad = fx_panel_overlay_padding();
    int overlay_item_gap_y = max_int(2, overlay_pad / 2);
    const int scrollbar_w = 8;
    const char* overlay_title = "Add Effect";
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS && panel->active_category_index >= 0 &&
        panel->active_category_index < panel->category_count) {
        overlay_title = panel->categories[panel->active_category_index].name;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        overlay_title = "Select Category";
    }
    int title_w = ui_measure_text_width(overlay_title, 2.0f);
    int back_w = overlay_effect_layer ? (ui_measure_text_width("<", 2.0f) + 12) : 0;
    int header_left = overlay_pad + (overlay_effect_layer ? back_w + 6 : 0);
    int overlay_header_min_w = header_left + title_w + overlay_pad + 6;
    int overlay_item_text_w = 0;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        char item_label[96];
        for (int i = 0; i < panel->category_count; ++i) {
            const FxCategoryUIInfo* cat = &panel->categories[i];
            snprintf(item_label, sizeof(item_label), "%s (%d)", cat->name, cat->type_count);
            int w = ui_measure_text_width(item_label, 1.5f);
            if (w > overlay_item_text_w) {
                overlay_item_text_w = w;
            }
        }
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS &&
               panel->active_category_index >= 0 &&
               panel->active_category_index < panel->category_count) {
        const FxCategoryUIInfo* cat = &panel->categories[panel->active_category_index];
        for (int i = 0; i < cat->type_count; ++i) {
            int type_index = cat->type_indices[i];
            if (type_index >= 0 && type_index < panel->type_count) {
                int w = ui_measure_text_width(panel->types[type_index].name, 1.5f);
                if (w > overlay_item_text_w) {
                    overlay_item_text_w = w;
                }
            }
        }
    }
    int empty_msg_w = ui_measure_text_width("No effects in this category.", 1.5f) + overlay_pad * 2;
    int overlay_min_w = overlay_pad * 2 + overlay_item_text_w + scrollbar_w + 12;
    if (overlay_min_w < overlay_header_min_w) {
        overlay_min_w = overlay_header_min_w;
    }
    if (overlay_min_w < empty_msg_w) {
        overlay_min_w = empty_msg_w;
    }
    int overlay_w = max_int(FX_PANEL_OVERLAY_WIDTH, overlay_min_w);
    if (overlay_w > content_w) {
        overlay_w = content_w;
    }
    int overlay_x = dropdown_button_rect->x;
    if (overlay_x + overlay_w > content_x + content_w) {
        overlay_x = content_x + content_w - overlay_w;
    }
    int overlay_y = dropdown_button_rect->y + dropdown_button_rect->h + 6;
    int available_h = (mixer_rect->y + mixer_rect->h) - FX_PANEL_MARGIN - overlay_y;
    if (available_h < overlay_header_h + overlay_item_h + overlay_pad) {
        return;
    }

    int max_visible_items = (available_h - overlay_header_h - overlay_pad) / overlay_item_h;
    if (max_visible_items <= 0) {
        return;
    }
    if (overlay_total_items <= 0) {
        layout->overlay_visible = false;
        return;
    }

    int display_capacity = overlay_total_items;
    if (display_capacity < 1) display_capacity = 1;
    if (display_capacity > max_visible_items) {
        display_capacity = max_visible_items;
    }

    int max_scroll = overlay_total_items - display_capacity;
    if (max_scroll < 0) max_scroll = 0;
    int scroll_index = panel->overlay_scroll_index;
    if (scroll_index > max_scroll) {
        scroll_index = max_scroll;
    }
    if (scroll_index < 0) {
        scroll_index = 0;
    }

    int first_index = scroll_index;
    int remaining = overlay_total_items - first_index;
    if (remaining < 0) remaining = 0;
    int visible_items = remaining;
    if (visible_items > display_capacity) {
        visible_items = display_capacity;
    }
    if (visible_items < 0) {
        visible_items = 0;
    }

    SDL_Rect overlay_rect = {
        overlay_x,
        overlay_y,
        overlay_w,
        overlay_header_h + display_capacity * overlay_item_h + overlay_item_gap_y + overlay_pad
    };

    layout->overlay_visible = true;
    layout->overlay_rect = overlay_rect;
    layout->overlay_header_rect = (SDL_Rect){overlay_rect.x, overlay_rect.y, overlay_rect.w, overlay_header_h};
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        int back_h = overlay_header_h - max_int(6, overlay_header_h / 4);
        int back_min_h = ui_font_line_height(2.0f) + 2;
        back_h = max_int(back_h, back_min_h);
        back_h = min_int(back_h, overlay_header_h);
        layout->overlay_back_rect = (SDL_Rect){
            overlay_rect.x + overlay_pad,
            overlay_rect.y + (overlay_header_h - back_h) / 2,
            back_w,
            back_h
        };
    } else {
        layout->overlay_back_rect = (SDL_Rect){0, 0, 0, 0};
    }

    layout->overlay_total_items = overlay_total_items;
    layout->overlay_visible_count = visible_items;
    layout->overlay_item_count = visible_items;

    int item_y = overlay_rect.y + overlay_header_h + overlay_item_gap_y;
    bool has_scrollbar = (overlay_total_items > display_capacity);
    layout->overlay_has_scrollbar = has_scrollbar;
    int item_width = overlay_rect.w - 2 * overlay_pad - (has_scrollbar ? (scrollbar_w + 4) : 0);
    if (item_width < 80) item_width = overlay_rect.w - 2 * overlay_pad;

    for (int i = 0; i < visible_items; ++i) {
        layout->overlay_item_rects[i] = (SDL_Rect){
            overlay_rect.x + overlay_pad,
            item_y,
            item_width,
            overlay_item_h
        };
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = first_index + i;
            layout->overlay_item_order[i] = (cat_index >= 0 && cat_index < panel->category_count) ? cat_index : -1;
        } else {
            const FxCategoryUIInfo* cat = NULL;
            if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
                cat = &panel->categories[panel->active_category_index];
            }
            int type_index = -1;
            if (cat) {
                int cat_offset = first_index + i;
                if (cat_offset >= 0 && cat_offset < cat->type_count) {
                    type_index = cat->type_indices[cat_offset];
                }
            }
            layout->overlay_item_order[i] = type_index;
        }
        item_y += overlay_item_h;
    }

    if (has_scrollbar) {
        int track_x = overlay_rect.x + overlay_rect.w - overlay_pad - scrollbar_w;
        int track_y = overlay_rect.y + overlay_header_h + overlay_item_gap_y;
        int track_h = display_capacity * overlay_item_h;
        layout->overlay_scrollbar_track = (SDL_Rect){track_x, track_y, scrollbar_w, track_h};
        float visible_ratio = (overlay_total_items > 0) ? (float)visible_items / (float)overlay_total_items : 1.0f;
        if (visible_ratio < 0.05f) visible_ratio = 0.05f;
        int thumb_h = (int)(track_h * visible_ratio);
        if (thumb_h < max_int(10, overlay_item_h / 2)) thumb_h = max_int(10, overlay_item_h / 2);
        int max_scroll_index = overlay_total_items - visible_items;
        float scroll_ratio = (max_scroll_index > 0) ? (float)scroll_index / (float)max_scroll_index : 0.0f;
        if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
        int thumb_y = track_y + (int)((track_h - thumb_h) * scroll_ratio);
        if (thumb_y > track_y + track_h - thumb_h) thumb_y = track_y + track_h - thumb_h;
        layout->overlay_scrollbar_thumb = (SDL_Rect){track_x, thumb_y, scrollbar_w, thumb_h};
    } else {
        layout->overlay_scrollbar_track = (SDL_Rect){0,0,0,0};
        layout->overlay_scrollbar_thumb = (SDL_Rect){0,0,0,0};
    }
}

void effects_panel_render_overlay(SDL_Renderer* renderer,
                                  const AppState* state,
                                  const EffectsPanelLayout* layout) {
    DawThemePalette theme = {0};
    if (!renderer || !state || !layout || !layout->overlay_visible) {
        return;
    }
    resolve_effects_panel_theme(&theme);
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color bg = theme.inspector_fill;
    SDL_Color border = theme.pane_border;
    SDL_Color header_bg = theme.control_fill;
    SDL_Color label = theme.text_primary;
    SDL_Color hover = theme.selection_fill;

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_rect);

    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_header_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_header_rect);

    const char* title = "Add Effect";
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS && panel->active_category_index >= 0 &&
        panel->active_category_index < panel->category_count) {
        title = panel->categories[panel->active_category_index].name;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        title = "Select Category";
    }
    int overlay_pad = fx_panel_overlay_padding();
    int header_text_y = layout->overlay_header_rect.y +
                        (layout->overlay_header_rect.h - ui_font_line_height(2.0f)) / 2;
    int header_text_x = layout->overlay_header_rect.x + overlay_pad;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS && layout->overlay_back_rect.w > 0) {
        header_text_x = layout->overlay_back_rect.x + layout->overlay_back_rect.w + 6;
    }
    int header_text_w = (layout->overlay_header_rect.x + layout->overlay_header_rect.w - overlay_pad) - header_text_x;
    if (header_text_w > 0) {
        ui_draw_text_clipped(renderer, header_text_x, header_text_y, title, label, 2.0f, header_text_w);
    }

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        SDL_Color back_color = theme.control_hover_fill;
        SDL_SetRenderDrawColor(renderer, back_color.r, back_color.g, back_color.b, back_color.a);
        SDL_RenderFillRect(renderer, &layout->overlay_back_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_back_rect);
        int back_text_y = layout->overlay_back_rect.y +
                          (layout->overlay_back_rect.h - ui_font_line_height(2.0f)) / 2;
        int back_text_x = layout->overlay_back_rect.x + (layout->overlay_back_rect.w - ui_measure_text_width("<", 2.0f)) / 2;
        ui_draw_text(renderer,
                     back_text_x,
                     back_text_y,
                     "<",
                     label,
                     2.0f);
    }

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        bool invalid_cat = (panel->active_category_index < 0 || panel->active_category_index >= panel->category_count);
        bool empty_cat = false;
        if (!invalid_cat) {
            const FxCategoryUIInfo* cat = &panel->categories[panel->active_category_index];
            empty_cat = (cat->type_count == 0);
        }
        if (invalid_cat || empty_cat || layout->overlay_item_count == 0) {
            const char* msg = "No effects in this category.";
            ui_draw_text(renderer,
                         layout->overlay_rect.x + overlay_pad,
                         layout->overlay_rect.y + layout->overlay_header_rect.h + 12,
                         msg,
                         label,
                         1.5f);
            return;
        }
    }

    for (int i = 0; i < layout->overlay_item_count; ++i) {
        SDL_Rect item_rect = layout->overlay_item_rects[i];
        bool is_hover = false;
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            if (layout->overlay_item_order[i] == panel->hovered_category_index) {
                is_hover = true;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            if (layout->overlay_item_order[i] == panel->hovered_effect_index) {
                is_hover = true;
            }
        }
        if (is_hover) {
            SDL_SetRenderDrawColor(renderer, hover.r, hover.g, hover.b, hover.a);
            SDL_RenderFillRect(renderer, &item_rect);
        }

        const char* item_label = "";
        char buffer[96];
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = layout->overlay_item_order[i];
            if (cat_index >= 0 && cat_index < panel->category_count) {
                const FxCategoryUIInfo* cat = &panel->categories[cat_index];
                snprintf(buffer, sizeof(buffer), "%s (%d)", cat->name, cat->type_count);
                item_label = buffer;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            int type_index = layout->overlay_item_order[i];
            if (type_index >= 0 && type_index < panel->type_count) {
                item_label = panel->types[type_index].name;
            }
        }
        int item_text_y = item_rect.y + (item_rect.h - ui_font_line_height(1.5f)) / 2;
        int item_text_x = item_rect.x + 8;
        int item_text_w = item_rect.w - 16;
        if (item_text_w > 0) {
            ui_draw_text_clipped(renderer, item_text_x, item_text_y, item_label, label, 1.5f, item_text_w);
        }
    }

    if (layout->overlay_has_scrollbar) {
        SDL_Color track = theme.slider_track;
        SDL_Color thumb = theme.slider_handle;
        SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, thumb.r, thumb.g, thumb.b, thumb.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_thumb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_thumb);
    }
}
