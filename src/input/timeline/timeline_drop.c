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

static float library_item_preview_duration(const AppState* state, const LibraryItem* item) {
    if (!state || !item) {
        return 0.0f;
    }
    int target_rate = state->runtime_cfg.sample_rate;
    if (state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        if (cfg && cfg->sample_rate > 0) {
            target_rate = cfg->sample_rate;
        }
    }
    if (target_rate <= 0) {
        return item->duration_seconds;
    }
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", state->library.directory, item->name);
    AudioMediaClip clip;
    SDL_zero(clip);
    if (!audio_media_clip_load(full_path, target_rate, &clip)) {
        return item->duration_seconds;
    }
    float seconds = 0.0f;
    if (clip.sample_rate > 0 && clip.frame_count > 0) {
        seconds = (float)clip.frame_count / (float)clip.sample_rate;
    }
    audio_media_clip_free(&clip);
    if (seconds <= 0.0f) {
        seconds = item->duration_seconds;
    }
    return seconds;
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
    float visible_seconds = geom.visible_seconds;
    float pixels_per_second = geom.pixels_per_second;
    float window_start = geom.window_start_seconds;
    float window_end = window_start + visible_seconds;
    float seconds = pixels_per_second > 0.0f ? window_start + (float)rel_x / pixels_per_second : window_start;
    float snap_interval = timeline_get_snap_interval_seconds(state, visible_seconds);

    float best_sec = clamp_scalar(seconds, window_start, window_end);
    float best_diff = FLT_MAX;

    if (!alt_held) {
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
        int track_top = timeline->rect.y + TIMELINE_CONTROLS_HEIGHT + 8;
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
        const LibraryItem* item = &state->library.items[state->drag_library_index];
        float label_duration = state->timeline_drop_preview_duration > 0.0f
                                   ? state->timeline_drop_preview_duration
                                   : library_item_preview_duration(state, item);
        set_drop_label(state, item->name, label_duration);
    }

    best_sec = clamp_scalar(best_sec, window_start, window_end);
    state->timeline_drop_seconds = clamp_scalar(seconds, window_start, window_end);
    state->timeline_drop_seconds_snapped = best_sec;
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
        if (lib->hovered_index >= 0) {
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
                if (target_track >= track_count) {
                    target_track = engine_add_track(state->engine);
                    track_count = engine_get_track_count(state->engine);
                    if (target_track < 0) {
                        target_track = track_count > 0 ? track_count - 1 : 0;
                    }
                }

                int new_clip_index = -1;
                const char* media_id = state->library.items[state->drag_library_index].media_id;
                if (!engine_add_clip_to_track_with_id(state->engine,
                                                      target_track,
                                                      path,
                                                      media_id,
                                                      start_frame,
                                                      &new_clip_index)) {
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
                        int resolved_index = -1;
                        engine_track_apply_no_overlap(state->engine, target_track, new_sampler, &resolved_index);
                        int sampler_track = target_track;
                        int sampler_clip = -1;
                        if (!timeline_find_clip_by_sampler(state, new_sampler, &sampler_track, &sampler_clip)) {
                            sampler_track = target_track;
                            sampler_clip = resolved_index;
                        }
                        if (sampler_track == target_track && sampler_clip >= 0) {
                            final_clip_index = sampler_clip;
                        } else if (sampler_track >= 0 && sampler_clip >= 0) {
                            target_track = sampler_track;
                            final_clip_index = sampler_clip;
                        }
                    }

                    if (final_clip_index >= 0) {
                        const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
                        if (refreshed_tracks && target_track >= 0 &&
                            target_track < engine_get_track_count(state->engine)) {
                            const EngineTrack* track = &refreshed_tracks[target_track];
                            if (track && final_clip_index < track->clip_count) {
                                const EngineClip* clip = &track->clips[final_clip_index];
                                UndoCommand cmd = {0};
                                cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
                                cmd.data.clip_add_remove.added = true;
                                cmd.data.clip_add_remove.track_index = target_track;
                                cmd.data.clip_add_remove.sampler = clip->sampler;
                                const char* media_id = engine_clip_get_media_id(clip);
                                const char* media_path = engine_clip_get_media_path(clip);
                                strncpy(cmd.data.clip_add_remove.clip.media_id, media_id ? media_id : "",
                                        sizeof(cmd.data.clip_add_remove.clip.media_id) - 1);
                                cmd.data.clip_add_remove.clip.media_id[sizeof(cmd.data.clip_add_remove.clip.media_id) - 1] = '\0';
                                strncpy(cmd.data.clip_add_remove.clip.media_path, media_path ? media_path : "",
                                        sizeof(cmd.data.clip_add_remove.clip.media_path) - 1);
                                cmd.data.clip_add_remove.clip.media_path[sizeof(cmd.data.clip_add_remove.clip.media_path) - 1] = '\0';
                                strncpy(cmd.data.clip_add_remove.clip.name, clip->name,
                                        sizeof(cmd.data.clip_add_remove.clip.name) - 1);
                                cmd.data.clip_add_remove.clip.name[sizeof(cmd.data.clip_add_remove.clip.name) - 1] = '\0';
                                cmd.data.clip_add_remove.clip.start_frame = clip->timeline_start_frames;
                                cmd.data.clip_add_remove.clip.duration_frames = clip->duration_frames;
                                cmd.data.clip_add_remove.clip.offset_frames = clip->offset_frames;
                                cmd.data.clip_add_remove.clip.fade_in_frames = clip->fade_in_frames;
                                cmd.data.clip_add_remove.clip.fade_out_frames = clip->fade_out_frames;
                                cmd.data.clip_add_remove.clip.gain = clip->gain;
                                cmd.data.clip_add_remove.clip.selected = false;
                                if (cmd.data.clip_add_remove.clip.duration_frames == 0 && clip->sampler) {
                                    cmd.data.clip_add_remove.clip.duration_frames = engine_sampler_get_frame_count(clip->sampler);
                                }
                                undo_manager_push(&state->undo, &cmd);
                            }
                        }
                        timeline_selection_set_single(state, target_track, final_clip_index);
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
