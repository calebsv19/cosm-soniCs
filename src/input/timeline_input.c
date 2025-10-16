#include "input/timeline_input.h"

#include "app_state.h"
#include "engine.h"
#include "engine/sampler.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TIMELINE_HANDLE_HIT_WIDTH 10
#define MIN_CLIP_DURATION_FRAMES 1

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void clear_timeline_drop(AppState* state) {
    if (!state) {
        return;
    }
    state->timeline_drop_active = false;
    state->timeline_drop_seconds = 0.0f;
    state->timeline_drop_seconds_snapped = -1.0f;
    state->timeline_drop_preview_duration = 0.0f;
    state->timeline_drop_label[0] = '\0';
}

static void set_drop_label(AppState* state, const char* filename) {
    if (!state) {
        return;
    }
    state->timeline_drop_label[0] = '\0';
    if (!filename) {
        return;
    }
    char temp[LIBRARY_NAME_MAX];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* dot = strrchr(temp, '.');
    if (dot) {
        *dot = '\0';
    }
    strncpy(state->timeline_drop_label, temp, sizeof(state->timeline_drop_label) - 1);
    state->timeline_drop_label[sizeof(state->timeline_drop_label) - 1] = '\0';
}

static int timeline_track_at_position(const AppState* state, int y, int track_height, int track_spacing) {
    if (!state) {
        return -1;
    }
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        return -1;
    }
    int track_top = timeline->rect.y + 16;
    int relative = y - track_top;
    if (relative < 0) {
        return 0;
    }
    int lane_height = track_height + track_spacing;
    if (lane_height <= 0) {
        return 0;
    }
    int index = relative / lane_height;
    if (index < 0) {
        index = 0;
    }
    return index;
}

static void timeline_clear_selection(AppState* state) {
    if (!state) {
        return;
    }
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    inspector_input_init(state);
    state->timeline_drop_track_index = 0;
}

static void timeline_select_clip(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->selected_track_index = track_index;
    state->selected_clip_index = clip_index;
}

static void timeline_end_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->timeline_drag.active = false;
    state->timeline_drag.trimming_left = false;
    state->timeline_drag.trimming_right = false;
}

typedef struct TimelineGeometry {
    int content_left;
    int content_width;
    int track_top;
    int track_height;
    int track_spacing;
    float visible_seconds;
    float pixels_per_second;
} TimelineGeometry;

static bool timeline_compute_geometry(const AppState* state, const Pane* timeline, TimelineGeometry* out_geom) {
    if (!state || !timeline || !out_geom) {
        return false;
    }
    TimelineGeometry geom = {0};
    geom.track_spacing = 12;
    geom.visible_seconds = clamp_scalar(state->timeline_visible_seconds,
                                        TIMELINE_MIN_VISIBLE_SECONDS,
                                        TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_scalar(state->timeline_vertical_scale,
                                        TIMELINE_MIN_VERTICAL_SCALE,
                                        TIMELINE_MAX_VERTICAL_SCALE);
    geom.track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (geom.track_height < 32) {
        geom.track_height = 32;
    }
    geom.track_top = timeline->rect.y + 16;
    geom.content_left = timeline->rect.x + TIMELINE_TRACK_HEADER_WIDTH + TIMELINE_BORDER_MARGIN;
    int content_right = timeline->rect.x + timeline->rect.w - TIMELINE_BORDER_MARGIN;
    geom.content_width = content_right - geom.content_left;
    if (geom.content_width <= 0) {
        return false;
    }
    geom.pixels_per_second = geom.visible_seconds > 0.0f
                                 ? (float)geom.content_width / geom.visible_seconds
                                 : 0.0f;
    if (geom.pixels_per_second <= 0.0f) {
        return false;
    }
    *out_geom = geom;
    return true;
}

static void timeline_controls_update_hover(AppState* state) {
    if (!state) {
        return;
    }
    TimelineControlsUI* controls = &state->timeline_controls;
    SDL_Point p = {state->mouse_x, state->mouse_y};
    controls->add_hovered = SDL_PointInRect(&p, &controls->add_rect);
    controls->remove_hovered = SDL_PointInRect(&p, &controls->remove_rect);
}

static bool timeline_controls_handle_click(AppState* state, const SDL_Point* point) {
    if (!state || !state->engine || !point) {
        return false;
    }
    TimelineControlsUI* controls = &state->timeline_controls;
    if (SDL_PointInRect(point, &controls->add_rect)) {
        int new_track = engine_add_track(state->engine);
        if (new_track >= 0) {
            timeline_select_clip(state, new_track, -1);
            inspector_input_init(state);
            state->timeline_drop_track_index = new_track;
            timeline_end_drag(state);
        }
        return true;
    }
    if (SDL_PointInRect(point, &controls->remove_rect)) {
        int track_count = engine_get_track_count(state->engine);
        if (track_count <= 0) {
            return true;
        }
        int target = state->selected_track_index;
        if (target < 0 || target >= track_count) {
            target = track_count - 1;
        }
        if (target >= 0 && engine_remove_track(state->engine, target)) {
            int remaining = engine_get_track_count(state->engine);
            if (remaining <= 0) {
                timeline_clear_selection(state);
            } else {
                int new_selection = target;
                if (new_selection >= remaining) {
                    new_selection = remaining - 1;
                }
                timeline_select_clip(state, new_selection, -1);
            }
            inspector_input_init(state);
            state->timeline_drop_track_index = state->selected_track_index >= 0 ? state->selected_track_index : 0;
            timeline_end_drag(state);
        }
        return true;
    }
    return false;
}

static uint64_t clip_effective_duration(const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    if (clip->duration_frames > 0) {
        return clip->duration_frames;
    }
    if (clip->media) {
        return clip->media->frame_count;
    }
    return 0;
}

static int timeline_find_clip_index_by_sampler(const EngineTrack* track, const EngineSamplerSource* sampler) {
    if (!track || !sampler) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].sampler == sampler) {
            return i;
        }
    }
    return -1;
}

static int timeline_resolve_overlapping_clips(AppState* state, int track_index, EngineSamplerSource* new_sampler) {
    if (!state || !state->engine || track_index < 0 || !new_sampler) {
        return -1;
    }

    for (;;) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (!tracks) {
            return -1;
        }
        const EngineTrack* track = &tracks[track_index];
        if (!track) {
            return -1;
        }

        int new_clip_index = timeline_find_clip_index_by_sampler(track, new_sampler);
        if (new_clip_index < 0 || new_clip_index >= track->clip_count) {
            return -1;
        }
        const EngineClip* new_clip = &track->clips[new_clip_index];
        uint64_t new_duration = clip_effective_duration(new_clip);
        uint64_t new_start = new_clip->timeline_start_frames;
        uint64_t new_end = new_start + new_duration;

        bool adjusted = false;
        for (int i = 0; i < track->clip_count; ++i) {
            if (i == new_clip_index) {
                continue;
            }
            const EngineClip* clip = &track->clips[i];
            if (!clip || !clip->media) {
                continue;
            }

            uint64_t clip_start = clip->timeline_start_frames;
            uint64_t clip_duration = clip_effective_duration(clip);
            uint64_t clip_end = clip_start + clip_duration;

            if (clip_end <= new_start || clip_start >= new_end) {
                continue;
            }

            EngineClip snapshot = *clip;
            EngineSamplerSource* original_sampler = clip->sampler;
            uint64_t left_frames = new_start > clip_start ? (new_start - clip_start) : 0;
            uint64_t right_frames = clip_end > new_end ? (clip_end - new_end) : 0;

            if (left_frames > 0) {
                engine_add_clip_segment(state->engine,
                                        track_index,
                                        &snapshot,
                                        0,
                                        left_frames,
                                        clip_start,
                                        NULL);
            }
            if (right_frames > 0) {
                uint64_t relative_offset = new_end > clip_start ? (new_end - clip_start) : 0;
                engine_add_clip_segment(state->engine,
                                        track_index,
                                        &snapshot,
                                        relative_offset,
                                        right_frames,
                                        new_end,
                                        NULL);
            }

            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
            if (refreshed_tracks) {
                const EngineTrack* refreshed_track = &refreshed_tracks[track_index];
                int original_index = timeline_find_clip_index_by_sampler(refreshed_track, original_sampler);
                if (original_index >= 0) {
                    engine_remove_clip(state->engine, track_index, original_index);
                }
            }

            adjusted = true;
            break;
        }

        if (!adjusted) {
            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
            if (!refreshed_tracks) {
                return -1;
            }
            const EngineTrack* refreshed_track = &refreshed_tracks[track_index];
            return timeline_find_clip_index_by_sampler(refreshed_track, new_sampler);
        }
    }
}

static void update_timeline_drop_hint(AppState* state) {
    if (!state || !state->dragging_library) {
        clear_timeline_drop(state);
        return;
    }
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        clear_timeline_drop(state);
        return;
    }
    TimelineGeometry geom;
    if (!timeline_compute_geometry(state, timeline, &geom)) {
        clear_timeline_drop(state);
        return;
    }
    SDL_Point p = {state->mouse_x, state->mouse_y};
    if (!SDL_PointInRect(&p, &timeline->rect)) {
        clear_timeline_drop(state);
        return;
    }

    int content_width = geom.content_width;
    if (content_width <= 0) {
        clear_timeline_drop(state);
        return;
    }
    int content_left = geom.content_left;
    int rel_x = state->mouse_x - content_left;
    if (rel_x < 0) rel_x = 0;
    if (rel_x > content_width) rel_x = content_width;

    float visible_seconds = geom.visible_seconds;
    float pixels_per_second = geom.pixels_per_second;
    float seconds = pixels_per_second > 0.0f ? (float)rel_x / pixels_per_second : 0.0f;
    float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;

    float best_sec = clamp_scalar(seconds, 0.0f, visible_seconds);
    float best_diff = FLT_MAX;

    int base_tick = (int)roundf(seconds / snap_interval);
    for (int offset = -2; offset <= 2; ++offset) {
        float candidate = (float)(base_tick + offset) * snap_interval;
        if (candidate < 0.0f || candidate > visible_seconds) {
            continue;
        }
        float diff = fabsf(candidate - seconds);
        if (diff < best_diff) {
            best_diff = diff;
            best_sec = candidate;
        }
    }

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    int drop_track = 0;
    if (tracks && sample_rate > 0) {
        int track_height = geom.track_height;
        int track_spacing = geom.track_spacing;
        drop_track = timeline_track_at_position(state, state->mouse_y, track_height, track_spacing);
        if (drop_track < 0) {
            drop_track = 0;
        }
        int track_count = engine_get_track_count(state->engine);
        if (track_count == 0) {
            drop_track = 0;
        } else if (drop_track >= track_count) {
            drop_track = track_count - 1;
        }

        const EngineTrack* track = (track_count > 0) ? &tracks[drop_track] : NULL;
        if (track) {
            for (int i = 0; i < track->clip_count; ++i) {
                const EngineClip* clip = &track->clips[i];
                if (!clip || !clip->media) {
                    continue;
                }
                float start_sec = (float)clip->timeline_start_frames / (float)sample_rate;
                uint64_t duration_frames = clip->duration_frames;
                if (duration_frames == 0) {
                    duration_frames = clip->media->frame_count;
                }
                float end_sec = (float)(clip->timeline_start_frames + duration_frames) / (float)sample_rate;
                float diff_start = fabsf(start_sec - seconds);
                if (diff_start < best_diff) {
                    best_diff = diff_start;
                    best_sec = start_sec;
                }
                float diff_end = fabsf(end_sec - seconds);
                if (diff_end < best_diff) {
                    best_diff = diff_end;
                    best_sec = end_sec;
                }
            }
        }
    }

    if (state->timeline_drop_label[0] == '\0' &&
        state->drag_library_index >= 0 &&
        state->drag_library_index < state->library.count) {
        set_drop_label(state, state->library.items[state->drag_library_index].name);
    }

    best_sec = clamp_scalar(best_sec, 0.0f, visible_seconds);
    state->timeline_drop_seconds = clamp_scalar(seconds, 0.0f, visible_seconds);
    state->timeline_drop_seconds_snapped = best_sec;
    state->timeline_drop_track_index = drop_track;
    if (state->timeline_drop_preview_duration <= 0.0f) {
        state->timeline_drop_preview_duration = 1.5f;
    }
    state->timeline_drop_active = true;
}

static void handle_library_drag(AppState* state, bool was_down, bool is_down) {
    if (!state) {
        return;
    }
    if (!was_down && is_down && !state->layout_runtime.drag.active) {
        if (state->library.hovered_index >= 0) {
            state->library.selected_index = state->library.hovered_index;
            state->dragging_library = true;
            state->drag_library_index = state->library.hovered_index;
            if (state->drag_library_index >= 0 && state->drag_library_index < state->library.count) {
                const LibraryItem* item = &state->library.items[state->drag_library_index];
                set_drop_label(state, item->name);
                state->timeline_drop_preview_duration = 2.0f;
            } else {
                clear_timeline_drop(state);
            }
        }
    }

    if (state->dragging_library && was_down && !is_down) {
        const Pane* timeline = ui_layout_get_pane(state, 1);
        const int mouse_x = state->mouse_x;
        const int mouse_y = state->mouse_y;
        if (timeline &&
            mouse_x >= timeline->rect.x && mouse_x <= timeline->rect.x + timeline->rect.w &&
            mouse_y >= timeline->rect.y && mouse_y <= timeline->rect.y + timeline->rect.h)
        {
            if (state->engine && state->drag_library_index >= 0 &&
                state->drag_library_index < state->library.count) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", state->library.directory,
                         state->library.items[state->drag_library_index].name);
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                float drop_sec = state->timeline_drop_seconds_snapped >= 0.0f
                                     ? state->timeline_drop_seconds_snapped
                                     : state->timeline_drop_seconds;
                if (drop_sec < 0.0f) {
                    drop_sec = 0.0f;
                }
                uint64_t start_frame = 0;
                if (cfg && cfg->sample_rate > 0) {
                    start_frame = (uint64_t)llroundf(drop_sec * (float)cfg->sample_rate);
                }
                int target_track = state->timeline_drop_track_index;
                int track_count = engine_get_track_count(state->engine);
                if (target_track < 0) {
                    target_track = 0;
                }
                if (target_track >= track_count) {
                    target_track = engine_add_track(state->engine);
                    track_count = engine_get_track_count(state->engine);
                    if (target_track < 0) {
                        target_track = track_count > 0 ? track_count - 1 : 0;
                    }
                }

                int new_clip_index = -1;
                if (!engine_add_clip_to_track(state->engine, target_track, path, start_frame, &new_clip_index)) {
                    SDL_Log("Failed to add clip: %s", path);
                } else {
                    SDL_Log("Added clip: %s", path);
                    const EngineTrack* tracks = engine_get_tracks(state->engine);
                    const EngineClip* new_clip_ptr = NULL;
                    EngineSamplerSource* new_sampler = NULL;
                    if (tracks) {
                        const EngineTrack* track = &tracks[target_track];
                        if (track && new_clip_index >= 0 && new_clip_index < track->clip_count) {
                            new_clip_ptr = &track->clips[new_clip_index];
                            new_sampler = new_clip_ptr->sampler;
                        }
                    }

                    int final_clip_index = new_clip_index;
                    if (new_sampler) {
                        int resolved = timeline_resolve_overlapping_clips(state, target_track, new_sampler);
                        if (resolved >= 0) {
                            final_clip_index = resolved;
                        } else if (new_clip_ptr) {
                            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
                            if (refreshed_tracks) {
                                const EngineTrack* refreshed_track = &refreshed_tracks[target_track];
                                final_clip_index = timeline_find_clip_index_by_sampler(refreshed_track, new_sampler);
                            }
                        }
                    }

                    if (final_clip_index >= 0) {
                        timeline_select_clip(state, target_track, final_clip_index);
                        const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
                        if (refreshed_tracks) {
                            const EngineTrack* track = &refreshed_tracks[target_track];
                            if (track && final_clip_index < track->clip_count) {
                                inspector_input_show(state, target_track, final_clip_index, &track->clips[final_clip_index]);
                            }
                        }
                    }
                }
            }
        }
        state->dragging_library = false;
        state->drag_library_index = -1;
        clear_timeline_drop(state);
    }
}

static float snap_time_to_interval(float value, float interval) {
    return roundf(value / interval) * interval;
}

static bool snap_to_neighbor_clip(const EngineTrack* track, int exclude_index, int sample_rate, float threshold_sec, float* inout_start_sec) {
    if (!track || !inout_start_sec) {
        return false;
    }
    float best_delta = threshold_sec;
    bool snapped = false;
    for (int i = 0; i < track->clip_count; ++i) {
        if (i == exclude_index) {
            continue;
        }
        const EngineClip* other = &track->clips[i];
        float other_start = (float)other->timeline_start_frames / (float)sample_rate;
        uint64_t duration_frames = other->duration_frames;
        if (duration_frames == 0 && other->media) {
            duration_frames = other->media->frame_count;
        }
        float other_end = (float)(other->timeline_start_frames + duration_frames) / (float)sample_rate;
        float start_delta = fabsf(*inout_start_sec - other_start);
        if (start_delta < best_delta) {
            best_delta = start_delta;
            *inout_start_sec = other_start;
            snapped = true;
        }
        float end_delta = fabsf(*inout_start_sec - other_end);
        if (end_delta < best_delta) {
            best_delta = end_delta;
            *inout_start_sec = other_end;
            snapped = true;
        }
    }
    return snapped;
}

static void handle_timeline_clip_interactions(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    if (!manager || !state || !state->engine) {
        return;
    }

    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        timeline_end_drag(state);
        return;
    }

    TimelineGeometry geom;
    if (!timeline_compute_geometry(state, timeline, &geom)) {
        timeline_end_drag(state);
        return;
    }

    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    if (!cfg || cfg->sample_rate <= 0) {
        timeline_end_drag(state);
        return;
    }
    int sample_rate = cfg->sample_rate;

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        timeline_end_drag(state);
        return;
    }

    SDL_Point mouse_point = {state->mouse_x, state->mouse_y};
    bool over_timeline = SDL_PointInRect(&mouse_point, &timeline->rect);

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
            int clip_x = geom.content_left + (int)round(start_sec * geom.pixels_per_second);
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
                hit_left = local_x <= TIMELINE_HANDLE_HIT_WIDTH;
                hit_right = local_x >= rect.w - TIMELINE_HANDLE_HIT_WIDTH;
                break;
            }
        }

        if (hit_clip >= 0) {
            break;
        }
    }

    if (!was_down && is_down) {
        if (hit_clip >= 0 && hit_track >= 0) {
            timeline_select_clip(state, hit_track, hit_clip);

            const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
            int refreshed_track_count = engine_get_track_count(state->engine);
            const EngineTrack* track = NULL;
            if (refreshed_tracks && hit_track < refreshed_track_count) {
                track = &refreshed_tracks[hit_track];
            }

            if (track && hit_clip < track->clip_count) {
                inspector_input_show(state, hit_track, hit_clip, &track->clips[hit_clip]);
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

            TimelineDragState* drag = &state->timeline_drag;
            drag->active = false;
            drag->trimming_left = false;
            drag->trimming_right = false;

            if (double_click && track && hit_clip < track->clip_count) {
                inspector_input_show(state, hit_track, hit_clip, &track->clips[hit_clip]);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;
                return;
            }

            if (!track || hit_clip < 0 || hit_clip >= track->clip_count) {
                timeline_end_drag(state);
                return;
            }

            const EngineClip* clip = &track->clips[hit_clip];

            drag->active = true;
            drag->trimming_left = hit_left && !hit_right;
            drag->trimming_right = hit_right && !hit_left;
            drag->track_index = hit_track;
            drag->clip_index = hit_clip;
            drag->start_mouse_x = state->mouse_x;
            drag->start_mouse_seconds = (float)(state->mouse_x - geom.content_left) / geom.pixels_per_second;
            drag->initial_start_frames = clip->timeline_start_frames;
            drag->initial_offset_frames = clip->offset_frames;
            drag->initial_duration_frames = clip->duration_frames;
            if (drag->initial_duration_frames == 0) {
                drag->initial_duration_frames = engine_sampler_get_frame_count(clip->sampler);
            }
            drag->clip_total_frames = engine_clip_get_total_frames(state->engine, hit_track, hit_clip);
            if (drag->clip_total_frames == 0) {
                drag->clip_total_frames = drag->initial_offset_frames + drag->initial_duration_frames;
            }
            float start_sec = (float)drag->initial_start_frames / (float)sample_rate;
            drag->start_right_seconds = start_sec + (float)drag->initial_duration_frames / (float)sample_rate;
            inspector_input_commit_if_editing(state);
            state->inspector.adjusting_gain = false;
        } else if (!hit_left && !hit_right && over_timeline) {
            timeline_clear_selection(state);
            timeline_end_drag(state);
        }
    }

    if (!is_down && was_down) {
        timeline_end_drag(state);
    }

    TimelineDragState* drag = &state->timeline_drag;
    if (!drag->active || !is_down || state->selected_clip_index < 0) {
        return;
    }

    const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
    int refreshed_track_count = engine_get_track_count(state->engine);
    if (!refreshed_tracks || drag->track_index < 0 || drag->track_index >= refreshed_track_count) {
        timeline_end_drag(state);
        return;
    }
    const EngineTrack* drag_track = &refreshed_tracks[drag->track_index];
    if (!drag_track || drag->clip_index < 0 || drag->clip_index >= drag_track->clip_count) {
        timeline_end_drag(state);
        return;
    }

    float mouse_seconds = (float)(state->mouse_x - geom.content_left) / geom.pixels_per_second;
    float min_start_sec = (float)drag->initial_start_frames / (float)sample_rate -
                          (float)drag->initial_offset_frames / (float)sample_rate;
    if (min_start_sec < 0.0f) {
        min_start_sec = 0.0f;
    }

    if (!drag->trimming_left && !drag->trimming_right) {
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float delta_sec = mouse_seconds - drag->start_mouse_seconds;
        float new_start_sec = clamp_scalar(initial_start_sec + delta_sec, 0.0f, geom.visible_seconds);
        float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
        new_start_sec = snap_time_to_interval(new_start_sec, snap_interval);
        snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        new_start_sec = clamp_scalar(new_start_sec, 0.0f, geom.visible_seconds);
        if (new_start_sec < min_start_sec) {
            new_start_sec = min_start_sec;
        }

        uint64_t new_start_frames = (uint64_t)llroundf(new_start_sec * (float)sample_rate);
        int new_index = drag->clip_index;
        if (engine_clip_set_timeline_start(state->engine, drag->track_index, drag->clip_index, new_start_frames, &new_index)) {
            drag->clip_index = new_index;
            state->selected_clip_index = new_index;
            inspector_input_set_clip(state, drag->track_index, new_index);
        }
    } else if (drag->trimming_left) {
        float new_start_sec = clamp_scalar(mouse_seconds, 0.0f, geom.visible_seconds);
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float initial_duration_sec = (float)drag->initial_duration_frames / (float)sample_rate;
        float max_start_sec = initial_start_sec + initial_duration_sec - (1.0f / (float)sample_rate);
        if (new_start_sec < min_start_sec) {
            new_start_sec = min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
        new_start_sec = snap_time_to_interval(new_start_sec, snap_interval);
        if (new_start_sec < min_start_sec) {
            new_start_sec = min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        if (new_start_sec < min_start_sec) {
            new_start_sec = min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }

        uint64_t new_start_frames = (uint64_t)llroundf(new_start_sec * (float)sample_rate);
        int64_t delta_frames = (int64_t)new_start_frames - (int64_t)drag->initial_start_frames;
        int64_t new_offset = (int64_t)drag->initial_offset_frames + delta_frames;
        if (new_offset < 0) {
            new_offset = 0;
        }
        if (drag->clip_total_frames > 0 && (uint64_t)new_offset >= drag->clip_total_frames) {
            new_offset = (int64_t)drag->clip_total_frames - 1;
        }
        int64_t new_duration = (int64_t)drag->initial_duration_frames - delta_frames;
        if (new_duration < MIN_CLIP_DURATION_FRAMES) {
            new_duration = MIN_CLIP_DURATION_FRAMES;
        }
        if (drag->clip_total_frames > 0 && (uint64_t)new_offset + (uint64_t)new_duration > drag->clip_total_frames) {
            uint64_t max_allowed = drag->clip_total_frames - (uint64_t)new_offset;
            if ((uint64_t)new_duration > max_allowed) {
                new_duration = (int64_t)max_allowed;
                if (new_duration < MIN_CLIP_DURATION_FRAMES) {
                    new_duration = MIN_CLIP_DURATION_FRAMES;
                }
            }
        }

        engine_clip_set_region(state->engine, drag->track_index, drag->clip_index,
                               (uint64_t)new_offset, (uint64_t)new_duration);
        int new_index = drag->clip_index;
        if (engine_clip_set_timeline_start(state->engine, drag->track_index, drag->clip_index,
                                           (uint64_t)new_start_frames, &new_index)) {
            drag->clip_index = new_index;
            state->selected_clip_index = new_index;
            inspector_input_set_clip(state, drag->track_index, new_index);
        }
    } else if (drag->trimming_right) {
        float new_right_sec = clamp_scalar(mouse_seconds, 0.0f, geom.visible_seconds);
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float min_right_sec = initial_start_sec + (1.0f / (float)sample_rate);
        float max_right_sec = (float)drag->clip_total_frames / (float)sample_rate;
        max_right_sec -= (float)drag->initial_offset_frames / (float)sample_rate;
        max_right_sec += initial_start_sec;
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }
        float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
        new_right_sec = snap_time_to_interval(new_right_sec, snap_interval);
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }
        snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_right_sec);
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }

        float new_duration_sec = new_right_sec - initial_start_sec;
        if (new_duration_sec < 1.0f / (float)sample_rate) {
            new_duration_sec = 1.0f / (float)sample_rate;
        }
        uint64_t new_duration_frames = (uint64_t)llroundf(new_duration_sec * (float)sample_rate);
        if (drag->clip_total_frames > 0 &&
            drag->initial_offset_frames + new_duration_frames > drag->clip_total_frames) {
            new_duration_frames = drag->clip_total_frames - drag->initial_offset_frames;
        }
        if (new_duration_frames < MIN_CLIP_DURATION_FRAMES) {
            new_duration_frames = MIN_CLIP_DURATION_FRAMES;
        }

        engine_clip_set_region(state->engine, drag->track_index, drag->clip_index,
                               drag->initial_offset_frames, new_duration_frames);
        inspector_input_set_clip(state, drag->track_index, drag->clip_index);
    }
}

void timeline_input_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->last_click_ticks = 0;
    manager->last_click_clip = -1;
    manager->last_click_track = -1;
}

void timeline_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event || !state->engine) {
        return;
    }

    if (event->type == SDL_MOUSEMOTION) {
        timeline_controls_update_hover(state);
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = {event->button.x, event->button.y};
        if (timeline_controls_handle_click(state, &p)) {
            timeline_controls_update_hover(state);
            return;
        }
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod mods = SDL_GetModState();
        bool duplicate_trigger = (key == SDLK_d) && (mods & (KMOD_CTRL | KMOD_GUI));
        if (duplicate_trigger) {
            if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                uint64_t offset = cfg ? (uint64_t)cfg->block_size : 0;
                if (!cfg || offset == 0) {
                    offset = 0;
                }
                int new_index = -1;
                if (engine_duplicate_clip(state->engine, state->selected_track_index, state->selected_clip_index, offset, &new_index)) {
                    timeline_select_clip(state, state->selected_track_index, new_index);
                    const EngineTrack* tracks = engine_get_tracks(state->engine);
                    if (tracks) {
                        const EngineTrack* track = &tracks[state->selected_track_index];
                        if (track && new_index >= 0 && new_index < track->clip_count) {
                            const EngineClip* clip = &track->clips[new_index];
                            inspector_input_show(state, state->selected_track_index, new_index, clip);
                        }
                    }
                }
            }
        }
    }
}

void timeline_input_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    timeline_controls_update_hover(state);
    handle_library_drag(state, was_down, is_down);
    update_timeline_drop_hint(state);
    handle_timeline_clip_interactions(manager, state, was_down, is_down);
}
