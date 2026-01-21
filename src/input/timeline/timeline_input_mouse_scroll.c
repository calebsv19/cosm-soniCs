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
    int delta = event->wheel.y != 0 ? event->wheel.y : event->wheel.x;
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    if (delta == 0) {
        return true;
    }
    float step_px = 60.0f;
    float delta_px = (float)delta * step_px;
    float delta_sec = delta_px / geom.pixels_per_second;
    SDL_Keymod mods = SDL_GetModState();

    if (mods & KMOD_ALT) {
        float zoom_factor = 1.0f - (float)delta * 0.1f;
        if (zoom_factor < 0.1f) {
            zoom_factor = 0.1f;
        }
        float anchor_seconds = timeline_x_to_seconds(&geom, mouse_x);
        timeline_zoom_around_seconds(state, anchor_seconds, zoom_factor);
    } else if (mods & KMOD_CTRL) {
        float scale = state->timeline_vertical_scale + (float)delta * 0.1f;
        state->timeline_vertical_scale = clamp_scalar(scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    } else if (mods & KMOD_SHIFT) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
        if (sample_rate <= 0) {
            return true;
        }
        float total_seconds = timeline_total_seconds(state);
        float ph_sec = (float)((double)engine_get_transport_frame(state->engine) / (double)sample_rate);
        float rel_sec = ph_sec - geom.window_start_seconds;
        float padding_px = 50.0f;
        float pad_sec = padding_px / geom.pixels_per_second;
        if (pad_sec < 0.0f) pad_sec = 0.0f;
        if (pad_sec > geom.visible_seconds * 0.45f) {
            pad_sec = geom.visible_seconds * 0.45f;
        }
        float rel_target = rel_sec - delta_sec;
        float window_start = geom.window_start_seconds;
        float min_start = 0.0f;
        float max_start = 0.0f;
        timeline_get_scroll_bounds(state, geom.visible_seconds, &min_start, &max_start);

        if (rel_target < pad_sec) {
            float past = pad_sec - rel_target;
            window_start -= past;
        } else if (rel_target > geom.visible_seconds - pad_sec) {
            float past = rel_target - (geom.visible_seconds - pad_sec);
            window_start += past;
        } else {
            ph_sec -= delta_sec;
        }
        state->timeline_window_start_seconds = clamp_scalar(window_start, min_start, max_start);

        if (rel_target < pad_sec) {
            ph_sec = window_start + pad_sec;
        } else if (rel_target > geom.visible_seconds - pad_sec) {
            ph_sec = window_start + geom.visible_seconds - pad_sec;
        }
        if (ph_sec < 0.0f) ph_sec = 0.0f;
        if (total_seconds > 0.0f && ph_sec > total_seconds) ph_sec = total_seconds;

        uint64_t frame = (uint64_t)llroundf(ph_sec * (float)sample_rate);
        bool was_playing = engine_transport_is_playing(state->engine);
        input_manager_reset_meter_history_on_seek(state);
        engine_transport_seek(state->engine, frame);
        if (was_playing) {
            engine_transport_play(state->engine);
        }
    } else {
        float window_start = geom.window_start_seconds - delta_sec;
        float min_start = 0.0f;
        float max_start = 0.0f;
        timeline_get_scroll_bounds(state, geom.visible_seconds, &min_start, &max_start);
        state->timeline_window_start_seconds = clamp_scalar(window_start, min_start, max_start);
        state->timeline_follow_override = true;
    }

    return true;
}
