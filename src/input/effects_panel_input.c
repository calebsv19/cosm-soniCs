#include "input/effects_panel_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel.h"

#include <stdbool.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
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

static bool panel_targets_track(const EffectsPanelState* panel) {
    return panel && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0;
}

static float slider_value_from_mouse(const AppState* state,
                                     const EffectsPanelLayout* layout,
                                     int slot_index,
                                     int param_index,
                                     int mouse_x) {
    if (!state || !layout) {
        return 0.0f;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return 0.0f;
    }
    const FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return 0.0f;
    }
    const SDL_Rect* slider_rect = &layout->param_slider_rects[slot_index][param_index];
    if (slider_rect->w <= 0) {
        return slot->param_values[param_index];
    }
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    float min_v = info ? info->param_min[param_index] : 0.0f;
    float max_v = info ? info->param_max[param_index] : 1.0f;
    if (max_v - min_v < 1e-6f) {
        max_v = min_v + 1.0f;
    }
    float t = (float)(mouse_x - slider_rect->x) / (float)slider_rect->w;
    t = clampf(t, 0.0f, 1.0f);
    return min_v + t * (max_v - min_v);
}

static void apply_slider_value(AppState* state, int slot_index, int param_index, float value) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return;
    }
    bool updated = false;
    if (panel_targets_track(panel)) {
        updated = engine_fx_track_set_param(state->engine,
                                            panel->target_track_index,
                                            slot->id,
                                            (uint32_t)param_index,
                                            value);
    } else {
        updated = engine_fx_master_set_param(state->engine, slot->id, (uint32_t)param_index, value);
    }
    if (updated) {
        slot->param_values[param_index] = value;
    }
}

static void close_overlay(EffectsPanelState* panel) {
    panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    panel->hovered_category_index = -1;
    panel->hovered_effect_index = -1;
    panel->active_category_index = -1;
    panel->overlay_scroll_index = 0;
}

void effects_panel_input_init(AppState* state) {
    if (!state) {
        return;
    }
    effects_panel_init(state);
    if (state->engine) {
        effects_panel_refresh_catalog(state);
        effects_panel_sync_from_engine(state);
    }
}

void effects_panel_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event) {
        return;
    }
    if (state->inspector.visible) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (!panel->initialized && state->engine) {
        effects_panel_refresh_catalog(state);
    }

    EffectsPanelLayout layout;
    effects_panel_compute_layout(state, &layout);
    bool overlay_open = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);

    switch (event->type) {
        case SDL_MOUSEBUTTONDOWN: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point pt = {event->button.x, event->button.y};
            bool button_hit = SDL_PointInRect(&pt, &layout.dropdown_button_rect);
            if (button_hit) {
                if (overlay_open) {
                    close_overlay(panel);
                } else {
                    if (!panel->initialized && state->engine) {
                        effects_panel_refresh_catalog(state);
                    }
                    if (panel->category_count > 0) {
                        panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                        panel->hovered_category_index = -1;
                        panel->hovered_effect_index = -1;
                        panel->active_category_index = -1;
                        panel->overlay_scroll_index = 0;
                    }
                }
                break;
            }

            if (overlay_open) {
                bool handled = false;
                if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS &&
                    SDL_PointInRect(&pt, &layout.overlay_back_rect) &&
                    layout.overlay_back_rect.w > 0) {
                    panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                    panel->hovered_category_index = -1;
                    panel->hovered_effect_index = -1;
                    panel->active_category_index = -1;
                    panel->overlay_scroll_index = 0;
                    handled = true;
                } else if (SDL_PointInRect(&pt, &layout.overlay_rect)) {
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
                                int cat_index = layout.overlay_item_order[i];
                                if (cat_index >= 0 && cat_index < panel->category_count) {
                                    const FxCategoryUIInfo* cat = &panel->categories[cat_index];
                                    panel->active_category_index = cat_index;
                                    panel->hovered_category_index = cat_index;
                                    panel->overlay_scroll_index = 0;
                                    if (cat->type_count > 0) {
                                        panel->overlay_layer = FX_PANEL_OVERLAY_EFFECTS;
                                        panel->hovered_effect_index = -1;
                                    } else {
                                        panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                                    }
                                }
                            } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
                                int type_index = layout.overlay_item_order[i];
                                if (type_index >= 0 && type_index < panel->type_count && state->engine) {
                                    FxTypeId type = panel->types[type_index].type_id;
                                    FxInstId id = 0;
                                    if (panel_targets_track(panel)) {
                                        id = engine_fx_track_add(state->engine, panel->target_track_index, type);
                                    } else {
                                        id = engine_fx_master_add(state->engine, type);
                                    }
                                    if (id != 0) {
                                        effects_panel_sync_from_engine(state);
                                        panel->highlighted_slot_index = panel->chain_count > 0 ? panel->chain_count - 1 : -1;
                                    }
                                    close_overlay(panel);
                                }
                            }
                            handled = true;
                            break;
                        }
                    }
                    if (!handled && layout.overlay_has_scrollbar &&
                        SDL_PointInRect(&pt, &layout.overlay_scrollbar_track)) {
                        handled = true;
                    }
                }
                if (!handled) {
                    close_overlay(panel);
                }
                break;
            }

            // Remove buttons (only when overlay is closed)
            for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                if (SDL_PointInRect(&pt, &layout.remove_button_rects[i])) {
                    if (state->engine) {
                        FxInstId id = panel->chain[i].id;
                        bool removed = false;
                        if (panel_targets_track(panel)) {
                            removed = engine_fx_track_remove(state->engine, panel->target_track_index, id);
                        } else {
                            removed = engine_fx_master_remove(state->engine, id);
                        }
                        if (id != 0 && removed) {
                            effects_panel_sync_from_engine(state);
                        }
                    }
                    panel->highlighted_slot_index = -1;
                    return;
                }
            }

            // Slider hit test
            for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                FxSlotUIState* slot = &panel->chain[i];
                for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
                    if (SDL_PointInRect(&pt, &layout.param_slider_rects[i][p])) {
                        panel->dragging_slider = true;
                        panel->active_slot_index = i;
                        panel->active_param_index = (int)p;
                        float value = slider_value_from_mouse(state, &layout, i, (int)p, event->button.x);
                        apply_slider_value(state, i, (int)p, value);
                        return;
                    }
                }
            }
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                break;
            }
            if (panel->dragging_slider) {
                panel->dragging_slider = false;
                panel->active_slot_index = -1;
                panel->active_param_index = -1;
                if (state->engine) {
                    effects_panel_sync_from_engine(state);
                }
            }
            break;
        }
        case SDL_MOUSEMOTION: {
            SDL_Point pt = {event->motion.x, event->motion.y};
            if (overlay_open && layout.overlay_visible) {
                if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
                    panel->hovered_category_index = -1;
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            int cat_index = layout.overlay_item_order[i];
                            if (cat_index >= 0 && cat_index < panel->category_count) {
                                panel->hovered_category_index = cat_index;
                            }
                            break;
                        }
                    }
                } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
                    panel->hovered_effect_index = -1;
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            int type_index = layout.overlay_item_order[i];
                            if (type_index >= 0 && type_index < panel->type_count) {
                                panel->hovered_effect_index = type_index;
                            }
                            break;
                        }
                    }
                }
                break;
            }

            if (panel->dragging_slider) {
                int slot_index = panel->active_slot_index;
                int param_index = panel->active_param_index;
                float value = slider_value_from_mouse(state, &layout, slot_index, param_index, event->motion.x);
                apply_slider_value(state, slot_index, param_index, value);
            } else {
                panel->highlighted_slot_index = -1;
                for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                    if (SDL_PointInRect(&pt, &layout.remove_button_rects[i])) {
                        panel->highlighted_slot_index = i;
                        break;
                    }
                }
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            if (overlay_open && layout.overlay_visible) {
                SDL_Point pt = {state->mouse_x, state->mouse_y};
                if (SDL_PointInRect(&pt, &layout.overlay_rect)) {
                    int max_scroll = layout.overlay_total_items - layout.overlay_visible_count;
                    if (max_scroll < 0) max_scroll = 0;
                    if (max_scroll > 0) {
                        int delta = 0;
                        if (event->wheel.y > 0) delta = -1;
                        else if (event->wheel.y < 0) delta = 1;
                        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                            delta = -delta;
                        }
                        if (delta != 0) {
                            int new_scroll = panel->overlay_scroll_index + delta;
                            if (new_scroll < 0) new_scroll = 0;
                            if (new_scroll > max_scroll) new_scroll = max_scroll;
                            if (new_scroll != panel->overlay_scroll_index) {
                                panel->overlay_scroll_index = new_scroll;
                                panel->hovered_category_index = -1;
                                panel->hovered_effect_index = -1;
                            }
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void effects_panel_input_update(InputManager* manager, AppState* state, bool left_was_down, bool left_is_down) {
    (void)manager;
    (void)left_was_down;
    (void)left_is_down;
    if (!state) {
        return;
    }
    if (state->inspector.visible) {
        close_overlay(&state->effects_panel);
        return;
    }
    if (state->engine) {
        EffectsPanelState* panel = &state->effects_panel;
        if (!panel->initialized || panel->type_count == 0) {
            effects_panel_refresh_catalog(state);
        }
        effects_panel_sync_from_engine(state);
    }
}
