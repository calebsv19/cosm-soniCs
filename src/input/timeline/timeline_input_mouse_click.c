#include "input/timeline/timeline_input_mouse_click.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "undo/undo_manager.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "input/timeline_snap.h"
#include "input/timeline_selection.h"
#include "input/timeline_input.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include "ui/font.h"
#include "time/tempo.h"
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

static bool clip_state_from_clip(const EngineClip* clip, int track_index, UndoClipState* out_state) {
    if (!clip || !out_state) {
        return false;
    }
    out_state->sampler = clip->sampler;
    out_state->track_index = track_index;
    out_state->start_frame = clip->timeline_start_frames;
    out_state->offset_frames = clip->offset_frames;
    out_state->duration_frames = clip->duration_frames;
    out_state->fade_in_frames = clip->fade_in_frames;
    out_state->fade_out_frames = clip->fade_out_frames;
    out_state->gain = clip->gain;
    if (out_state->duration_frames == 0 && clip->sampler) {
        out_state->duration_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return true;
}

static void timeline_drag_begin_undo(AppState* state,
                                     const EngineTrack* track,
                                     int track_index,
                                     int clip_index,
                                     bool multi_move) {
    if (!state || !track || clip_index < 0 || clip_index >= track->clip_count) {
        return;
    }
    if (!multi_move) {
        UndoCommand cmd = {0};
        cmd.type = UNDO_CMD_CLIP_TRANSFORM;
        if (!clip_state_from_clip(&track->clips[clip_index], track_index, &cmd.data.clip_transform.before)) {
            return;
        }
        cmd.data.clip_transform.after = cmd.data.clip_transform.before;
        undo_manager_begin_drag(&state->undo, &cmd);
        return;
    }
    int count = state->selection_count;
    if (count <= 0) {
        return;
    }
    UndoClipState before[TIMELINE_MAX_SELECTION];
    UndoClipState after[TIMELINE_MAX_SELECTION];
    int filled = 0;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    for (int i = 0; i < count && filled < TIMELINE_MAX_SELECTION; ++i) {
        TimelineSelectionEntry entry = state->selection[i];
        if (!tracks || entry.track_index < 0 || entry.track_index >= track_count) {
            continue;
        }
        const EngineTrack* sel_track = &tracks[entry.track_index];
        if (!sel_track || entry.clip_index < 0 || entry.clip_index >= sel_track->clip_count) {
            continue;
        }
        if (clip_state_from_clip(&sel_track->clips[entry.clip_index], entry.track_index, &before[filled])) {
            after[filled] = before[filled];
            filled++;
        }
    }
    if (filled <= 0) {
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MULTI_CLIP_TRANSFORM;
    cmd.data.multi_clip_transform.count = filled;
    cmd.data.multi_clip_transform.before = before;
    cmd.data.multi_clip_transform.after = after;
    undo_manager_begin_drag(&state->undo, &cmd);
}

// Captures a ripple drag undo state for the anchor and downstream target clips.
static void timeline_drag_begin_ripple_undo(AppState* state,
                                            int track_index,
                                            int anchor_clip_index,
                                            const TimelineDragState* drag) {
    if (!state || !state->engine || !drag || track_index < 0 || anchor_clip_index < 0) {
        return;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index >= track_count) {
        return;
    }
    const EngineTrack* track = &tracks[track_index];
    if (!track || anchor_clip_index >= track->clip_count) {
        return;
    }
    int total = 1 + drag->ripple_target_count;
    if (total <= 0) {
        return;
    }
    UndoClipState* before = (UndoClipState*)SDL_calloc((size_t)total, sizeof(UndoClipState));
    UndoClipState* after = (UndoClipState*)SDL_calloc((size_t)total, sizeof(UndoClipState));
    if (!before || !after) {
        SDL_free(before);
        SDL_free(after);
        return;
    }
    int filled = 0;
    if (clip_state_from_clip(&track->clips[anchor_clip_index], track_index, &before[filled])) {
        after[filled] = before[filled];
        filled++;
    }
    for (int i = 0; i < drag->ripple_target_count && filled < total; ++i) {
        EngineSamplerSource* sampler = drag->ripple_targets[i];
        if (!sampler) {
            continue;
        }
        int clip_track = -1;
        int clip_index = -1;
        if (!timeline_find_clip_by_sampler(state, sampler, &clip_track, &clip_index)) {
            continue;
        }
        if (clip_track != track_index) {
            continue;
        }
        if (clip_index < 0 || clip_index >= track->clip_count) {
            continue;
        }
        if (clip_state_from_clip(&track->clips[clip_index], track_index, &before[filled])) {
            after[filled] = before[filled];
            filled++;
        }
    }
    if (filled <= 0) {
        SDL_free(before);
        SDL_free(after);
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MULTI_CLIP_TRANSFORM;
    cmd.data.multi_clip_transform.count = filled;
    cmd.data.multi_clip_transform.before = before;
    cmd.data.multi_clip_transform.after = after;
    undo_manager_begin_drag(&state->undo, &cmd);
    SDL_free(before);
    SDL_free(after);
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
                TrackNameEditor* editor = &state->track_name_editor;
                if (editor->editing && editor->track_index == t) {
                    int button_w = 18;
                    int button_spacing = 4;
                    int buttons_total = button_w * 2 + button_spacing;
                    int buttons_x = header_rect.x + header_rect.w - buttons_total - 8;
                    if (buttons_x < header_rect.x + 36) {
                        buttons_x = header_rect.x + 36;
                    }
                    int text_x = header_rect.x + 6;
                    int text_max_x = buttons_x - 4;
                    if (text_max_x < text_x) text_max_x = text_x;
                    editor->cursor = track_name_cursor_from_x(editor->buffer, 1.0f, text_x, text_max_x, mouse_point.x);
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
                timeline_input_mouse_drag_end(state);
                manager->last_click_clip = -1;
                manager->last_click_track = -1;
                manager->last_click_ticks = 0;

                if (double_click) {
                    track_name_editor_start(state, t);
                    TrackNameEditor* editor = &state->track_name_editor;
                    if (editor->editing) {
                        int button_w = 18;
                        int button_spacing = 4;
                        int buttons_total = button_w * 2 + button_spacing;
                        int buttons_x = header_rect.x + header_rect.w - buttons_total - 8;
                        if (buttons_x < header_rect.x + 36) {
                            buttons_x = header_rect.x + 36;
                        }
                        int text_x = header_rect.x + 6;
                        int text_max_x = buttons_x - 4;
                        if (text_max_x < text_x) text_max_x = text_x;
                        editor->cursor = track_name_cursor_from_x(editor->buffer, 1.0f, text_x, text_max_x, mouse_point.x);
                    }
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
                drag->pending_shift_select = true;
                drag->pending_shift_track = hit_track;
                drag->pending_shift_clip = hit_clip;
                drag->pending_shift_remove = already_selected;
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
                inspector_input_begin_rename(state);
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
            drag->mode = TIMELINE_DRAG_MODE_SLIDE;
            if (drag->trimming_left) {
                drag->mode = TIMELINE_DRAG_MODE_TRIM_LEFT;
            } else if (drag->trimming_right) {
                drag->mode = TIMELINE_DRAG_MODE_TRIM_RIGHT;
            } else if (fade_left) {
                drag->mode = TIMELINE_DRAG_MODE_FADE_IN;
            } else if (fade_right) {
                drag->mode = TIMELINE_DRAG_MODE_FADE_OUT;
            } else if (shift_held) {
                drag->mode = TIMELINE_DRAG_MODE_SLIP;
            } else if (alt_held) {
                drag->mode = TIMELINE_DRAG_MODE_RIPPLE;
            }
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
                    uint64_t offset_frames = 0;
                    int track_idx = entry.track_index;
                    if (refreshed_tracks && track_idx >= 0 && track_idx < refreshed_track_count) {
                        const EngineTrack* sel_track = &refreshed_tracks[track_idx];
                        if (sel_track && entry.clip_index >= 0 && entry.clip_index < sel_track->clip_count) {
                            const EngineClip* sel_clip = &sel_track->clips[entry.clip_index];
                            sampler = sel_clip->sampler;
                            start_frames = sel_clip->timeline_start_frames;
                            offset_frames = sel_clip->offset_frames;
                        }
                    }
                    drag->multi_samplers[s] = sampler;
                    drag->multi_initial_track[s] = track_idx;
                    drag->multi_initial_start[s] = start_frames;
                    drag->multi_initial_offset[s] = sampler ? offset_frames : 0;
                }
            } else {
                drag->multi_clip_count = 0;
            }
            if (drag->mode == TIMELINE_DRAG_MODE_RIPPLE) {
                drag->multi_move = false;
                drag->multi_clip_count = 0;
                if (drag->ripple_targets) {
                    SDL_free(drag->ripple_targets);
                    drag->ripple_targets = NULL;
                }
                drag->ripple_target_count = 0;
                drag->ripple_last_delta_frames = 0;
                int ripple_capacity = track ? track->clip_count : 0;
                if (ripple_capacity > 0) {
                    drag->ripple_targets = (EngineSamplerSource**)SDL_calloc((size_t)ripple_capacity,
                                                                             sizeof(EngineSamplerSource*));
                }
                if (drag->ripple_targets) {
                    for (int i = 0; i < track->clip_count; ++i) {
                        const EngineClip* ripple_clip = &track->clips[i];
                        if (!ripple_clip || !ripple_clip->sampler || ripple_clip->sampler == clip->sampler) {
                            continue;
                        }
                        if (ripple_clip->timeline_start_frames >= drag->initial_start_frames) {
                            drag->ripple_targets[drag->ripple_target_count++] = ripple_clip->sampler;
                        }
                    }
                }
            }
            if (drag->mode == TIMELINE_DRAG_MODE_RIPPLE) {
                timeline_drag_begin_ripple_undo(state, hit_track, hit_clip, drag);
            } else {
                timeline_drag_begin_undo(state, track, hit_track, hit_clip, drag->multi_move);
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
