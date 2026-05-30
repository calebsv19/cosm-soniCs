#include "input/effects_panel_input.h"
#include "effects_panel_input_internal.h"

#include "app_state.h"
#include "input/effects_panel_input_helpers.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/midi_preset_browser.h"

bool effects_panel_input_handle_mouse_wheel(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_MOUSEWHEEL) {
        return false;
    }

    EffectsPanelState* panel = &state->effects_panel;
    EffectsPanelLayout layout;
    effects_panel_compute_layout(state, &layout);
    bool overlay_open = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);

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
        return true;
    }

    SDL_Point pt = {state->mouse_x, state->mouse_y};
    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        if (panel->track_snapshot.instrument_menu_open &&
            SDL_PointInRect(&pt, &layout.track_snapshot.instrument_menu_rect)) {
            int dy = event->wheel.y;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dy = -dy;
            }
            panel->track_snapshot.instrument_menu_scroll_row =
                midi_preset_browser_scroll_delta(&layout.track_snapshot.instrument_browser,
                                                 panel->track_snapshot.instrument_menu_scroll_row,
                                                 dy);
            effects_panel_compute_layout(state, &layout);
            return true;
        }
        if (panel->track_snapshot.list_scroll_max > 0.0f &&
            SDL_PointInRect(&pt, &layout.track_snapshot.list_clip_rect)) {
            int dy = event->wheel.y;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dy = -dy;
            }
            if (dy != 0) {
                float step = (float)(FX_PANEL_LIST_ROW_HEIGHT + FX_PANEL_LIST_ROW_GAP);
                panel->track_snapshot.list_scroll -= (float)dy * step;
                if (panel->track_snapshot.list_scroll < 0.0f) {
                    panel->track_snapshot.list_scroll = 0.0f;
                }
                if (panel->track_snapshot.list_scroll > panel->track_snapshot.list_scroll_max) {
                    panel->track_snapshot.list_scroll = panel->track_snapshot.list_scroll_max;
                }
                effects_panel_compute_layout(state, &layout);
            }
            return true;
        }
        int open_index = panel->list_open_slot_index;
        if (open_index >= 0 && open_index < panel->chain_count &&
            SDL_PointInRect(&pt, &layout.detail_rect) &&
            panel->slot_runtime[open_index].scroll_max > 0.0f) {
            int dy = event->wheel.y;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dy = -dy;
            }
            if (dy != 0) {
                panel->slot_runtime[open_index].scroll -= (float)dy * 30.0f;
                if (panel->slot_runtime[open_index].scroll < 0.0f) panel->slot_runtime[open_index].scroll = 0.0f;
                if (panel->slot_runtime[open_index].scroll > panel->slot_runtime[open_index].scroll_max) {
                    panel->slot_runtime[open_index].scroll = panel->slot_runtime[open_index].scroll_max;
                }
                effects_panel_compute_layout(state, &layout);
            }
            return true;
        }
    } else {
        int slot = hit_column_index(&layout, panel, &pt);
        if (slot >= 0 && slot < panel->chain_count && panel->slot_runtime[slot].scroll_max > 0.0f) {
            int dy = event->wheel.y;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dy = -dy;
            }
            if (dy != 0) {
                panel->slot_runtime[slot].scroll -= (float)dy * 30.0f;
                if (panel->slot_runtime[slot].scroll < 0.0f) panel->slot_runtime[slot].scroll = 0.0f;
                if (panel->slot_runtime[slot].scroll > panel->slot_runtime[slot].scroll_max) {
                    panel->slot_runtime[slot].scroll = panel->slot_runtime[slot].scroll_max;
                }
                effects_panel_compute_layout(state, &layout);
            }
            return true;
        }
    }

    return false;
}

void effects_panel_input_update_state(InputManager* manager,
                                      AppState* state,
                                      bool left_was_down,
                                      bool left_is_down) {
    (void)manager;
    (void)left_was_down;
    (void)left_is_down;
    if (!state) {
        return;
    }
    if (state->inspector.visible) {
        close_overlay(&state->effects_panel);
        state->effects_panel.focused = false;
        state->effects_panel.selected_slot_index = -1;
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
