#include "input/effects_panel_eq_detail_input.h"

#include "app_state.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_eq_detail.h"

static bool eq_detail_hit(const EffectsPanelLayout* layout, const SDL_Point* pt) {
    if (!layout || !pt) {
        return false;
    }
    return SDL_PointInRect(pt, &layout->detail_rect);
}

static void compute_selector_rects(const SDL_Rect* panel, SDL_Rect* master, SDL_Rect* track) {
    if (!panel || !master || !track) {
        return;
    }
    int x = panel->x + panel->w - EQ_DETAIL_SELECTOR_W - EQ_DETAIL_SELECTOR_PAD;
    int y = panel->y + EQ_DETAIL_SELECTOR_PAD - 4;
    *master = (SDL_Rect){x, y, EQ_DETAIL_SELECTOR_W / 2 - 2, EQ_DETAIL_SELECTOR_H};
    *track = (SDL_Rect){x + EQ_DETAIL_SELECTOR_W / 2 + 2, y, EQ_DETAIL_SELECTOR_W / 2 - 2, EQ_DETAIL_SELECTOR_H};
}

bool effects_panel_eq_detail_handle_mouse_down(AppState* state,
                                               const EffectsPanelLayout* layout,
                                               const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    SDL_Point pt = {event->button.x, event->button.y};
    if (!eq_detail_hit(layout, &pt)) {
        state->effects_panel.eq_detail.hovered = false;
        return false;
    }
    SDL_Rect master_rect;
    SDL_Rect track_rect;
    compute_selector_rects(&layout->detail_rect, &master_rect, &track_rect);
    if (SDL_PointInRect(&pt, &master_rect)) {
        state->effects_panel.eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
        state->effects_panel.eq_detail.spectrum_ready = false;
        return true;
    }
    if (SDL_PointInRect(&pt, &track_rect)) {
        if (state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
            state->effects_panel.target_track_index >= 0) {
            state->effects_panel.eq_detail.view_mode = EQ_DETAIL_VIEW_TRACK;
            state->effects_panel.eq_detail.spectrum_ready = false;
            return true;
        }
        state->effects_panel.eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
        state->effects_panel.eq_detail.spectrum_ready = false;
        return true;
    }
    state->effects_panel.eq_detail.hovered = true;
    state->effects_panel.eq_detail.dragging = true;
    state->effects_panel.eq_detail.last_mouse = pt;
    return true;
}

bool effects_panel_eq_detail_handle_mouse_up(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    if (!state->effects_panel.eq_detail.dragging) {
        return false;
    }
    state->effects_panel.eq_detail.dragging = false;
    return true;
}

bool effects_panel_eq_detail_handle_mouse_motion(AppState* state,
                                                 const EffectsPanelLayout* layout,
                                                 const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    SDL_Point pt = {event->motion.x, event->motion.y};
    state->effects_panel.eq_detail.hovered = eq_detail_hit(layout, &pt);
    if (!state->effects_panel.eq_detail.dragging) {
        return state->effects_panel.eq_detail.hovered;
    }
    state->effects_panel.eq_detail.last_mouse = pt;
    return true;
}
