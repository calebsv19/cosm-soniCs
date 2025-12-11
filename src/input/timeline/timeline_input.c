#include "input/timeline_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline_selection.h"
#include "input/library_input.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include "ui/effects_panel.h"
#include "time/tempo.h"

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

static void add_unique_sampler(EngineSamplerSource** list,
                               int* count,
                               int max,
                               EngineSamplerSource* sampler) {
    if (!list || !count || max <= 0 || !sampler) {
        return;
    }
    int current = *count;
    for (int i = 0; i < current; ++i) {
        if (list[i] == sampler) {
            return;
        }
    }
    if (current >= max) {
        return;
    }
    list[current] = sampler;
    *count = current + 1;
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

static void timeline_marquee_clear(AppState* state) {
    if (!state) return;
    state->timeline_marquee_active = false;
    state->timeline_marquee_rect = (SDL_Rect){0,0,0,0};
    state->timeline_marquee_extend = false;
    state->timeline_marquee_start_x = 0;
    state->timeline_marquee_start_y = 0;
}

typedef struct {
    EngineSamplerSource* sampler;
    int track_index;
    uint64_t start_frame;
} TimelineClipboardEntry;

static struct {
    TimelineClipboardEntry entries[TIMELINE_MAX_SELECTION];
    int count;
    uint64_t anchor_start_frame;
} g_timeline_clipboard = {0};

static void timeline_clipboard_clear(void) {
    g_timeline_clipboard.count = 0;
    g_timeline_clipboard.anchor_start_frame = 0;
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

static void timeline_clipboard_copy(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    timeline_clipboard_clear();

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        return;
    }

    TimelineSelectionEntry temp_entries[TIMELINE_MAX_SELECTION];
    int temp_count = 0;
    if (state->selection_count > 0) {
        int count = state->selection_count;
        if (count > TIMELINE_MAX_SELECTION) count = TIMELINE_MAX_SELECTION;
        for (int i = 0; i < count; ++i) {
            temp_entries[temp_count++] = state->selection[i];
        }
    } else if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
        temp_entries[temp_count++] = (TimelineSelectionEntry){
            .track_index = state->selected_track_index,
            .clip_index = state->selected_clip_index
        };
    }

    if (temp_count <= 0) {
        return;
    }

    uint64_t anchor = UINT64_MAX;
    uint64_t selected_anchor = UINT64_MAX;
    for (int i = 0; i < temp_count && g_timeline_clipboard.count < TIMELINE_MAX_SELECTION; ++i) {
        TimelineSelectionEntry entry = temp_entries[i];
        if (entry.track_index < 0 || entry.track_index >= track_count) {
            continue;
        }
        const EngineTrack* track = &tracks[entry.track_index];
        if (!track || entry.clip_index < 0 || entry.clip_index >= track->clip_count) {
            continue;
        }
        const EngineClip* clip = &track->clips[entry.clip_index];
        if (!clip || !clip->sampler) {
            continue;
        }
        TimelineClipboardEntry* dst = &g_timeline_clipboard.entries[g_timeline_clipboard.count++];
        dst->sampler = clip->sampler;
        dst->track_index = entry.track_index;
        dst->start_frame = clip->timeline_start_frames;
        if (dst->start_frame < anchor) {
            anchor = dst->start_frame;
        }
        if (entry.track_index == state->selected_track_index &&
            entry.clip_index == state->selected_clip_index) {
            selected_anchor = dst->start_frame;
        }
    }

    if (anchor == UINT64_MAX) {
        timeline_clipboard_clear();
        return;
    }
    if (selected_anchor != UINT64_MAX) {
        g_timeline_clipboard.anchor_start_frame = selected_anchor;
    } else {
        g_timeline_clipboard.anchor_start_frame = anchor;
    }
}

static void timeline_clipboard_paste(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (g_timeline_clipboard.count <= 0) {
        return;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
    if (sample_rate <= 0) {
        return;
    }
    uint64_t playhead = engine_get_transport_frame(state->engine);
    uint64_t anchor = g_timeline_clipboard.anchor_start_frame;
    int track_count = engine_get_track_count(state->engine);
    int target_track = state->selected_track_index;
    if (target_track < 0 || target_track >= track_count) {
        target_track = g_timeline_clipboard.entries[0].track_index;
    }
    if (target_track < 0) target_track = 0;
    while (target_track >= track_count) {
        engine_add_track(state->engine);
        track_count = engine_get_track_count(state->engine);
    }

    TimelineSelectionEntry new_sel[TIMELINE_MAX_SELECTION];
    int new_count = 0;

    for (int i = 0; i < g_timeline_clipboard.count && new_count < TIMELINE_MAX_SELECTION; ++i) {
        const TimelineClipboardEntry* src = &g_timeline_clipboard.entries[i];
        if (!src->sampler) {
            continue;
        }
        int clip_track = -1;
        int clip_idx = -1;
        if (!timeline_find_clip_by_sampler(state, src->sampler, &clip_track, &clip_idx)) {
            continue;
        }
        uint64_t desired_start = playhead + (src->start_frame > anchor ? (src->start_frame - anchor) : 0);
        int dup_index = -1;
        // duplicate on source track
        if (!engine_duplicate_clip(state->engine, clip_track, clip_idx, 0, &dup_index) || dup_index < 0) {
            continue;
        }
        // Set start on the duplicate
        int updated_index = dup_index;
        engine_clip_set_timeline_start(state->engine, clip_track, dup_index, desired_start, &updated_index);
        dup_index = updated_index;

        // If target track differs, move duplicate to that track keeping desired start.
        if (target_track != clip_track) {
            int moved = timeline_move_clip_to_track(state, clip_track, dup_index, target_track, desired_start);
            if (moved >= 0) {
                dup_index = moved;
                clip_track = target_track;
            }
        }

        new_sel[new_count].track_index = clip_track;
        new_sel[new_count].clip_index = dup_index;
        new_count++;
    }

    if (new_count > 0) {
        timeline_selection_clear(state);
        for (int i = 0; i < new_count; ++i) {
            timeline_selection_add(state, new_sel[i].track_index, new_sel[i].clip_index);
        }
        state->active_track_index = new_sel[0].track_index;
        state->selected_track_index = new_sel[0].track_index;
        state->selected_clip_index = new_sel[0].clip_index;
    }
}

static int timeline_track_at_position(const AppState* state, int y, int track_height, int track_spacing) {
    if (!state) {
        return -1;
    }
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        return -1;
    }
    int track_top = timeline->rect.y + TIMELINE_CONTROLS_HEIGHT + 8;
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
    timeline_selection_clear(state);
}

static void timeline_select_clip(AppState* state, int track_index, int clip_index) {
    timeline_selection_set_single(state, track_index, clip_index);
}

static void timeline_end_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->timeline_drag.active = false;
    state->timeline_drag.trimming_left = false;
    state->timeline_drag.trimming_right = false;
    state->timeline_drag.adjusting_fade_in = false;
    state->timeline_drag.adjusting_fade_out = false;
    state->timeline_drag.destination_track_index = -1;
    state->timeline_drag.started_moving = false;
    state->timeline_drag.current_start_seconds = 0.0f;
    state->timeline_drag.current_duration_seconds = 0.0f;
    state->inspector.adjusting_fade_in = false;
    state->inspector.adjusting_fade_out = false;
    state->timeline_drag.multi_move = false;
    state->timeline_drag.multi_clip_count = 0;
    for (int i = 0; i < TIMELINE_MAX_SELECTION; ++i) {
        state->timeline_drag.multi_samplers[i] = NULL;
        state->timeline_drag.multi_initial_track[i] = -1;
        state->timeline_drag.multi_initial_start[i] = 0;
    }
    timeline_marquee_clear(state);
}

void track_name_editor_stop(AppState* state, bool commit) {
    if (!state) {
        return;
    }
    TrackNameEditor* editor = &state->track_name_editor;
    if (!editor->editing) {
        return;
    }
    SDL_Log("track_name_editor_stop (commit=%d, track=%d)", commit ? 1 : 0, editor->track_index);
    if (commit && state->engine && editor->track_index >= 0) {
        int track_count = engine_get_track_count(state->engine);
        if (editor->track_index < track_count) {
            engine_track_set_name(state->engine, editor->track_index, editor->buffer);
            effects_panel_sync_from_engine(state);
        }
    }
    editor->editing = false;
    editor->track_index = -1;
    editor->buffer[0] = '\0';
    editor->cursor = 0;
    SDL_StopTextInput();
}

void track_name_editor_start(AppState* state, int track_index) {
    if (!state || !state->engine || track_index < 0) {
        return;
    }
    track_name_editor_stop(state, true);
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index >= track_count) {
        return;
    }
    const EngineTrack* track = &tracks[track_index];
    TrackNameEditor* editor = &state->track_name_editor;
    editor->editing = true;
    editor->track_index = track_index;
    const char* source = track->name[0] ? track->name : NULL;
    char temp[ENGINE_CLIP_NAME_MAX];
    if (!source) {
        snprintf(temp, sizeof(temp), "Track %d", track_index + 1);
        source = temp;
    }
    strncpy(editor->buffer, source, sizeof(editor->buffer) - 1);
    editor->buffer[sizeof(editor->buffer) - 1] = '\0';
    editor->cursor = (int)strlen(editor->buffer);
    SDL_StartTextInput();
}

typedef struct TimelineGeometry {
    int content_left;
    int content_width;
    int track_top;
    int track_height;
    int track_spacing;
    int header_width;
    float visible_seconds;
    float window_start_seconds;
    float pixels_per_second;
} TimelineGeometry;

static float timeline_total_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
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
    if (max_frames == 0) {
        return 0.0f;
    }
    return (float)max_frames / (float)sample_rate;
}

static bool timeline_compute_geometry(const AppState* state, const Pane* timeline, TimelineGeometry* out_geom) {
    if (!state || !timeline || !out_geom) {
        return false;
    }
    TimelineGeometry geom = {0};
    geom.track_spacing = 12;
    geom.header_width = TIMELINE_TRACK_HEADER_WIDTH;
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
    geom.track_top = timeline->rect.y + TIMELINE_CONTROLS_HEIGHT + 8;
    geom.content_left = timeline->rect.x + TIMELINE_TRACK_HEADER_WIDTH + TIMELINE_BORDER_MARGIN;
    int content_right = timeline->rect.x + timeline->rect.w - TIMELINE_BORDER_MARGIN;
    geom.content_width = content_right - geom.content_left;
    if (geom.content_width <= 0) {
        return false;
    }
    float total_seconds = timeline_total_seconds(state);
    float max_start = total_seconds > geom.visible_seconds ? total_seconds - geom.visible_seconds : 0.0f;
    if (max_start < 0.0f) {
        max_start = 0.0f;
    }
    float window_start = state->timeline_window_start_seconds;
    if (window_start < 0.0f) window_start = 0.0f;
    if (window_start > max_start) window_start = max_start;
    geom.window_start_seconds = window_start;
    geom.pixels_per_second = geom.visible_seconds > 0.0f
                                 ? (float)geom.content_width / geom.visible_seconds
                                 : 0.0f;
    if (geom.pixels_per_second <= 0.0f) {
        return false;
    }
    *out_geom = geom;
    return true;
}

static float timeline_x_to_seconds(const TimelineGeometry* geom, int x) {
    if (!geom || geom->pixels_per_second <= 0.0f) {
        return 0.0f;
    }
    float local = (float)(x - geom->content_left) / geom->pixels_per_second;
    return geom->window_start_seconds + local;
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
            timeline_end_drag(state);
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
            timeline_end_drag(state);
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
            if (state->loop_end_frame <= state->loop_start_frame) {
                uint64_t default_len = sample_rate > 0 ? (uint64_t)sample_rate : 48000;
                if (default_len == 0) {
                    default_len = 48000;
                }
                state->loop_end_frame = state->loop_start_frame + default_len;
            }
        }
        state->loop_enabled = new_state;
        state->loop_restart_pending = false;
        engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame);
        return true;
    }
    if (state->loop_enabled && SDL_PointInRect(point, &controls->loop_start_rect)) {
        controls->adjusting_loop_start = true;
        controls->adjusting_loop_end = false;
        return true;
    }
    if (state->loop_enabled && SDL_PointInRect(point, &controls->loop_end_rect)) {
        controls->adjusting_loop_end = true;
        controls->adjusting_loop_start = false;
        return true;
    }
    return false;
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
    float window_start = geom.window_start_seconds;
    float window_end = window_start + visible_seconds;
    float seconds = pixels_per_second > 0.0f ? window_start + (float)rel_x / pixels_per_second : window_start;
    // Snap interval respects beat view: use tempo-based subdivision when enabled.
    float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
    if (state->timeline_view_in_beats && state->tempo.bpm > 0.0f && state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sr = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
        if (sr > 0) {
            TempoState tempo = state->tempo;
            tempo.sample_rate = sr;
            tempo_state_clamp(&tempo);
            double visible_beats = tempo_seconds_to_beats(state->timeline_visible_seconds, &tempo);
            double subdivision = 1.0;
            if (visible_beats <= 2.0) {
                subdivision = 1.0 / 16.0;
            } else if (visible_beats <= 4.0) {
                subdivision = 1.0 / 8.0;
            } else if (visible_beats <= 8.0) {
                subdivision = 1.0 / 4.0;
            } else if (visible_beats <= 16.0) {
                subdivision = 1.0 / 2.0;
            } else {
                subdivision = 1.0;
            }
            double interval_sec = tempo_beats_to_seconds(subdivision, &tempo);
            if (interval_sec > 0.0 && interval_sec < snap_interval * 2.0f) {
                snap_interval = (float)interval_sec;
            }
        }
    }

    float best_sec = clamp_scalar(seconds, window_start, window_end);
    float best_diff = FLT_MAX;

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
        const LibraryItem* item = &state->library.items[state->drag_library_index];
        set_drop_label(state, item->name, item->duration_seconds);
    }

    best_sec = clamp_scalar(best_sec, window_start, window_end);
    state->timeline_drop_seconds = clamp_scalar(seconds, window_start, window_end);
    state->timeline_drop_seconds_snapped = best_sec;
    state->timeline_drop_track_index = drop_track;
    float preview_seconds = state->timeline_drop_preview_duration;
    if (state->dragging_library &&
        state->drag_library_index >= 0 &&
        state->drag_library_index < state->library.count) {
        float candidate = state->library.items[state->drag_library_index].duration_seconds;
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

static void handle_library_drag(InputManager* manager, AppState* state, bool was_down, bool is_down) {
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
                set_drop_label(state, item->name, item->duration_seconds);
                state->timeline_drop_preview_duration = item->duration_seconds > 0.0f ? item->duration_seconds : 2.0f;
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

static bool snap_time_to_any_clip(const AppState* state, int sample_rate, float threshold_sec, float* inout_seconds) {
    if (!state || !state->engine || !inout_seconds || sample_rate <= 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        return false;
    }
    float best_delta = threshold_sec;
    bool snapped = false;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) {
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
            float delta_start = fabsf(*inout_seconds - start_sec);
            if (delta_start < best_delta) {
                best_delta = delta_start;
                *inout_seconds = start_sec;
                snapped = true;
            }
            float delta_end = fabsf(*inout_seconds - end_sec);
            if (delta_end < best_delta) {
                best_delta = delta_end;
                *inout_seconds = end_sec;
                snapped = true;
            }
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
            seconds = clamp_scalar(seconds, window_min, window_max);
            if (state->timeline_view_in_beats && !alt_held && state->tempo.bpm > 0.0f && state->engine) {
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                int sr = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
                if (sr > 0) {
                    TempoState tempo = state->tempo;
                    tempo.sample_rate = sr;
                    tempo_state_clamp(&tempo);
                    double visible_beats = tempo_seconds_to_beats(state->timeline_visible_seconds, &tempo);
                    double subdiv = 1.0;
                    if (visible_beats <= 2.0) {
                        subdiv = 1.0 / 16.0;
                    } else if (visible_beats <= 4.0) {
                        subdiv = 1.0 / 8.0;
                    } else if (visible_beats <= 8.0) {
                        subdiv = 1.0 / 4.0;
                    } else if (visible_beats <= 16.0) {
                        subdiv = 1.0 / 2.0;
                    } else {
                        subdiv = 1.0;
                    }
                    double beat_pos = tempo_seconds_to_beats((double)seconds, &tempo);
                    double snapped_beats = floor(beat_pos / subdiv + 0.5) * subdiv;
                    double snapped_sec = tempo_beats_to_seconds(snapped_beats, &tempo);
                    if (snapped_sec < window_min) snapped_sec = window_min;
                    if (snapped_sec > window_max) snapped_sec = window_max;
                    seconds = (float)snapped_sec;
                }
            }
            uint64_t frame = (uint64_t)llroundf(seconds * (float)sample_rate);
            engine_transport_seek(state->engine, frame);
            manager->last_click_clip = -1;
            manager->last_click_track = -1;
            manager->last_click_ticks = 0;
            timeline_end_drag(state);
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
                timeline_end_drag(state);
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
            } else if (state->selection_count == 0) {
                inspector_input_init(state);
            } else {
                inspector_input_init(state);
            }

            if (shift_click) {
                timeline_end_drag(state);
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
                timeline_end_drag(state);
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
            timeline_end_drag(state);
        }

        if (hit_clip < 0 && hit_track < 0 && over_timeline) {
            state->timeline_marquee_active = true;
            state->timeline_marquee_extend = shift_held;
            state->timeline_marquee_start_x = state->mouse_x;
            state->timeline_marquee_start_y = state->mouse_y;
            state->timeline_marquee_rect = (SDL_Rect){state->mouse_x, state->mouse_y, 0, 0};
            timeline_end_drag(state);
            return;
        }
    }

    if (!is_down && was_down) {
        if (drag->active && !drag->trimming_left && !drag->trimming_right &&
            !drag->adjusting_fade_in && !drag->adjusting_fade_out) {
            int dst_track = drag->destination_track_index;
            if (dst_track < 0) {
                dst_track = drag->track_index;
            }
            if (dst_track >= track_count) {
                dst_track = track_count > 0 ? track_count - 1 : drag->track_index;
            }

            float start_sec = drag->current_start_seconds;
            if (start_sec < 0.0f) {
                start_sec = 0.0f;
            }
            uint64_t start_frame = (uint64_t)llroundf(start_sec * (float)sample_rate);

            const EngineTrack* drop_tracks = engine_get_tracks(state->engine);
            int drop_track_count = engine_get_track_count(state->engine);
            const EngineClip* drop_anchor_clip = NULL;
            if (drop_tracks && drag->track_index >= 0 && drag->track_index < drop_track_count) {
                const EngineTrack* drop_anchor_track = &drop_tracks[drag->track_index];
                if (drop_anchor_track && drag->clip_index >= 0 && drag->clip_index < drop_anchor_track->clip_count) {
                    drop_anchor_clip = &drop_anchor_track->clips[drag->clip_index];
                }
            }
            EngineSamplerSource* anchor_sampler = drop_anchor_clip ? drop_anchor_clip->sampler : NULL;
            EngineSamplerSource* overlap_targets[TIMELINE_MAX_SELECTION + 1];
            int overlap_target_count = 0;
            add_unique_sampler(overlap_targets, &overlap_target_count, TIMELINE_MAX_SELECTION + 1, anchor_sampler);

            if (drag->multi_move && drag->multi_clip_count > 0) {
                int track_offset = dst_track - drag->track_index;
                int64_t frame_delta = (int64_t)start_frame - (int64_t)drag->initial_start_frames;
                TimelineSelectionEntry rebuilt[TIMELINE_MAX_SELECTION];
                int rebuilt_count = 0;
                int anchor_rebuilt_index = -1;

                int required_tracks = dst_track;
                for (int s = 0; s < drag->multi_clip_count; ++s) {
                    if (drag->multi_initial_track[s] >= 0) {
                        int target_track = drag->multi_initial_track[s] + track_offset;
                        if (target_track > required_tracks) {
                            required_tracks = target_track;
                        }
                    }
                }
                int current_tracks = track_count;
                while (required_tracks >= current_tracks) {
                    engine_add_track(state->engine);
                    current_tracks = engine_get_track_count(state->engine);
                }

                for (int s = 0; s < drag->multi_clip_count; ++s) {
                    EngineSamplerSource* sampler = drag->multi_samplers[s];
                    if (!sampler) {
                        continue;
                    }
                    int current_track = -1;
                    int current_clip = -1;
                    if (!timeline_find_clip_by_sampler(state, sampler, &current_track, &current_clip)) {
                        continue;
                    }
                    uint64_t base_start = drag->multi_initial_start[s];
                    int target_track = drag->multi_initial_track[s] + track_offset;
                    if (target_track < 0) {
                        target_track = 0;
                    }
                    uint64_t target_start = base_start;
                    if (frame_delta != 0) {
                        int64_t candidate = (int64_t)base_start + frame_delta;
                        if (candidate < 0) {
                            candidate = 0;
                        }
                        target_start = (uint64_t)candidate;
                    }

                    int new_index = -1;
                    if (target_track == current_track) {
                        new_index = current_clip;
                        if (engine_clip_set_timeline_start(state->engine, current_track, current_clip, target_start, &new_index)) {
                            timeline_selection_update_index(state, current_track, current_clip, new_index);
                        }
                    } else {
                        new_index = timeline_move_clip_to_track(state, current_track, current_clip, target_track, target_start);
                    }
                    add_unique_sampler(overlap_targets, &overlap_target_count, TIMELINE_MAX_SELECTION + 1, sampler);
                    if (new_index >= 0 && rebuilt_count < TIMELINE_MAX_SELECTION) {
                        rebuilt[rebuilt_count].track_index = target_track;
                        rebuilt[rebuilt_count].clip_index = new_index;
                        if (anchor_rebuilt_index < 0) {
                            bool is_anchor = false;
                            if (anchor_sampler && sampler == anchor_sampler) {
                                is_anchor = true;
                            } else if (!anchor_sampler &&
                                       drag->multi_initial_track[s] == drag->track_index &&
                                       drag->multi_initial_start[s] == drag->initial_start_frames) {
                                is_anchor = true;
                            }
                            if (is_anchor) {
                                anchor_rebuilt_index = rebuilt_count;
                            }
                        }
                        rebuilt_count++;
                    }
                }

                if (anchor_rebuilt_index > 0 && anchor_rebuilt_index < rebuilt_count) {
                    TimelineSelectionEntry tmp = rebuilt[0];
                    rebuilt[0] = rebuilt[anchor_rebuilt_index];
                    rebuilt[anchor_rebuilt_index] = tmp;
                }

                timeline_selection_clear(state);
                for (int i = 0; i < rebuilt_count; ++i) {
                    timeline_selection_add(state, rebuilt[i].track_index, rebuilt[i].clip_index);
                }
                if (rebuilt_count > 0) {
                    state->selected_track_index = rebuilt[0].track_index;
                    state->selected_clip_index = rebuilt[0].clip_index;
                    state->active_track_index = rebuilt[0].track_index;
                    state->timeline_drop_track_index = rebuilt[0].track_index;
                    const EngineTrack* updated_tracks = engine_get_tracks(state->engine);
                    int updated_count = engine_get_track_count(state->engine);
                    if (updated_tracks && rebuilt[0].track_index >= 0 && rebuilt[0].track_index < updated_count) {
                        const EngineTrack* anchor_track = &updated_tracks[rebuilt[0].track_index];
                        if (anchor_track && rebuilt[0].clip_index >= 0 && rebuilt[0].clip_index < anchor_track->clip_count) {
                            const EngineClip* anchor_clip = &anchor_track->clips[rebuilt[0].clip_index];
                            inspector_input_show(state, rebuilt[0].track_index, rebuilt[0].clip_index, anchor_clip);
                        } else {
                            inspector_input_init(state);
                        }
                    } else {
                        inspector_input_init(state);
                    }
                } else {
                    inspector_input_init(state);
                }
            } else if (dst_track != drag->track_index && dst_track >= 0) {
                int new_index = timeline_move_clip_to_track(state, drag->track_index, drag->clip_index, dst_track, start_frame);
                if (new_index >= 0) {
                    timeline_selection_set_single(state, dst_track, new_index);
                    const EngineTrack* updated_tracks = engine_get_tracks(state->engine);
                    int updated_count = engine_get_track_count(state->engine);
                    if (updated_tracks && dst_track >= 0 && dst_track < updated_count) {
                        const EngineTrack* dst = &updated_tracks[dst_track];
                        if (dst && new_index >= 0 && new_index < dst->clip_count) {
                            add_unique_sampler(overlap_targets, &overlap_target_count, TIMELINE_MAX_SELECTION + 1, dst->clips[new_index].sampler);
                            inspector_input_show(state, dst_track, new_index, &dst->clips[new_index]);
                        }
                    }
                }
            }

            for (int i = 0; i < overlap_target_count; ++i) {
                EngineSamplerSource* sampler = overlap_targets[i];
                int target_track = -1;
                int target_clip = -1;
                if (timeline_find_clip_by_sampler(state, sampler, &target_track, &target_clip)) {
                    engine_track_apply_no_overlap(state->engine, target_track, sampler, &target_clip);
                }
            }
        }
        timeline_end_drag(state);
        return;
    }

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
    const EngineClip* drag_clip = &drag_track->clips[drag->clip_index];

    if (!drag->trimming_left && !drag->trimming_right && !drag->adjusting_fade_in && !drag->adjusting_fade_out) {
        int hover_track = timeline_track_at_position(state, state->mouse_y, geom.track_height, geom.track_spacing);
        if (hover_track < 0) {
            hover_track = drag->track_index;
        }
        if (hover_track >= track_count) {
            hover_track = track_count > 0 ? track_count - 1 : drag->track_index;
        }
        drag->destination_track_index = hover_track;
    } else {
        drag->destination_track_index = drag->track_index;
    }

    if (!drag->adjusting_fade_in) {
        state->inspector.adjusting_fade_in = false;
    }
    if (!drag->adjusting_fade_out) {
        state->inspector.adjusting_fade_out = false;
    }

    float mouse_seconds = timeline_x_to_seconds(&geom, state->mouse_x);
    float trim_min_start_sec = (float)drag->initial_start_frames / (float)sample_rate -
                               (float)drag->initial_offset_frames / (float)sample_rate;
    if (trim_min_start_sec < 0.0f) {
        trim_min_start_sec = 0.0f;
    }
    const float move_min_start_sec = 0.0f;

    if (drag->adjusting_fade_in || drag->adjusting_fade_out) {
        state->inspector.adjusting_fade_in = drag->adjusting_fade_in;
        state->inspector.adjusting_fade_out = drag->adjusting_fade_out;

    double clip_start_sec = (double)drag_clip->timeline_start_frames / (double)sample_rate;
    uint64_t clip_frames = drag_clip->duration_frames;
    if (clip_frames == 0 && drag_clip->sampler) {
        clip_frames = engine_sampler_get_frame_count(drag_clip->sampler);
    }
    if (clip_frames == 0) {
        clip_frames = 1;
        }
        double clip_length_sec = (double)clip_frames / (double)sample_rate;

        int clip_x = geom.content_left + (int)round((clip_start_sec - geom.window_start_seconds) * geom.pixels_per_second);
        int clip_w = (int)round(clip_length_sec * geom.pixels_per_second);
        if (clip_w < 1) {
            clip_w = 1;
        }

        float local_px = (float)(state->mouse_x - clip_x);
        if (drag->adjusting_fade_out) {
            local_px = (float)(clip_x + clip_w - state->mouse_x);
        }
        if (local_px < 0.0f) local_px = 0.0f;
        if (local_px > (float)clip_w) local_px = (float)clip_w;
        float new_seconds = local_px / geom.pixels_per_second;
        if (new_seconds < 0.0f) new_seconds = 0.0f;
        if (new_seconds > clip_length_sec) new_seconds = (float)clip_length_sec;

        uint64_t new_frames = (uint64_t)llround(new_seconds * (float)sample_rate);
        uint64_t target_fade_in = drag->adjusting_fade_in ? new_frames : drag_clip->fade_in_frames;
        uint64_t target_fade_out = drag->adjusting_fade_out ? new_frames : drag_clip->fade_out_frames;
        engine_clip_set_fades(state->engine, drag->track_index, drag->clip_index, target_fade_in, target_fade_out);

        const EngineTrack* updated_tracks = engine_get_tracks(state->engine);
        int updated_count = engine_get_track_count(state->engine);
        if (updated_tracks && drag->track_index >= 0 && drag->track_index < updated_count) {
            const EngineTrack* updated_track = &updated_tracks[drag->track_index];
            if (updated_track && drag->clip_index >= 0 && drag->clip_index < updated_track->clip_count) {
                const EngineClip* updated_clip = &updated_track->clips[drag->clip_index];
                state->inspector.fade_in_frames = updated_clip->fade_in_frames;
                state->inspector.fade_out_frames = updated_clip->fade_out_frames;
            }
        }
        inspector_input_set_clip(state, drag->track_index, drag->clip_index);
        return;
    } else if (!drag->trimming_left && !drag->trimming_right) {
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float delta_sec = mouse_seconds - drag->start_mouse_seconds;
        if (!drag->started_moving) {
            int dx = state->mouse_x - drag->start_mouse_x;
            if (dx < 0) dx = -dx;
            if (dx >= 2) {
                drag->started_moving = true;
            } else {
                delta_sec = 0.0f;
            }
        }
        float new_start_sec = clamp_scalar(initial_start_sec + delta_sec, 0.0f, geom.visible_seconds);
        float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
        if (drag->started_moving) {
            new_start_sec = snap_time_to_interval(new_start_sec, snap_interval);
            snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        }
        new_start_sec = clamp_scalar(new_start_sec, 0.0f, geom.visible_seconds);
        if (new_start_sec < move_min_start_sec) {
            new_start_sec = move_min_start_sec;
        }

        int64_t target_start_frames = (int64_t)llroundf(new_start_sec * (float)sample_rate);
        if (target_start_frames < 0) {
            target_start_frames = 0;
        }
        uint64_t new_start_frames = (uint64_t)target_start_frames;
        int old_index = drag->clip_index;
        int new_index = drag->clip_index;
        EngineSamplerSource* move_sampler = drag_clip ? drag_clip->sampler : NULL;
        if (engine_clip_set_timeline_start(state->engine, drag->track_index, drag->clip_index, (uint64_t)new_start_frames, &new_index)) {
            int final_index = new_index;
            if (move_sampler) {
                int resolved_index = new_index;
                if (engine_track_apply_no_overlap(state->engine, drag->track_index, move_sampler, &resolved_index) &&
                    resolved_index >= 0) {
                    final_index = resolved_index;
                }
            }
            drag->clip_index = final_index;
            timeline_selection_update_index(state, drag->track_index, old_index, final_index);
            inspector_input_set_clip(state, drag->track_index, final_index);
        }

        if (drag->multi_move) {
            EngineSamplerSource* anchor_sampler = drag_clip ? drag_clip->sampler : NULL;
            int64_t frame_delta = (int64_t)new_start_frames - (int64_t)drag->initial_start_frames;
            for (int s = 0; s < drag->multi_clip_count; ++s) {
                EngineSamplerSource* sampler = drag->multi_samplers[s];
                if (!sampler || sampler == anchor_sampler) {
                    continue;
                }
                int clip_track = -1;
                int clip_idx = -1;
                if (!timeline_find_clip_by_sampler(state, sampler, &clip_track, &clip_idx)) {
                    continue;
                }
                uint64_t base_start = drag->multi_initial_start[s];
                int64_t target_frames = (int64_t)base_start + frame_delta;
                if (target_frames < 0) {
                    target_frames = 0;
                }
                if (clip_track == drag->track_index) {
                    clip_track = drag->track_index;
                }
                int updated_index = clip_idx;
                if (engine_clip_set_timeline_start(state->engine, clip_track, clip_idx, (uint64_t)target_frames, &updated_index)) {
                    timeline_selection_update_index(state, clip_track, clip_idx, updated_index);
                }
            }
        }
    } else if (drag->trimming_left) {
        float new_start_sec = clamp_scalar(mouse_seconds, 0.0f, geom.visible_seconds);
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float initial_duration_sec = (float)drag->initial_duration_frames / (float)sample_rate;
        float max_start_sec = initial_start_sec + initial_duration_sec - (1.0f / (float)sample_rate);
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        float snap_interval = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
        new_start_sec = snap_time_to_interval(new_start_sec, snap_interval);
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
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
            int old_index = drag->clip_index;
            drag->clip_index = new_index;
            timeline_selection_update_index(state, drag->track_index, old_index, new_index);
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

    const EngineTrack* current_tracks = engine_get_tracks(state->engine);
    int current_track_count = engine_get_track_count(state->engine);
    if (current_tracks && drag->track_index >= 0 && drag->track_index < current_track_count) {
        const EngineTrack* current_track = &current_tracks[drag->track_index];
        if (current_track && drag->clip_index >= 0 && drag->clip_index < current_track->clip_count) {
            const EngineClip* current_clip = &current_track->clips[drag->clip_index];
            uint64_t frames_now = current_clip->duration_frames;
            if (frames_now == 0 && current_clip->sampler) {
                frames_now = engine_sampler_get_frame_count(current_clip->sampler);
            }
            drag->current_start_seconds = (float)current_clip->timeline_start_frames / (float)sample_rate;
            drag->current_duration_seconds = frames_now > 0 ? (float)frames_now / (float)sample_rate : 0.0f;
        }
    }
}

void timeline_input_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->last_click_ticks = 0;
    manager->last_click_clip = -1;
    manager->last_click_track = -1;
    manager->last_header_click_ticks = 0;
    manager->last_header_click_track = -1;
}

void timeline_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event || !state->engine) {
        return;
    }

    if (library_input_handle_event(manager, state, event)) {
        return;
    }

    TrackNameEditor* editor = &state->track_name_editor;
    if (event->type == SDL_TEXTINPUT && editor->editing) {
        size_t len = strlen(editor->buffer);
        size_t free_space = sizeof(editor->buffer) - 1 - len;
        if (free_space > 0) {
            size_t incoming = strlen(event->text.text);
            if (incoming > free_space) {
                incoming = free_space;
            }
            int cursor = editor->cursor;
            if (cursor < 0) cursor = 0;
            if (cursor > (int)len) cursor = (int)len;
            // Make room for incoming text at cursor
            memmove(editor->buffer + cursor + incoming, editor->buffer + cursor, len - cursor + 1);
            memcpy(editor->buffer + cursor, event->text.text, incoming);
            editor->cursor = cursor + (int)incoming;
        }
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
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        state->timeline_controls.adjusting_loop_start = false;
        state->timeline_controls.adjusting_loop_end = false;
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod mods = SDL_GetModState();
        if (editor->editing) {
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                track_name_editor_stop(state, true);
            } else if (key == SDLK_ESCAPE) {
                track_name_editor_stop(state, false);
            } else if (key == SDLK_BACKSPACE) {
                size_t len = strlen(editor->buffer);
                if (len > 0 && editor->cursor > 0) {
                    int cur = editor->cursor;
                    memmove(editor->buffer + cur - 1, editor->buffer + cur, len - (size_t)cur + 1);
                    editor->cursor = cur - 1;
                }
            } else if (key == SDLK_DELETE) {
                size_t len = strlen(editor->buffer);
                int cur = editor->cursor;
                if (len > 0 && cur >= 0 && cur < (int)len) {
                    memmove(editor->buffer + cur, editor->buffer + cur + 1, len - (size_t)cur);
                }
            } else if (key == SDLK_LEFT) {
                if (editor->cursor > 0) {
                    editor->cursor -= 1;
                }
            } else if (key == SDLK_RIGHT) {
                int len = (int)strlen(editor->buffer);
                if (editor->cursor < len) {
                    editor->cursor += 1;
                }
            }
            return;
        }
        bool copy_trigger = (key == SDLK_c) && (mods & (KMOD_CTRL | KMOD_GUI));
        bool paste_trigger = (key == SDLK_v) && (mods & (KMOD_CTRL | KMOD_GUI));
        if (copy_trigger) {
            timeline_clipboard_copy(state);
            return;
        } else if (paste_trigger) {
            timeline_clipboard_paste(state);
            return;
        }
        bool duplicate_trigger = (key == SDLK_d) && (mods & (KMOD_CTRL | KMOD_GUI));
        if (duplicate_trigger) {
            TimelineSelectionEntry originals[TIMELINE_MAX_SELECTION];
            EngineSamplerSource* original_samplers[TIMELINE_MAX_SELECTION];
            int original_count = 0;
            int anchor_original_index = -1;

            const EngineTrack* tracks_snapshot = engine_get_tracks(state->engine);
            int track_count_snapshot = engine_get_track_count(state->engine);

            if (state->selection_count > 0) {
                int count = state->selection_count;
                if (count > TIMELINE_MAX_SELECTION) {
                    count = TIMELINE_MAX_SELECTION;
                }
                for (int i = 0; i < count; ++i) {
                    TimelineSelectionEntry entry = state->selection[i];
                    originals[original_count] = entry;
                    EngineSamplerSource* sampler = NULL;
                    if (tracks_snapshot && entry.track_index >= 0 && entry.track_index < track_count_snapshot) {
                        const EngineTrack* track = &tracks_snapshot[entry.track_index];
                        if (track && entry.clip_index >= 0 && entry.clip_index < track->clip_count) {
                            sampler = track->clips[entry.clip_index].sampler;
                        }
                    }
                    original_samplers[original_count] = sampler;
                    if (anchor_original_index < 0 &&
                        entry.track_index == state->selected_track_index &&
                        entry.clip_index == state->selected_clip_index) {
                        anchor_original_index = original_count;
                    }
                    original_count++;
                }
            } else if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
                TimelineSelectionEntry entry = {
                    .track_index = state->selected_track_index,
                    .clip_index = state->selected_clip_index
                };
                originals[0] = entry;
                EngineSamplerSource* sampler = NULL;
                if (tracks_snapshot && entry.track_index >= 0 && entry.track_index < track_count_snapshot) {
                    const EngineTrack* track = &tracks_snapshot[entry.track_index];
                    if (track && entry.clip_index >= 0 && entry.clip_index < track->clip_count) {
                        sampler = track->clips[entry.clip_index].sampler;
                    }
                }
                original_samplers[0] = sampler;
                anchor_original_index = 0;
                original_count = 1;
            }

            if (anchor_original_index < 0 && original_count > 0) {
                anchor_original_index = 0;
            }

            if (original_count > 0) {
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                uint64_t offset = cfg ? (uint64_t)cfg->block_size : 0;
                if (!cfg || offset == 0) {
                    offset = 0;
                }

                TimelineSelectionEntry new_selection[TIMELINE_MAX_SELECTION];
                int new_count = 0;
                int anchor_new_index = -1;

                for (int i = 0; i < original_count; ++i) {
                    EngineSamplerSource* sampler = original_samplers[i];
                    if (!sampler) {
                        continue;
                    }
                    int track_index = -1;
                    int clip_index = -1;
                    if (!timeline_find_clip_by_sampler(state, sampler, &track_index, &clip_index)) {
                        continue;
                    }

                    int duplicate_index = -1;
                    if (engine_duplicate_clip(state->engine, track_index, clip_index, offset, &duplicate_index)) {
                        if (duplicate_index >= 0 && new_count < TIMELINE_MAX_SELECTION) {
                            new_selection[new_count].track_index = track_index;
                            new_selection[new_count].clip_index = duplicate_index;
                            if (i == anchor_original_index) {
                                anchor_new_index = new_count;
                            }
                            new_count++;
                        }
                    }
                }

                if (new_count > 0) {
                    if (anchor_new_index > 0 && anchor_new_index < new_count) {
                        TimelineSelectionEntry tmp = new_selection[0];
                        new_selection[0] = new_selection[anchor_new_index];
                        new_selection[anchor_new_index] = tmp;
                    }

                    timeline_selection_clear(state);
                    for (int i = 0; i < new_count; ++i) {
                        timeline_selection_add(state, new_selection[i].track_index, new_selection[i].clip_index);
                    }

                    state->active_track_index = new_selection[0].track_index;
                    state->selected_track_index = new_selection[0].track_index;
                    state->selected_clip_index = new_selection[0].clip_index;
                    state->timeline_drop_track_index = new_selection[0].track_index;

                    const EngineTrack* updated_tracks = engine_get_tracks(state->engine);
                    int updated_count = engine_get_track_count(state->engine);
                    if (updated_tracks &&
                        new_selection[0].track_index >= 0 &&
                        new_selection[0].track_index < updated_count) {
                        const EngineTrack* anchor_track = &updated_tracks[new_selection[0].track_index];
                        if (anchor_track &&
                            new_selection[0].clip_index >= 0 &&
                            new_selection[0].clip_index < anchor_track->clip_count) {
                            const EngineClip* anchor_clip = &anchor_track->clips[new_selection[0].clip_index];
                            inspector_input_show(state, new_selection[0].track_index, new_selection[0].clip_index, anchor_clip);
                        } else {
                            inspector_input_init(state);
                        }
                    } else {
                        inspector_input_init(state);
                    }
                }
            }
        }
    }
}

void timeline_input_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    timeline_controls_update_hover(state);
    handle_library_drag(manager, state, was_down, is_down);
    update_timeline_drop_hint(state);
    handle_timeline_clip_interactions(manager, state, was_down, is_down);
}
