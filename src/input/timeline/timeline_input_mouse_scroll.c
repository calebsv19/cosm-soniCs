#include "input/timeline/timeline_input_mouse_scroll.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "input/timeline/timeline_geometry.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include <SDL2/SDL.h>
#include <math.h>

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void timeline_zoom_around_seconds(AppState* state, float anchor_seconds, float zoom_factor) {
    if (!state) {
        return;
    }
    if (zoom_factor <= 0.0f) {
        return;
    }
    float old_visible = state->timeline_visible_seconds;
    float new_visible = old_visible * zoom_factor;
    if (new_visible < TIMELINE_MIN_VISIBLE_SECONDS) new_visible = TIMELINE_MIN_VISIBLE_SECONDS;
    if (new_visible > TIMELINE_MAX_VISIBLE_SECONDS) new_visible = TIMELINE_MAX_VISIBLE_SECONDS;
    float ratio = 0.0f;
    if (old_visible > 1e-6f) {
        ratio = (anchor_seconds - state->timeline_window_start_seconds) / old_visible;
    }
    state->timeline_visible_seconds = new_visible;
    float new_start = anchor_seconds - ratio * new_visible;
    float min_start = 0.0f;
    float max_start = 0.0f;
    timeline_get_scroll_bounds(state, new_visible, &min_start, &max_start);
    state->timeline_window_start_seconds = clamp_scalar(new_start, min_start, max_start);
}

bool timeline_input_mouse_handle_scroll(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event || event->type != SDL_MOUSEWHEEL || !state->engine) {
        return false;
    }

    const Pane* timeline_pane = ui_layout_get_pane(state, 1);
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
#if SDL_VERSION_ATLEAST(2, 26, 0)
    mouse_x = event->wheel.mouseX;
    mouse_y = event->wheel.mouseY;
#endif
    SDL_Point p = {mouse_x, mouse_y};
    bool hovered = timeline_pane && SDL_PointInRect(&p, &timeline_pane->rect);
    if (!hovered) {
        return false;
    }

    TimelineGeometry geom;
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline || !timeline_compute_geometry(state, timeline, &geom)) {
        return false;
    }
    float dx = (float)event->wheel.x, dy = (float)event->wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (event->wheel.preciseX != 0.0f) dx = event->wheel.preciseX;
    if (event->wheel.preciseY != 0.0f) dy = event->wheel.preciseY;
#endif
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { dx = -dx; dy = -dy; }
    SDL_Keymod mods = SDL_GetModState();
    float delta = dy != 0.0f ? dy : dx;
    if (mods & KMOD_ALT) {
        float zoom_factor = fmaxf(0.1f, 1.0f - delta * 0.1f);
        timeline_zoom_around_seconds(state, timeline_x_to_seconds(&geom, mouse_x), zoom_factor);
    } else if (mods & KMOD_CTRL) {
        timeline_zoom_rows(state, state->timeline_vertical_scale + delta * 0.1f, mouse_y);
    } else {
        if (mods & KMOD_SHIFT) { dx += dy; dy = 0.0f; }
        timeline_set_vertical_scroll(state, geom.vertical_scroll - dy * 60.0f);
        if (dx != 0.0f) {
            float min_start, max_start;
            timeline_get_scroll_bounds(state, geom.visible_seconds, &min_start, &max_start);
            state->timeline_window_start_seconds = clamp_scalar(
                geom.window_start_seconds - dx * 60.0f / geom.pixels_per_second, min_start, max_start);
            state->timeline_follow_override = true;
        }
    }

    return true;
}

// Moves a captured scrollbar thumb using live bounds, even beyond the pane.
static void timeline_scrollbar_move(AppState* state, int y) {
    const Pane* pane = ui_layout_get_pane(state, 1);
    TimelineGeometry geom;
    if (!pane || !timeline_compute_geometry(state, pane, &geom)) return;
    int travel = geom.scrollbar.h - geom.scroll_thumb.h;
    if (travel <= 0) { timeline_set_vertical_scroll(state, 0.0f); return; }
    float fraction = (y - geom.scrollbar.y - state->timeline_scrollbar_grab) / travel;
    timeline_set_vertical_scroll(state, fraction * geom.vertical_max);
}

// Captures scrollbar presses before clip/header hit testing can consume them.
bool timeline_scrollbar_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) return false;
    if (state->timeline_scrollbar_dragging) {
        if (event->type == SDL_MOUSEMOTION) timeline_scrollbar_move(state, event->motion.y);
        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
            timeline_scrollbar_move(state, event->button.y);
            state->timeline_scrollbar_dragging = false;
        }
        return event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEBUTTONDOWN;
    }
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) return false;
    const Pane* pane = ui_layout_get_pane(state, 1);
    TimelineGeometry geom;
    SDL_Point point = {event->button.x, event->button.y};
    if (!pane || !pane->visible || !timeline_compute_geometry(state, pane, &geom) ||
        !SDL_PointInRect(&point, &geom.scrollbar)) return false;
    if (SDL_PointInRect(&point, &geom.scroll_thumb)) {
        state->timeline_scrollbar_grab = point.y - geom.scroll_thumb.y;
        state->timeline_scrollbar_dragging = true;
    } else {
        float page = point.y < geom.scroll_thumb.y ? -(float)geom.viewport.h : (float)geom.viewport.h;
        timeline_set_vertical_scroll(state, geom.vertical_scroll + page);
    }
    return true;
}

// Gives a held scrollbar exclusive sampled-pointer ownership until release.
bool timeline_scrollbar_update(AppState* state, bool is_down) {
    if (!state || !state->timeline_scrollbar_dragging) return false;
    if (!is_down) state->timeline_scrollbar_dragging = false;
    else timeline_scrollbar_move(state, state->mouse_y);
    return true;
}
