#include "input/transport_input.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"
#include "ui/layout.h"
#include "session/project_manager.h"

#include <SDL2/SDL.h>

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint64_t transport_total_frames(const AppState* state) {
    if (!state || !state->engine) {
        return 0;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    if (!tracks || track_count <= 0) {
        return 0;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    return max_frames;
}

static float transport_total_seconds(const AppState* state) {
    const EngineRuntimeConfig* cfg = state && state->engine ? engine_get_config(state->engine) : NULL;
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    uint64_t frames = transport_total_frames(state);
    if (frames == 0) {
        return 0.0f;
    }
    return (float)frames / (float)sample_rate;
}

static float timeline_window_max_start(const AppState* state) {
    if (!state) {
        return 0.0f;
    }
    float visible = clamp_scalar(state->timeline_visible_seconds,
                                 TIMELINE_MIN_VISIBLE_SECONDS,
                                 TIMELINE_MAX_VISIBLE_SECONDS);
    float total_seconds = transport_total_seconds(state);
    if (total_seconds > visible) {
        return total_seconds - visible;
    }
    return 0.0f;
}

static void clamp_timeline_window(AppState* state) {
    if (!state) {
        return;
    }
    float visible = clamp_scalar(state->timeline_visible_seconds,
                                 TIMELINE_MIN_VISIBLE_SECONDS,
                                 TIMELINE_MAX_VISIBLE_SECONDS);
    state->timeline_visible_seconds = visible;
    float max_start = timeline_window_max_start(state);
    float start = state->timeline_window_start_seconds;
    if (start < 0.0f) start = 0.0f;
    if (start > max_start) start = max_start;
    state->timeline_window_start_seconds = start;
}

static void open_project_prompt(AppState* state) {
    if (!state) return;
    state->project_prompt.active = true;
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    SDL_StartTextInput();
}

static void transport_seek_to(AppState* state, float t) {
    if (!state || !state->engine) {
        return;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return;
    }
    uint64_t total_frames = transport_total_frames(state);
    if (total_frames == 0) {
        total_frames = (uint64_t)(state->timeline_visible_seconds * (float)sample_rate);
    }
    if (total_frames < 1) {
        total_frames = 1;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint64_t frame = (uint64_t)llroundf(t * (float)total_frames);
    engine_transport_seek(state->engine, frame);
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
            if (SDL_PointInRect(&p, &transport->save_rect)) {
                if (state->project.has_name) {
                    project_manager_save(state, state->project.name, true);
                } else {
                    open_project_prompt(state);
                }
                break;
            }
            if (SDL_PointInRect(&p, &transport->load_rect)) {
                bool shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (shift_down) {
                    if (project_manager_new(state)) {
                        project_manager_post_load(state);
                    }
                } else {
                    state->project_load.active = true;
                    state->project_load.scroll_offset = 0.0f;
                    state->project_load.selected_index = -1;
                    state->project_load.last_click_index = -1;
                    state->project_load.last_click_ticks = 0;
                    int count = 0;
                    project_manager_list(state->project_load.entries, (int)(sizeof(state->project_load.entries) / sizeof(state->project_load.entries[0])), &count);
                    state->project_load.count = count;
                    if (count > 0) {
                        int match = -1;
                        for (int i = 0; i < count; ++i) {
                            if (state->project.path[0] &&
                                strcmp(state->project.path, state->project_load.entries[i].path) == 0) {
                                match = i;
                                break;
                            }
                        }
                        state->project_load.selected_index = match >= 0 ? match : 0;
                    } else {
                        SDL_Log("No project to load (config/projects)");
                    }
                }
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->grid_rect)) {
                state->timeline_show_all_grid_lines = !state->timeline_show_all_grid_lines;
                break;
            }
            if (SDL_PointInRect(&p, &transport->fit_width_rect)) {
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                int sample_rate = cfg ? cfg->sample_rate : 0;
                uint64_t total = transport_total_frames(state);
                float seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
                if (sample_rate > 0 && total > 0) {
                    seconds = (float)total / (float)sample_rate;
                    seconds *= 1.05f;
                }
                seconds = clamp_scalar(seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
                state->timeline_visible_seconds = seconds;
                clamp_timeline_window(state);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->fit_height_rect)) {
                const Pane* timeline = ui_layout_get_pane(state, 1);
                int track_count = engine_get_track_count(state->engine);
                if (timeline && track_count > 0) {
                    float header_padding = 20.0f + (float)(track_count - 1) * 12.0f + 24.0f;
                    float available = (float)timeline->rect.h - header_padding;
                    if (available < (float)track_count * 32.0f) {
                        available = (float)track_count * 32.0f;
                    }
                    float per_track = available / (float)track_count;
                    float target_scale = per_track / (float)TIMELINE_BASE_TRACK_HEIGHT;
                    target_scale = clamp_scalar(target_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
                    state->timeline_vertical_scale = target_scale;
                    transport_ui_sync(transport, state);
                }
                break;
            }
            if (SDL_PointInRect(&p, &transport->seek_track_rect) || SDL_PointInRect(&p, &transport->seek_handle_rect)) {
                float t = (float)(p.x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
                transport->adjusting_seek = true;
                transport_seek_to(state, t);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->window_track_rect) || SDL_PointInRect(&p, &transport->window_handle_rect)) {
                manager->prev_window_slider_down = true;
                transport->adjusting_window = true;
                float t = (float)(p.x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                float max_start = timeline_window_max_start(state);
                state->timeline_window_start_seconds = t * max_start;
                clamp_timeline_window(state);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->horiz_track_rect)) {
                manager->prev_horiz_slider_down = true;
                float t = (float)(p.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                    t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
                clamp_timeline_window(state);
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
            manager->prev_window_slider_down = false;
            transport->adjusting_seek = false;
            transport->adjusting_window = false;
        }
        break;
    case SDL_MOUSEMOTION:
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_window_slider_down && transport->window_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            float max_start = timeline_window_max_start(state);
            state->timeline_window_start_seconds = t * max_start;
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
        if (transport->adjusting_seek && transport->seek_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
            transport_seek_to(state, t);
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
    if (manager->prev_horiz_slider_down || manager->prev_vert_slider_down || manager->prev_window_slider_down || transport->adjusting_seek || transport->adjusting_window) {
        int mouse_x, mouse_y;
        Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        (void)buttons;
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_window_slider_down && transport->window_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            float max_start = timeline_window_max_start(state);
            state->timeline_window_start_seconds = t * max_start;
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
        if (transport->adjusting_seek && transport->seek_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
            transport_seek_to(state, t);
            transport_ui_sync(transport, state);
        }
    }
}
