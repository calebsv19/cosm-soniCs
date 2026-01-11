#include "input/timeline/timeline_input_mouse_click.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "input/timeline_snap.h"
#include "input/timeline_selection.h"
#include "input/timeline_input.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include "time/tempo.h"
#include <SDL2/SDL.h>
#include <math.h>

#define TIMELINE_HANDLE_HIT_WIDTH 10

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void timeline_marquee_clear(AppState* state) {
    if (!state) return;
    state->timeline_marquee_active = false;
    state->timeline_marquee_rect = (SDL_Rect){0,0,0,0};
    state->timeline_marquee_extend = false;
    state->timeline_marquee_start_x = 0;
    state->timeline_marquee_start_y = 0;
}

static void timeline_clear_selection(AppState* state) {
    timeline_selection_clear(state);
}

static void timeline_select_clip(AppState* state, int track_index, int clip_index) {
    timeline_selection_set_single(state, track_index, clip_index);
}

static void timeline_controls_update_hover(AppState* state) {
    if (!state) {
        return;
    }
    TimelineControlsUI* controls = &state->timeline_controls;
    SDL_Point p = {state->mouse_x, state->mouse_y};
    controls->add_hovered = SDL_PointInRect(&p, &controls->add_rect);
    controls->remove_hovered = SDL_PointInRect(&p, &controls->remove_rect);
    controls->loop_toggle_hovered = SDL_PointInRect(&p, &controls->loop_toggle_rect);
    bool loop_active = state->loop_enabled;
    controls->loop_start_hovered = loop_active && SDL_PointInRect(&p, &controls->loop_start_rect);
    controls->loop_end_hovered = loop_active && SDL_PointInRect(&p, &controls->loop_end_rect);
    if (!loop_active) {
        controls->adjusting_loop_start = false;
        controls->adjusting_loop_end = false;
    }
}

static bool timeline_controls_handle_click(AppState* state, const SDL_Point* point) {
    if (!state || !state->engine || !point) {
        return false;
    }
    TimelineControlsUI* controls = &state->timeline_controls;
    if (SDL_PointInRect(point, &controls->add_rect)) {
        track_name_editor_stop(state, true);
        int new_track = engine_add_track(state->engine);
        if (new_track >= 0) {
            timeline_select_clip(state, new_track, -1);
            inspector_input_init(state);
            state->timeline_drop_track_index = new_track;
            timeline_input_mouse_drag_end(state);
            state->active_track_index = new_track;
        }
        return true;
    }
    if (SDL_PointInRect(point, &controls->remove_rect)) {
        track_name_editor_stop(state, true);
        int track_count = engine_get_track_count(state->engine);
        if (track_count <= 0) {
            return true;
        }
        int target = state->active_track_index;
        if (target < 0 || target >= track_count) {
            target = state->selected_track_index;
        }
        if (target < 0 || target >= track_count) {
            target = track_count - 1;
        }
        if (state->track_name_editor.editing && state->track_name_editor.track_index == target) {
            track_name_editor_stop(state, true);
        }
        if (target >= 0 && engine_remove_track(state->engine, target)) {
            int remaining = engine_get_track_count(state->engine);
            if (remaining <= 0) {
                timeline_clear_selection(state);
                state->active_track_index = -1;
            } else {
                int new_selection = target;
                if (new_selection >= remaining) {
                    new_selection = remaining - 1;
                }
                timeline_select_clip(state, new_selection, -1);
                int active = state->active_track_index;
                if (active == target) {
                    active = new_selection;
                } else if (active > target) {
                    active -= 1;
                }
                if (active >= remaining) {
                    active = remaining - 1;
                }
                if (active < 0) {
                    active = new_selection >= 0 ? new_selection : 0;
                }
                state->active_track_index = active;
            }
            inspector_input_init(state);
            int drop_track = state->selected_track_index >= 0 ? state->selected_track_index : state->active_track_index;
            state->timeline_drop_track_index = drop_track >= 0 ? drop_track : 0;
            timeline_input_mouse_drag_end(state);
        }
        return true;
    }
    if (SDL_PointInRect(point, &controls->loop_toggle_rect)) {
        track_name_editor_stop(state, true);
        controls->adjusting_loop_start = false;
        controls->adjusting_loop_end = false;
        bool new_state = !state->loop_enabled;
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sample_rate = cfg ? cfg->sample_rate : 0;
        if (new_state) {
            uint64_t start_frame = state->loop_start_frame;
            uint64_t end_frame = state->loop_end_frame;
            if (start_frame >= end_frame) {
                uint64_t frame = engine_get_transport_frame(state->engine);
                uint64_t len = sample_rate > 0 ? (uint64_t)sample_rate : 0;
                start_frame = frame;
                end_frame = frame + len;
            }
            state->loop_start_frame = start_frame;
            state->loop_end_frame = end_frame;
            engine_transport_set_loop(state->engine, true, start_frame, end_frame);
        } else {
            engine_transport_set_loop(state->engine, false, 0, 0);
        }
        state->loop_enabled = new_state;
        return true;
    }
    if (SDL_PointInRect(point, &controls->loop_start_rect)) {
        track_name_editor_stop(state, true);
        if (state->loop_enabled) {
            controls->adjusting_loop_start = true;
        }
        return true;
    }
    if (SDL_PointInRect(point, &controls->loop_end_rect)) {
        track_name_editor_stop(state, true);
        if (state->loop_enabled) {
            controls->adjusting_loop_end = true;
        }
        return true;
    }
    return false;
}

static bool snap_time_to_any_clip(const AppState* state, int sample_rate, float threshold_sec, float* inout_seconds) {
    if (!state || !state->engine || !inout_seconds || threshold_sec <= 0.0f || sample_rate <= 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        return false;
    }
    bool snapped = false;
    float best_delta = threshold_sec + 1.0f;
    float seconds = *inout_seconds;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track || track->clip_count <= 0) {
            continue;
        }
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) {
                continue;
            }
            float start_sec = (float)clip->timeline_start_frames / (float)sample_rate;
            uint64_t duration = clip->duration_frames;
            if (duration == 0 && clip->media) {
                duration = clip->media->frame_count;
            }
            float end_sec = (float)(clip->timeline_start_frames + duration) / (float)sample_rate;
            float delta_start = fabsf(seconds - start_sec);
            if (delta_start <= threshold_sec && delta_start < best_delta) {
                best_delta = delta_start;
                *inout_seconds = start_sec;
                snapped = true;
            }
            float delta_end = fabsf(seconds - end_sec);
            if (delta_end <= threshold_sec && delta_end < best_delta) {
                best_delta = delta_end;
                *inout_seconds = end_sec;
                snapped = true;
            }
        }
    }
    return snapped;
}

bool timeline_input_mouse_handle_click_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event) {
        return false;
    }

    if (event->type == SDL_MOUSEMOTION) {
        timeline_controls_update_hover(state);
        const Pane* timeline_pane = ui_layout_get_pane(state, 1);
        SDL_Point p = {event->motion.x, event->motion.y};
        state->timeline_hovered = timeline_pane && SDL_PointInRect(&p, &timeline_pane->rect);
        return false;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = {event->button.x, event->button.y};
        if (timeline_controls_handle_click(state, &p)) {
            timeline_controls_update_hover(state);
            return true;
        }
        return false;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        state->timeline_controls.adjusting_loop_start = false;
        state->timeline_controls.adjusting_loop_end = false;
        return false;
    }

    return false;
}

void timeline_input_mouse_click_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    if (!manager || !state || !state->engine) {
        return;
    }

    timeline_controls_update_hover(state);

    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        timeline_input_mouse_drag_end(state);
        return;
    }

    TimelineGeometry geom;
    if (!timeline_compute_geometry(state, timeline, &geom)) {
        timeline_input_mouse_drag_end(state);
        return;
    }

    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    if (!cfg || cfg->sample_rate <= 0) {
        timeline_input_mouse_drag_end(state);
        return;
    }
    int sample_rate = cfg->sample_rate;

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        timeline_input_mouse_drag_end(state);
        return;
    }

    TimelineDragState* drag = &state->timeline_drag;

    if (state->timeline_marquee_active) {
        if (is_down) {
            state->timeline_marquee_rect.w = state->mouse_x - state->timeline_marquee_start_x;
            state->timeline_marquee_rect.h = state->mouse_y - state->timeline_marquee_start_y;
        } else {
            SDL_Rect rect = state->timeline_marquee_rect;
            if (rect.w < 0) { rect.x += rect.w; rect.w = -rect.w; }
            if (rect.h < 0) { rect.y += rect.h; rect.h = -rect.h; }
            const EngineTrack* tracks_all = engine_get_tracks(state->engine);
            int track_count_all = engine_get_track_count(state->engine);
            if (tracks_all && track_count_all > 0 && rect.w > 0 && rect.h > 0) {
                if (!state->timeline_marquee_extend) {
                    timeline_clear_selection(state);
                }
                for (int t = 0; t < track_count_all; ++t) {
                    const EngineTrack* track = &tracks_all[t];
                    if (!track || track->clip_count <= 0) continue;
                    int lane_top = geom.track_top + t * (geom.track_height + geom.track_spacing);
                    int clip_y = lane_top + 8;
                    int clip_h = geom.track_height - 16;
                    if (clip_h < 8) clip_h = geom.track_height;
                    for (int i = 0; i < track->clip_count; ++i) {
                        const EngineClip* clip = &track->clips[i];
                        if (!clip || !clip->sampler) continue;
                        uint64_t frame_count = clip->duration_frames;
                        if (frame_count == 0) frame_count = engine_sampler_get_frame_count(clip->sampler);
                        double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                        double clip_sec = (double)frame_count / (double)sample_rate;
                        float clip_offset = start_sec - geom.window_start_seconds;
                        int clip_x = geom.content_left + (int)round(clip_offset * geom.pixels_per_second);
                        int clip_w = (int)round(clip_sec * geom.pixels_per_second);
                        if (clip_w < 4) clip_w = 4;
                        SDL_Rect crect = {clip_x, clip_y, clip_w, clip_h};
                        if (SDL_HasIntersection(&rect, &crect)) {
                            timeline_selection_add(state, t, i);
                        }
                    }
                }
            }
            timeline_marquee_clear(state);
        }
        return;
    }

    SDL_Point mouse_point = {state->mouse_x, state->mouse_y};
    bool over_timeline = SDL_PointInRect(&mouse_point, &timeline->rect);
    TimelineControlsUI* controls = &state->timeline_controls;
    if (controls->adjusting_loop_start || controls->adjusting_loop_end) {
        if (!is_down) {
            controls->adjusting_loop_start = false;
            controls->adjusting_loop_end = false;
        } else if (state->loop_enabled) {
            float seconds = timeline_x_to_seconds(&geom, state->mouse_x);
            float window_min = geom.window_start_seconds;
            float window_max = geom.window_start_seconds + geom.visible_seconds;
            if (window_min < 0.0f) window_min = 0.0f;
            if (seconds < window_min) seconds = window_min;
            if (seconds > window_max) seconds = window_max;
            float snap_threshold = 0.05f;
            snap_time_to_any_clip(state, sample_rate, snap_threshold, &seconds);
            uint64_t frame = 0;
            if (sample_rate > 0) {
                frame = (uint64_t)llroundf(seconds * (float)sample_rate);
            }
            if (controls->adjusting_loop_start) {
                if (state->loop_end_frame > 0 && frame >= state->loop_end_frame) {
                    frame = state->loop_end_frame > 0 ? state->loop_end_frame - 1 : 0;
                }
                state->loop_start_frame = frame;
            } else if (controls->adjusting_loop_end) {
                if (frame <= state->loop_start_frame) {
                    frame = state->loop_start_frame + 1;
                }
                state->loop_end_frame = frame;
            }
            engine_transport_set_loop(state->engine, true, state->loop_start_frame, state->loop_end_frame);
        }
        return;
    }
    SDL_Keymod mods = SDL_GetModState();
    bool shift_held = (mods & KMOD_SHIFT) != 0;
    bool alt_held = (mods & KMOD_ALT) != 0;
    int hit_track = -1;
    int hit_clip = -1;
    bool hit_left = false;
    bool hit_right = false;

    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) {
            continue;
        }
        int lane_top = geom.track_top + t * (geom.track_height + geom.track_spacing);
        int clip_y = lane_top + 8;
        int clip_h = geom.track_height - 16;
        if (clip_h < 8) {
            clip_h = geom.track_height;
        }

        SDL_Rect header_rect = {
            timeline->rect.x + 8,
            lane_top + 4,
            TIMELINE_TRACK_HEADER_WIDTH - 16,
            geom.track_height - 8
        };

        int button_w = 18;
        int button_h = 16;
        int button_spacing = 4;
        int buttons_total = button_w * 2 + button_spacing;
        int buttons_x = header_rect.x + header_rect.w - buttons_total - 8;
        if (buttons_x < header_rect.x + 36) {
            buttons_x = header_rect.x + 36;
        }
        int button_y = header_rect.y + header_rect.h - button_h - 4;
        SDL_Rect mute_rect = {buttons_x, button_y, button_w, button_h};
        SDL_Rect solo_rect = {buttons_x + button_w + button_spacing, button_y, button_w, button_h};

        if (!was_down && is_down) {
            if (SDL_PointInRect(&mouse_point, &mute_rect)) {
                engine_track_set_muted(state->engine, t, !track->muted);
                return;
            }
            if (SDL_PointInRect(&mouse_point, &solo_rect)) {
                engine_track_set_solo(state->engine, t, !track->solo);
                return;
            }
        }

        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip || !clip->sampler) {
                continue;
            }

            uint64_t frame_count = clip->duration_frames;
            if (frame_count == 0) {
                frame_count = engine_sampler_get_frame_count(clip->sampler);
            }

            double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
            double clip_sec = (double)frame_count / (double)sample_rate;
            float clip_offset = start_sec - geom.window_start_seconds;
            int clip_x = geom.content_left + (int)round(clip_offset * geom.pixels_per_second);
            int clip_w = (int)round(clip_sec * geom.pixels_per_second);
            if (clip_w < 4) {
                clip_w = 4;
            }

            SDL_Rect rect = {
                clip_x,
                clip_y,
                clip_w,
                clip_h,
            };

            if (SDL_PointInRect(&mouse_point, &rect)) {
                hit_track = t;
                hit_clip = i;
                int local_x = state->mouse_x - rect.x;
                int fade_in_px = 0;
                int fade_out_px = 0;
                if (sample_rate > 0) {
                    fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * geom.pixels_per_second);
                    fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * geom.pixels_per_second);
                    if (fade_in_px > rect.w) fade_in_px = rect.w;
                    if (fade_out_px > rect.w) fade_out_px = rect.w;
                }

                bool over_left_edge = local_x <= TIMELINE_HANDLE_HIT_WIDTH;
                bool over_right_edge = local_x >= rect.w - TIMELINE_HANDLE_HIT_WIDTH;

                bool in_fade_left = (fade_in_px > 0) ? (local_x <= fade_in_px) : over_left_edge;
                bool in_fade_right = (fade_out_px > 0) ? (local_x >= rect.w - fade_out_px) : over_right_edge;

                if (alt_held) {
                    hit_left = in_fade_left;
                    hit_right = in_fade_right;
                } else {
                    hit_left = over_left_edge;
                    hit_right = over_right_edge;
                }
                break;
            }
        }

        if (hit_clip >= 0) {
            break;
        }
    }

    if (!was_down && is_down && shift_held && over_timeline) {
        bool over_clip = (hit_clip >= 0 && hit_track >= 0);
        if (!over_clip) {
            float seconds = timeline_x_to_seconds(&geom, state->mouse_x);
            float window_min = geom.window_start_seconds;
            float window_max = geom.window_start_seconds + geom.visible_seconds;
            if (window_min < 0.0f) window_min = 0.0f;
            seconds = clamp_scalar(seconds, window_min, window_max);
            if (!alt_held) {
                seconds = timeline_snap_seconds_to_grid(state, seconds, geom.visible_seconds);
                if (seconds < window_min) seconds = window_min;
                if (seconds > window_max) seconds = window_max;
            }
            uint64_t frame = (uint64_t)llroundf(seconds * (float)sample_rate);
            engine_transport_seek(state->engine, frame);
            manager->last_click_clip = -1;
            manager->last_click_track = -1;
            manager->last_click_ticks = 0;
            timeline_input_mouse_drag_end(state);
            return;
        }
    }

    if (!was_down && is_down) {
        for (int t = 0; t < track_count; ++t) {
            int lane_top = geom.track_top + t * (geom.track_height + geom.track_spacing);
            SDL_Rect header_rect = {
                timeline->rect.x + 8,
                lane_top + 4,
                geom.header_width - 16,
                geom.track_height - 8
            };
            if (SDL_PointInRect(&mouse_point, &header_rect)) {
                Uint32 now = SDL_GetTicks();
                bool double_click = false;
                if (manager->last_header_click_track == t) {
                    Uint32 delta = now - manager->last_header_click_ticks;
                    if (delta <= 350) {
                        double_click = true;
                    }
                }
                manager->last_header_click_track = t;
                manager->last_header_click_ticks = now;

                track_name_editor_stop(state, true);

                timeline_selection_clear(state);
                state->active_track_index = t;
                state->timeline_drop_track_index = t;
                state->selected_track_index = t;
                state->selected_clip_index = -1;
                timeline_input_mouse_drag_end(state);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;

                if (double_click) {
                    track_name_editor_start(state, t);
                }
                return;
            }
        }

        if (hit_clip >= 0 && hit_track >= 0) {
            track_name_editor_stop(state, true);

            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
            int refreshed_track_count = engine_get_track_count(state->engine);
            const EngineTrack* track = NULL;
            if (refreshed_tracks && hit_track < refreshed_track_count) {
                track = &refreshed_tracks[hit_track];
            }

            bool already_selected = timeline_selection_contains(state, hit_track, hit_clip, NULL);
            bool shift_click = shift_held;
            if (shift_click) {
                if (already_selected) {
                    timeline_selection_remove(state, hit_track, hit_clip);
                } else {
                    timeline_selection_add(state, hit_track, hit_clip);
                }
            } else if (!already_selected) {
                timeline_selection_set_single(state, hit_track, hit_clip);
            }

            if (state->selection_count == 1 && track && hit_clip < track->clip_count) {
                const TimelineSelectionEntry selected = state->selection[0];
                const EngineTrack* sel_track = NULL;
                const EngineTrack* all_tracks = refreshed_tracks;
                if (!all_tracks) {
                    all_tracks = engine_get_tracks(state->engine);
                }
                int total_tracks = engine_get_track_count(state->engine);
                if (all_tracks && selected.track_index >= 0 && selected.track_index < total_tracks) {
                    sel_track = &all_tracks[selected.track_index];
                }
                if (sel_track && selected.clip_index >= 0 && selected.clip_index < sel_track->clip_count) {
                    inspector_input_show(state, selected.track_index, selected.clip_index, &sel_track->clips[selected.clip_index]);
                }
            } else {
                inspector_input_init(state);
            }

            if (shift_click) {
                timeline_input_mouse_drag_end(state);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;
                return;
            }

            Uint32 now = SDL_GetTicks();
            bool double_click = false;
            if (manager->last_click_clip == hit_clip && manager->last_click_track == hit_track) {
                Uint32 delta = now - manager->last_click_ticks;
                if (delta <= 350) {
                    double_click = true;
                }
            }
            manager->last_click_ticks = now;
            manager->last_click_clip = hit_clip;
            manager->last_click_track = hit_track;

            drag->active = false;
            drag->trimming_left = false;
            drag->trimming_right = false;

            if (!track || hit_clip < 0 || hit_clip >= track->clip_count) {
                timeline_input_mouse_drag_end(state);
                return;
            }

            const EngineClip* clip = &track->clips[hit_clip];

            drag->destination_track_index = hit_track;
            uint64_t clip_frames_init = clip->duration_frames;
            if (clip_frames_init == 0 && clip->sampler) {
                clip_frames_init = engine_sampler_get_frame_count(clip->sampler);
            }
            drag->current_start_seconds = (float)clip->timeline_start_frames / (float)sample_rate;
            drag->current_duration_seconds = clip_frames_init > 0 ? (float)clip_frames_init / (float)sample_rate : 0.0f;

            if (double_click) {
                inspector_input_show(state, hit_track, hit_clip, clip);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;
                return;
            }

            drag->multi_move = (state->selection_count > 1 && timeline_selection_contains(state, hit_track, hit_clip, NULL));

            drag->active = true;
            bool fade_left = hit_left && !hit_right && alt_held;
            bool fade_right = hit_right && !hit_left && alt_held;
            drag->trimming_left = hit_left && !hit_right && !alt_held;
            drag->trimming_right = hit_right && !hit_left && !alt_held;
            drag->adjusting_fade_in = fade_left;
            drag->adjusting_fade_out = fade_right;
            drag->track_index = hit_track;
            drag->clip_index = hit_clip;
            drag->start_mouse_x = state->mouse_x;
            drag->start_mouse_seconds = timeline_x_to_seconds(&geom, state->mouse_x);
            drag->initial_start_frames = clip->timeline_start_frames;
            drag->initial_offset_frames = clip->offset_frames;
            drag->initial_duration_frames = clip->duration_frames;
            drag->started_moving = false;
            if (drag->initial_duration_frames == 0) {
                drag->initial_duration_frames = engine_sampler_get_frame_count(clip->sampler);
            }
            drag->clip_total_frames = engine_clip_get_total_frames(state->engine, hit_track, hit_clip);
            if (drag->clip_total_frames == 0) {
                drag->clip_total_frames = drag->initial_offset_frames + drag->initial_duration_frames;
            }
            drag->initial_fade_in_frames = clip->fade_in_frames;
            drag->initial_fade_out_frames = clip->fade_out_frames;
            float start_sec = (float)drag->initial_start_frames / (float)sample_rate;
            drag->start_right_seconds = start_sec + (float)drag->initial_duration_frames / (float)sample_rate;
            inspector_input_commit_if_editing(state);
            state->inspector.adjusting_gain = false;
            if (fade_left || fade_right) {
                state->inspector.adjusting_fade_in = fade_left;
                state->inspector.adjusting_fade_out = fade_right;
            }
            if (drag->multi_move) {
                drag->multi_clip_count = state->selection_count;
                for (int s = 0; s < state->selection_count && s < TIMELINE_MAX_SELECTION; ++s) {
                    TimelineSelectionEntry entry = state->selection[s];
                    EngineSamplerSource* sampler = NULL;
                    uint64_t start_frames = 0;
                    int track_idx = entry.track_index;
                    if (refreshed_tracks && track_idx >= 0 && track_idx < refreshed_track_count) {
                        const EngineTrack* sel_track = &refreshed_tracks[track_idx];
                        if (sel_track && entry.clip_index >= 0 && entry.clip_index < sel_track->clip_count) {
                            const EngineClip* sel_clip = &sel_track->clips[entry.clip_index];
                            sampler = sel_clip->sampler;
                            start_frames = sel_clip->timeline_start_frames;
                        }
                    }
                    drag->multi_samplers[s] = sampler;
                    drag->multi_initial_track[s] = track_idx;
                    drag->multi_initial_start[s] = start_frames;
                }
            } else {
                drag->multi_clip_count = 0;
            }
        } else if (!hit_left && !hit_right && over_timeline) {
            if (state->layout_runtime.drag.active) {
                return;
            }
            timeline_clear_selection(state);
            timeline_input_mouse_drag_end(state);
        }

        if (hit_clip < 0 && hit_track < 0 && over_timeline) {
            state->timeline_marquee_active = true;
            state->timeline_marquee_extend = shift_held;
            state->timeline_marquee_start_x = state->mouse_x;
            state->timeline_marquee_start_y = state->mouse_y;
            state->timeline_marquee_rect = (SDL_Rect){state->mouse_x, state->mouse_y, 0, 0};
            timeline_input_mouse_drag_end(state);
            return;
        }
    }
}
