#include "ui/effects_panel.h"

#include "app_state.h"
#include "ui/font.h"

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

static void draw_list_toggle(SDL_Renderer* renderer, const SDL_Rect* rect, bool enabled) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color fill_off = {180, 60, 60, 220};
    if (!enabled) {
        SDL_SetRenderDrawColor(renderer, fill_off.r, fill_off.g, fill_off.b, fill_off.a);
        SDL_RenderFillRect(renderer, rect);
    }
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
}

void effects_panel_render_list(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color label_color = {210, 210, 220, 255};
    SDL_Color text_dim = {150, 160, 180, 255};

    SDL_Rect list_rect = layout->list_rect;
    SDL_Rect detail_rect = layout->detail_rect;

    SDL_SetRenderDrawColor(renderer, 30, 32, 40, 255);
    SDL_RenderFillRect(renderer, &list_rect);
    SDL_SetRenderDrawColor(renderer, 70, 75, 92, 255);
    SDL_RenderDrawRect(renderer, &list_rect);

    SDL_SetRenderDrawColor(renderer, 26, 28, 34, 255);
    SDL_RenderFillRect(renderer, &detail_rect);
    SDL_SetRenderDrawColor(renderer, 70, 75, 92, 255);
    SDL_RenderDrawRect(renderer, &detail_rect);

    for (int i = 0; i < layout->list_row_count && i < panel->chain_count; ++i) {
        SDL_Rect row = layout->list_row_rects[i];
        bool selected = (panel->selected_slot_index == i);
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 90, 120, 170, 180);
            SDL_RenderFillRect(renderer, &row);
        }
        SDL_SetRenderDrawColor(renderer, 70, 75, 92, 200);
        SDL_RenderDrawRect(renderer, &row);

        const FxSlotUIState* slot = &panel->chain[i];
        const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
        const char* name = info ? info->name : "Effect";
        SDL_Color text_color = slot->enabled ? label_color : text_dim;
        int text_y = row.y + (row.h - ui_font_line_height(1.3f)) / 2;
        ui_draw_text(renderer, row.x + 6, text_y, name, text_color, 1.3f);
        draw_list_toggle(renderer, &layout->list_toggle_rects[i], slot->enabled);
    }

    if (panel->chain_count == 0) {
        ui_draw_text(renderer,
                     list_rect.x + FX_PANEL_LIST_PAD,
                     list_rect.y + FX_PANEL_LIST_PAD,
                     "No effects yet.",
                     text_dim,
                     1.3f);
    }

    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        int open_index = panel->list_open_slot_index;
        SDL_Rect slot_rect = detail_rect;
        EffectsSlotLayout slot_layout;
        SDL_zero(slot_layout);
        effects_slot_compute_layout((EffectsPanelState*)panel,
                                    open_index,
                                    &slot_rect,
                                    FX_PANEL_HEADER_HEIGHT,
                                    FX_PANEL_INNER_MARGIN,
                                    FX_PANEL_PARAM_GAP,
                                    &slot_layout);
        effects_slot_render(renderer,
                            state,
                            open_index,
                            &slot_layout,
                            false,
                            false,
                            false,
                            label_color,
                            text_dim);
    } else {
        ui_draw_text(renderer,
                     detail_rect.x + 12,
                     detail_rect.y + 12,
                     "Double-click an effect to open it.",
                     text_dim,
                     1.3f);
    }
}
