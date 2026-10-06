#include "app/media_import.h"
#include "input/timeline/timeline_drop.h"

#include "app_state.h"
#include "audio/media_clip.h"
#include "input/library_input.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_snap.h"
#include "input/timeline_selection.h"
#include "engine/sampler.h"
#include "undo/undo_manager.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>

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

static void set_drop_label(AppState* state, const char* filename, float duration_seconds) {
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
    if (duration_seconds > 0.0f) {
        snprintf(state->timeline_drop_label, sizeof(state->timeline_drop_label), "%s (%.1fs)", temp, duration_seconds);
    } else {
        strncpy(state->timeline_drop_label, temp, sizeof(state->timeline_drop_label) - 1);
        state->timeline_drop_label[sizeof(state->timeline_drop_label) - 1] = '\0';
    }
}

// Uses scan metadata without decoding or converting audio during pointer events.
static float library_item_preview_duration(const AppState* state, const LibraryItem* item) {
    (void)state;
    return item ? item->duration_seconds : 0.0f;
}

void timeline_drop_update_hint(AppState* state) {
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

    SDL_Keymod mods = SDL_GetModState();
    bool alt_held = (mods & KMOD_ALT) != 0;
    bool allow_snap = state->timeline_snap_enabled && !alt_held;
    float visible_seconds = geom.visible_seconds;
    float pixels_per_second = geom.pixels_per_second;
    float window_start = geom.window_start_seconds;
    float window_end = window_start + visible_seconds;
    float seconds = pixels_per_second > 0.0f ? window_start + (float)rel_x / pixels_per_second : window_start;

    float best_sec = clamp_scalar(seconds, window_start, window_end);
    float best_diff = FLT_MAX;

    if (allow_snap) {
        float snap_interval = timeline_get_snap_interval_seconds(state, visible_seconds);
        int base_tick = (int)roundf(seconds / snap_interval);
        for (int offset = -2; offset <= 2; ++offset) {
            float candidate = (float)(base_tick + offset) * snap_interval;
            if (candidate < window_start || candidate > window_end) {
                continue;
            }
            float diff = fabsf(candidate - seconds);
            if (diff < best_diff) {
                best_diff = diff;
                best_sec = candidate;
            }
        }
    }

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    int track_count = engine_get_track_count(state->engine);
    int drop_track = 0;
    if (tracks && sample_rate > 0) {
        int track_height = geom.track_height;
        int track_spacing = geom.track_spacing;
        int track_top = geom.track_top;
        int lane_height = track_height + track_spacing;
        int last_lane_bottom = track_top + track_count * lane_height;
        if (track_count == 0) {
            drop_track = 0;
        } else if (state->mouse_y >= last_lane_bottom) {
            drop_track = track_count;
        } else {
            drop_track = timeline_track_at_position(state, state->mouse_y, track_height, track_spacing);
            if (drop_track < 0) {
                drop_track = 0;
            } else if (drop_track >= track_count) {
                drop_track = track_count - 1;
            }
        }

        if (drop_track < track_count && drop_track >= 0) {
            const EngineTrack* track = &tracks[drop_track];
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
                if (allow_snap) {
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
    }

    if (state->timeline_drop_label[0] == '\0' &&
        state->drag_library_index >= 0 &&
        state->drag_library_index < state->library.count) {
        const LibraryItem* item = &state->library.items[state->drag_library_index];
        float label_duration = state->timeline_drop_preview_duration > 0.0f
                                   ? state->timeline_drop_preview_duration
                                   : library_item_preview_duration(state, item);
        set_drop_label(state, item->name, label_duration);
    }

    best_sec = clamp_scalar(best_sec, window_start, window_end);
    state->timeline_drop_seconds = clamp_scalar(seconds, window_start, window_end);
    state->timeline_drop_seconds_snapped = allow_snap ? best_sec : -1.0f;
    state->timeline_drop_track_index = drop_track;
    float preview_seconds = state->timeline_drop_preview_duration;
    if (state->dragging_library &&
        state->drag_library_index >= 0 &&
        state->drag_library_index < state->library.count &&
        preview_seconds <= 0.0f) {
        const LibraryItem* item = &state->library.items[state->drag_library_index];
        float candidate = library_item_preview_duration(state, item);
        if (candidate > 0.0f) {
            preview_seconds = candidate;
        }
    }
    if (preview_seconds <= 0.0f) {
        preview_seconds = 1.5f;
    }
    state->timeline_drop_preview_duration = preview_seconds;
    state->timeline_drop_active = true;
}

void timeline_drop_handle_library_drag(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    if (!state) {
        return;
    }
    LibraryBrowser* lib = &state->library;
    if (library_input_is_editing(state)) {
        return;
    }
    if (!was_down && is_down && !state->layout_runtime.drag.active) {
        if (library_input_handle_primary_click(state, state->mouse_x, state->mouse_y)) {
            return;
        }
        if (lib->panel_mode == LIBRARY_PANEL_MODE_SOURCE && lib->hovered_index >= 0) {
            Uint32 now = SDL_GetTicks();
            bool is_double = false;
            if (manager->last_library_click_index == lib->hovered_index &&
                now - manager->last_library_click_ticks < 300) {
                is_double = true;
            }
            manager->last_library_click_index = lib->hovered_index;
            manager->last_library_click_ticks = now;

            lib->selected_index = lib->hovered_index;
            if (is_double) {
                const Pane* library_pane = ui_layout_get_pane(state, 3);
                library_input_start_edit(state, library_pane, state->mouse_x);
                return;
            }

            state->dragging_library = true;
            state->drag_library_index = lib->hovered_index;
            if (state->drag_library_index >= 0 && state->drag_library_index < lib->count) {
                const LibraryItem* item = &lib->items[state->drag_library_index];
                float preview = library_item_preview_duration(state, item);
                set_drop_label(state, item->name, preview);
                state->timeline_drop_preview_duration = preview > 0.0f ? preview : 2.0f;
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
                if (target_track > track_count) target_track = track_count;
                const char* media_id = state->library.items[state->drag_library_index].media_id;
                (void)daw_media_import_submit(state, path, media_id, target_track, start_frame, AUDIO_MEDIA_JOB_LIBRARY);
            }
        }
        state->dragging_library = false;
        state->drag_library_index = -1;
        clear_timeline_drop(state);
    }
}
