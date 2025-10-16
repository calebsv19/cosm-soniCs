#include "input/transport_input.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"

#include <SDL2/SDL.h>

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void transport_input_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->prev_horiz_slider_down = false;
    manager->prev_vert_slider_down = false;
}

void transport_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event) {
        return;
    }

    TransportUI* transport = &state->transport_ui;
    transport_ui_sync(transport, state);

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_Point p = {event->button.x, event->button.y};
            if (SDL_PointInRect(&p, &transport->grid_rect)) {
                state->timeline_show_all_grid_lines = !state->timeline_show_all_grid_lines;
                break;
            }
            if (SDL_PointInRect(&p, &transport->horiz_track_rect)) {
                manager->prev_horiz_slider_down = true;
                float t = (float)(p.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                    t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
                transport_ui_sync(transport, state);
            } else if (SDL_PointInRect(&p, &transport->vert_track_rect)) {
                manager->prev_vert_slider_down = true;
                float t = (float)(p.x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                    t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
                transport_ui_sync(transport, state);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            manager->prev_horiz_slider_down = false;
            manager->prev_vert_slider_down = false;
        }
        break;
    case SDL_MOUSEMOTION:
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
        break;
    default:
        break;
    }
}

void transport_input_update(InputManager* manager, AppState* state) {
    if (!manager || !state) {
        return;
    }
    TransportUI* transport = &state->transport_ui;
    transport_ui_sync(transport, state);
    if (manager->prev_horiz_slider_down || manager->prev_vert_slider_down) {
        int mouse_x, mouse_y;
        Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        (void)buttons;
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
    }
}
