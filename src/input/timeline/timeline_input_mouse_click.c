#include "input/timeline/timeline_input_mouse_click.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "undo/undo_manager.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_clip_press.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "input/timeline/timeline_input_mouse_tempo_overlay.h"
#include "input/timeline_snap.h"
#include "input/timeline_selection.h"
#include "input/timeline_input.h"
#include "input/automation_input.h"
#include "input/tempo_overlay_input.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/effects_panel.h"
#include "ui/timeline_view.h"
#include "ui/font.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

static int track_name_cursor_from_x(const char* text, float scale, int start_x, int end_x, int mouse_x) {
    if (!text) return 0;
    int len = (int)strlen(text);
    if (mouse_x <= start_x) return 0;
    if (mouse_x >= end_x) return len;
    int rel = mouse_x - start_x;
    int cursor = len;
    char temp[ENGINE_CLIP_NAME_MAX];
    for (int i = 0; i <= len; ++i) {
        snprintf(temp, sizeof(temp), "%.*s", i, text);
        int w = ui_measure_text_width(temp, scale);
        if (w >= rel) {
            cursor = i;
            break;
        }
    }
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    return cursor;
}

static SDL_Rect timeline_lane_clip_rect(const TimelineGeometry* geom, int lane_top, int clip_x, int clip_w) {
    SDL_Rect rect = {0, 0, 0, 0};
    if (!geom) {
        return rect;
    }
    timeline_view_compute_lane_clip_rect(lane_top, geom->track_height, clip_x, clip_w, &rect);
    return rect;
}

static void session_track_free(SessionTrack* track) {
    if (!track) {
        return;
    }
    free(track->clips);
    free(track->fx);
    track->clips = NULL;
    track->fx = NULL;
    track->clip_count = 0;
    track->fx_count = 0;
}

static void eq_curve_to_session(const EqCurveState* src, SessionEqCurve* dst) {
    if (!src || !dst) {
        return;
    }
    dst->low_cut.enabled = src->low_cut.enabled;
    dst->low_cut.freq_hz = src->low_cut.freq_hz;
    dst->low_cut.slope = src->low_cut.slope;
    dst->high_cut.enabled = src->high_cut.enabled;
    dst->high_cut.freq_hz = src->high_cut.freq_hz;
    dst->high_cut.slope = src->high_cut.slope;
    for (int i = 0; i < 4; ++i) {
        dst->bands[i].enabled = src->bands[i].enabled;
        dst->bands[i].freq_hz = src->bands[i].freq_hz;
        dst->bands[i].gain_db = src->bands[i].gain_db;
        dst->bands[i].q_width = src->bands[i].q_width;
    }
}

static bool session_track_from_engine(AppState* state, int track_index, SessionTrack* out_track) {
    if (!state || !state->engine || !out_track) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return false;
    }
    const EngineTrack* track = &tracks[track_index];
    memset(out_track, 0, sizeof(*out_track));
    strncpy(out_track->name, track->name, sizeof(out_track->name) - 1);
    out_track->name[sizeof(out_track->name) - 1] = '\0';
    out_track->gain = track->gain;
    out_track->pan = track->pan;
    out_track->muted = track->muted;
    out_track->solo = track->solo;
    if (state->effects_panel.eq_curve_tracks &&
        track_index < state->effects_panel.eq_curve_tracks_count) {
        eq_curve_to_session(&state->effects_panel.eq_curve_tracks[track_index], &out_track->eq);
    }
    out_track->clip_count = track->clip_count;
    if (track->clip_count > 0) {
        out_track->clips = (SessionClip*)calloc((size_t)track->clip_count, sizeof(SessionClip));
        if (!out_track->clips) {
            out_track->clip_count = 0;
            return false;
        }
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            SessionClip* dst = &out_track->clips[i];
            const char* media_id = engine_clip_get_media_id(clip);
            const char* media_path = engine_clip_get_media_path(clip);
            strncpy(dst->media_id, media_id ? media_id : "", sizeof(dst->media_id) - 1);
            dst->media_id[sizeof(dst->media_id) - 1] = '\0';
            strncpy(dst->media_path, media_path ? media_path : "", sizeof(dst->media_path) - 1);
            dst->media_path[sizeof(dst->media_path) - 1] = '\0';
            strncpy(dst->name, clip->name, sizeof(dst->name) - 1);
            dst->name[sizeof(dst->name) - 1] = '\0';
            dst->start_frame = clip->timeline_start_frames;
            dst->duration_frames = clip->duration_frames;
            dst->offset_frames = clip->offset_frames;
            dst->fade_in_frames = clip->fade_in_frames;
            dst->fade_out_frames = clip->fade_out_frames;
            dst->gain = clip->gain;
            dst->selected = clip->selected;
            if (dst->duration_frames == 0 && clip->sampler) {
                dst->duration_frames = engine_sampler_get_frame_count(clip->sampler);
            }
        }
    }
    FxMasterSnapshot fx_snapshot = {0};
    if (engine_fx_track_snapshot(state->engine, track_index, &fx_snapshot) && fx_snapshot.count > 0) {
        out_track->fx_count = fx_snapshot.count;
        out_track->fx = (SessionFxInstance*)calloc((size_t)fx_snapshot.count, sizeof(SessionFxInstance));
        if (!out_track->fx) {
            out_track->fx_count = 0;
            return true;
        }
        for (int i = 0; i < fx_snapshot.count; ++i) {
            SessionFxInstance* dst = &out_track->fx[i];
            const FxMasterInstanceInfo* src = &fx_snapshot.items[i];
            dst->type = src->type;
            dst->enabled = src->enabled;
            dst->param_count = src->param_count;
            for (uint32_t p = 0; p < src->param_count && p < FX_MAX_PARAMS; ++p) {
                dst->params[p] = src->params[p];
                dst->param_mode[p] = src->param_mode[p];
                dst->param_beats[p] = src->param_beats[p];
            }
        }
    }
    return true;
}

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
    effects_panel_sync_from_engine(state);
}

// Converts a mouse y position into an automation value in -1..1.
static float timeline_automation_value_from_y(const SDL_Rect* rect, int y) {
    if (!rect || rect->h <= 0) {
        return 0.0f;
    }
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    float value = (float)(baseline - y) / (float)range;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

// Locates a nearby automation point by pixel distance.
static int timeline_automation_hit_point(const EngineAutomationLane* lane,
                                         uint64_t clip_frames,
                                         double clip_start_seconds,
                                         int sample_rate,
                                         const TimelineGeometry* geom,
                                         const SDL_Rect* rect,
                                         int x,
                                         int y) {
    if (!lane || !rect || !geom || rect->w <= 0 || rect->h <= 0 || clip_frames == 0 || sample_rate <= 0) {
        return -1;
    }
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    const int radius = 6;
    for (int i = 0; i < lane->point_count; ++i) {
        uint64_t frame = lane->points[i].frame > clip_frames ? clip_frames : lane->points[i].frame;
        double point_seconds = clip_start_seconds + (double)frame / (double)sample_rate;
        int px = timeline_seconds_to_x(geom, (float)point_seconds);
        int py = baseline - (int)llround((double)lane->points[i].value * (double)range);
        if (abs(px - x) <= radius && abs(py - y) <= radius) {
            return i;
        }
    }
    return -1;
}

static void timeline_select_clip(AppState* state, int track_index, int clip_index) {
    timeline_selection_set_single(state, track_index, clip_index);
    effects_panel_sync_from_engine(state);
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
    controls->snap_toggle_hovered = SDL_PointInRect(&p, &controls->snap_toggle_rect);
    controls->automation_toggle_hovered = SDL_PointInRect(&p, &controls->automation_toggle_rect);
    controls->automation_target_hovered = SDL_PointInRect(&p, &controls->automation_target_rect);
    controls->tempo_toggle_hovered = SDL_PointInRect(&p, &controls->tempo_toggle_rect);
    controls->automation_label_toggle_hovered = SDL_PointInRect(&p, &controls->automation_label_toggle_rect);
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
            UndoCommand cmd = {0};
            cmd.type = UNDO_CMD_TRACK_EDIT;
            cmd.data.track_edit.track_index = new_track;
            cmd.data.track_edit.has_before = false;
            cmd.data.track_edit.has_after = true;
            if (session_track_from_engine(state, new_track, &cmd.data.track_edit.after)) {
                undo_manager_push(&state->undo, &cmd);
                session_track_free(&cmd.data.track_edit.after);
            }
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
        if (target >= 0) {
            UndoCommand cmd = {0};
            cmd.type = UNDO_CMD_TRACK_EDIT;
            cmd.data.track_edit.track_index = target;
            cmd.data.track_edit.has_before = true;
            cmd.data.track_edit.has_after = false;
            if (session_track_from_engine(state, target, &cmd.data.track_edit.before)) {
                if (engine_remove_track(state->engine, target)) {
                    undo_manager_push(&state->undo, &cmd);
                }
                session_track_free(&cmd.data.track_edit.before);
            } else if (engine_remove_track(state->engine, target)) {
                // If we fail to snapshot, still remove without undo.
            }
        }
        if (target >= 0) {
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
    if (SDL_PointInRect(point, &controls->snap_toggle_rect)) {
        track_name_editor_stop(state, true);
        state->timeline_snap_enabled = !state->timeline_snap_enabled;
        return true;
    }
    if (SDL_PointInRect(point, &controls->automation_toggle_rect)) {
        track_name_editor_stop(state, true);
        state->timeline_automation_mode = !state->timeline_automation_mode;
        return true;
    }
    if (SDL_PointInRect(point, &controls->automation_target_rect)) {
        track_name_editor_stop(state, true);
        int next = (int)state->automation_ui.target + 1;
        if (next >= (int)ENGINE_AUTOMATION_TARGET_COUNT) {
            next = 0;
        }
        state->automation_ui.target = (EngineAutomationTarget)next;
        return true;
    }
    if (SDL_PointInRect(point, &controls->tempo_toggle_rect)) {
        track_name_editor_stop(state, true);
        state->timeline_tempo_overlay_enabled = !state->timeline_tempo_overlay_enabled;
        return true;
    }
    if (SDL_PointInRect(point, &controls->automation_label_toggle_rect)) {
        track_name_editor_stop(state, true);
        state->timeline_automation_labels_enabled = !state->timeline_automation_labels_enabled;
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

    if (!is_down && was_down && state->timeline_drag.pending_shift_select) {
        bool should_toggle = !state->timeline_drag.started_moving;
        int track_index = state->timeline_drag.pending_shift_track;
        int clip_index = state->timeline_drag.pending_shift_clip;
        bool remove = state->timeline_drag.pending_shift_remove;
        state->timeline_drag.pending_shift_select = false;
        state->timeline_drag.pending_shift_remove = false;
        state->timeline_drag.pending_shift_track = -1;
        state->timeline_drag.pending_shift_clip = -1;
        if (should_toggle && track_index >= 0 && clip_index >= 0) {
            if (remove) {
                timeline_selection_remove(state, track_index, clip_index);
            } else {
                timeline_selection_add(state, track_index, clip_index);
            }
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            int track_count = engine_get_track_count(state->engine);
            if (state->selection_count == 1 &&
                tracks &&
                track_index < track_count &&
                clip_index < tracks[track_index].clip_count) {
                inspector_input_show(state, track_index, clip_index, &tracks[track_index].clips[clip_index]);
            } else {
                inspector_input_init(state);
            }
            effects_panel_sync_from_engine(state);
        }
    }

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
                        SDL_Rect crect = timeline_lane_clip_rect(&geom, lane_top, clip_x, clip_w);
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

    if (timeline_input_mouse_handle_tempo_overlay(state, &geom, sample_rate, was_down, is_down)) {
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
            if (state->timeline_snap_enabled) {
                float grid_snap = timeline_snap_seconds_to_grid(state, seconds, geom.visible_seconds);
                float best = grid_snap;
                float best_delta = fabsf(grid_snap - seconds);
                float clip_snap = seconds;
                if (snap_time_to_any_clip(state, sample_rate, snap_threshold, &clip_snap)) {
                    float clip_delta = fabsf(clip_snap - seconds);
                    if (clip_delta < best_delta) {
                        best = clip_snap;
                    }
                }
                seconds = best;
            }
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

        TimelineTrackHeaderLayout header_layout = {0};
        timeline_view_compute_track_header_layout(&timeline->rect,
                                                  lane_top,
                                                  geom.track_height,
                                                  geom.header_width,
                                                  &header_layout);
        SDL_Rect mute_rect = header_layout.mute_rect;
        SDL_Rect solo_rect = header_layout.solo_rect;

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

            SDL_Rect rect = timeline_lane_clip_rect(&geom, lane_top, clip_x, clip_w);

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

    if (state->timeline_automation_mode && state->automation_ui.dragging) {
        if (!is_down) {
            state->automation_ui.dragging = false;
            automation_commit_edit(state);
        } else {
            int track_index = state->automation_ui.track_index;
            int clip_index = state->automation_ui.clip_index;
            if (track_index >= 0 && track_index < track_count) {
                const EngineTrack* track = &tracks[track_index];
                if (track && clip_index >= 0 && clip_index < track->clip_count) {
                    const EngineClip* clip = &track->clips[clip_index];
                    uint64_t frame_count = clip->duration_frames;
                    if (frame_count == 0) {
                        frame_count = engine_sampler_get_frame_count(clip->sampler);
                    }
                    if (frame_count > 0) {
                        double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                        double clip_sec = (double)frame_count / (double)sample_rate;
                        float clip_offset = (float)(start_sec - geom.window_start_seconds);
                        int clip_x = geom.content_left + (int)round(clip_offset * geom.pixels_per_second);
                        int clip_w = (int)round(clip_sec * geom.pixels_per_second);
                        if (clip_w < 4) clip_w = 4;
                        int lane_top = geom.track_top + track_index * (geom.track_height + geom.track_spacing);
                        SDL_Rect clip_rect = timeline_lane_clip_rect(&geom, lane_top, clip_x, clip_w);

                        float seconds = timeline_x_to_seconds(&geom, state->mouse_x);
                        seconds = timeline_snap_seconds_to_grid(state, seconds, geom.visible_seconds);
                        float local = seconds - (float)start_sec;
                        if (local < 0.0f) local = 0.0f;
                        if (local > (float)clip_sec) local = (float)clip_sec;
                        uint64_t frame = (uint64_t)llroundf(local * (float)sample_rate);
                        float value = timeline_automation_value_from_y(&clip_rect, state->mouse_y);
                        int new_index = state->automation_ui.point_index;
                        engine_clip_update_automation_point(state->engine,
                                                            track_index,
                                                            clip_index,
                                                            state->automation_ui.target,
                                                            state->automation_ui.point_index,
                                                            frame,
                                                            value,
                                                            &new_index);
                        state->automation_ui.point_index = new_index;
                    }
                }
            }
        }
        return;
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
            input_manager_reset_meter_history_on_seek(state);
            engine_transport_seek(state->engine, frame);
            manager->last_click_clip = -1;
            manager->last_click_track = -1;
            manager->last_click_ticks = 0;
            timeline_input_mouse_drag_end(state);
            return;
        }
    }

    if (!was_down && is_down) {
        if (state->timeline_automation_mode && hit_clip >= 0 && hit_track >= 0) {
            const EngineClip* clip = &tracks[hit_track].clips[hit_clip];
            uint64_t frame_count = clip->duration_frames;
            if (frame_count == 0) {
                frame_count = engine_sampler_get_frame_count(clip->sampler);
            }
            if (frame_count > 0) {
                double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                double clip_sec = (double)frame_count / (double)sample_rate;
                float clip_offset = (float)(start_sec - geom.window_start_seconds);
                int clip_x = geom.content_left + (int)round(clip_offset * geom.pixels_per_second);
                int clip_w = (int)round(clip_sec * geom.pixels_per_second);
                if (clip_w < 4) clip_w = 4;
                int lane_top = geom.track_top + hit_track * (geom.track_height + geom.track_spacing);
                SDL_Rect clip_rect = timeline_lane_clip_rect(&geom, lane_top, clip_x, clip_w);

                automation_begin_edit(state, hit_track, hit_clip);
                const EngineAutomationLane* lane = NULL;
                engine_clip_get_automation_lane(state->engine,
                                                hit_track,
                                                hit_clip,
                                                state->automation_ui.target,
                                                &lane);
                int hit_point = timeline_automation_hit_point(lane,
                                                              frame_count,
                                                              start_sec,
                                                              sample_rate,
                                                              &geom,
                                                              &clip_rect,
                                                              state->mouse_x,
                                                              state->mouse_y);
                float seconds = timeline_x_to_seconds(&geom, state->mouse_x);
                seconds = timeline_snap_seconds_to_grid(state, seconds, geom.visible_seconds);
                float local = seconds - (float)start_sec;
                if (local < 0.0f) local = 0.0f;
                if (local > (float)clip_sec) local = (float)clip_sec;
                uint64_t frame = (uint64_t)llroundf(local * (float)sample_rate);
                float value = timeline_automation_value_from_y(&clip_rect, state->mouse_y);

                if (hit_point >= 0) {
                    state->automation_ui.track_index = hit_track;
                    state->automation_ui.clip_index = hit_clip;
                    state->automation_ui.point_index = hit_point;
                } else {
                    int new_index = -1;
                    engine_clip_add_automation_point(state->engine,
                                                     hit_track,
                                                     hit_clip,
                                                     state->automation_ui.target,
                                                     frame,
                                                     value,
                                                     &new_index);
                    state->automation_ui.track_index = hit_track;
                    state->automation_ui.clip_index = hit_clip;
                    state->automation_ui.point_index = new_index;
                }
                state->automation_ui.dragging = true;
                state->automation_ui.dragging_from_inspector = false;
                return;
            }
        }
        if (state->timeline_automation_mode) {
            state->automation_ui.point_index = -1;
        }
        for (int t = 0; t < track_count; ++t) {
            int lane_top = geom.track_top + t * (geom.track_height + geom.track_spacing);
            TimelineTrackHeaderLayout header_layout = {0};
            timeline_view_compute_track_header_layout(&timeline->rect,
                                                      lane_top,
                                                      geom.track_height,
                                                      geom.header_width,
                                                      &header_layout);
            SDL_Rect header_rect = header_layout.header_rect;
            if (SDL_PointInRect(&mouse_point, &header_rect)) {
                TrackNameEditor* editor = &state->track_name_editor;
                if (editor->editing && editor->track_index == t) {
                    editor->cursor = track_name_cursor_from_x(editor->buffer,
                                                              1.0f,
                                                              header_layout.text_x,
                                                              header_layout.text_max_x,
                                                              mouse_point.x);
                    return;
                }

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
                effects_panel_sync_from_engine(state);
                timeline_input_mouse_drag_end(state);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;

                if (double_click) {
                    track_name_editor_start(state, t);
                    TrackNameEditor* editor = &state->track_name_editor;
                    if (editor->editing) {
                        editor->cursor = track_name_cursor_from_x(editor->buffer,
                                                                  1.0f,
                                                                  header_layout.text_x,
                                                                  header_layout.text_max_x,
                                                                  mouse_point.x);
                    }
                }
                return;
            }
        }

        if (timeline_input_mouse_handle_clip_press(manager,
                                                   state,
                                                   &geom,
                                                   sample_rate,
                                                   hit_track,
                                                   hit_clip,
                                                   hit_left,
                                                   hit_right,
                                                   shift_held,
                                                   alt_held,
                                                   over_timeline)) {
            return;
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
