#include "ui/effects_panel.h"
#include "ui/effects_panel_eq_detail.h"
#include "ui/effects_panel_meter_detail.h"
#include "ui/effects_panel_slot_layout.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

static void resolve_effects_list_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .timeline_fill = {30, 32, 40, 255},
            .inspector_fill = {26, 28, 34, 255},
            .pane_border = {70, 75, 92, 255},
            .control_border = {90, 95, 110, 255},
            .text_primary = {210, 210, 220, 255},
            .text_muted = {150, 160, 180, 255},
            .selection_fill = {90, 120, 170, 180},
            .control_hover_fill = {48, 56, 74, 180},
            .accent_error = {180, 60, 60, 220}
        };
    }
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

static void draw_list_toggle(SDL_Renderer* renderer,
                             const SDL_Rect* rect,
                             bool enabled,
                             const DawThemePalette* theme) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color border = theme ? theme->control_border : (SDL_Color){90, 95, 110, 255};
    SDL_Color fill_off = theme ? theme->accent_error : (SDL_Color){180, 60, 60, 220};
    if (!enabled) {
        SDL_SetRenderDrawColor(renderer, fill_off.r, fill_off.g, fill_off.b, fill_off.a);
        SDL_RenderFillRect(renderer, rect);
    }
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
}

void effects_panel_render_list(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    DawThemePalette theme = {0};
    if (!renderer || !state || !layout) {
        return;
    }
    resolve_effects_list_theme(&theme);
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color label_color = theme.text_primary;
    SDL_Color text_dim = theme.text_muted;
    float text_scale = FX_PANEL_LIST_TEXT_SCALE;

    SDL_Rect list_rect = layout->list_rect;
    SDL_Rect detail_rect = layout->detail_rect;

    SDL_SetRenderDrawColor(renderer, theme.timeline_fill.r, theme.timeline_fill.g, theme.timeline_fill.b, theme.timeline_fill.a);
    SDL_RenderFillRect(renderer, &list_rect);
    SDL_SetRenderDrawColor(renderer, theme.pane_border.r, theme.pane_border.g, theme.pane_border.b, theme.pane_border.a);
    SDL_RenderDrawRect(renderer, &list_rect);

    SDL_SetRenderDrawColor(renderer, theme.inspector_fill.r, theme.inspector_fill.g, theme.inspector_fill.b, theme.inspector_fill.a);
    SDL_RenderFillRect(renderer, &detail_rect);
    SDL_SetRenderDrawColor(renderer, theme.pane_border.r, theme.pane_border.g, theme.pane_border.b, theme.pane_border.a);
    SDL_RenderDrawRect(renderer, &detail_rect);

    effects_panel_render_track_snapshot(renderer, state, layout);

    SDL_Rect prev_clip;
    SDL_bool had_clip = ui_clip_is_enabled(renderer);
    bool set_clip = false;
    if (layout->track_snapshot.list_clip_rect.w > 0 && layout->track_snapshot.list_clip_rect.h > 0) {
        ui_get_clip_rect(renderer, &prev_clip);
        ui_set_clip_rect(renderer, &layout->track_snapshot.list_clip_rect);
        set_clip = true;
    }

    int clip_top = layout->track_snapshot.list_clip_rect.y;
    int clip_bottom = clip_top + layout->track_snapshot.list_clip_rect.h;
    for (int i = 0; i < layout->list_row_count && i < panel->chain_count; ++i) {
        SDL_Rect row = layout->list_row_rects[i];
        if (row.y + row.h >= clip_bottom) {
            break;
        }
        if (row.y + row.h <= clip_top) {
            continue;
        }
        bool selected = (panel->selected_slot_index == i);
        if (selected) {
            SDL_SetRenderDrawColor(renderer,
                                   theme.selection_fill.r,
                                   theme.selection_fill.g,
                                   theme.selection_fill.b,
                                   theme.selection_fill.a);
            SDL_RenderFillRect(renderer, &row);
        }
        SDL_SetRenderDrawColor(renderer,
                               theme.pane_border.r,
                               theme.pane_border.g,
                               theme.pane_border.b,
                               200);
        SDL_RenderDrawRect(renderer, &row);

        const FxSlotUIState* slot = &panel->chain[i];
        const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
        const char* name = info ? info->name : "Effect";
        SDL_Color text_color = slot->enabled ? label_color : text_dim;
        int text_y = row.y + (row.h - ui_font_line_height(text_scale)) / 2;
        int text_x = row.x + 6;
        int text_max_w = row.w - 12;
        if (layout->list_toggle_rects[i].w > 0) {
            text_max_w = layout->list_toggle_rects[i].x - text_x - 6;
        }
        ui_draw_text_clipped(renderer, text_x, text_y, name, text_color, text_scale, text_max_w);
        draw_list_toggle(renderer, &layout->list_toggle_rects[i], slot->enabled, &theme);
    }

    if (set_clip) {
        ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
    }

    if (panel->chain_count == 0) {
        ui_draw_text(renderer,
                     list_rect.x + FX_PANEL_LIST_PAD,
                     list_rect.y + FX_PANEL_LIST_PAD,
                     "No effects yet.",
                     text_dim,
                     text_scale);
    }

    if (panel->list_detail_mode == FX_LIST_DETAIL_EQ) {
        effects_panel_eq_detail_render(renderer, state, layout);
    } else if (panel->list_detail_mode == FX_LIST_DETAIL_METER) {
        if (state->engine) {
            engine_set_spectrum_target(state->engine, ENGINE_SPECTRUM_VIEW_MASTER, -1, false);
        }
        effects_panel_meter_detail_render(renderer, state, layout);
    } else if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        if (state->engine) {
            engine_set_active_fx_meter(state->engine, true, -1, 0);
        }
        if (state->engine) {
            engine_set_spectrum_target(state->engine, ENGINE_SPECTRUM_VIEW_MASTER, -1, false);
        }
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
        if (state->engine) {
            engine_set_active_fx_meter(state->engine, true, -1, 0);
            engine_set_spectrum_target(state->engine, ENGINE_SPECTRUM_VIEW_MASTER, -1, false);
        }
        ui_draw_text(renderer,
                     detail_rect.x + 12,
                     detail_rect.y + 12,
                     "Double-click an effect to open it.",
                     text_dim,
                     1.3f);
    }
}
