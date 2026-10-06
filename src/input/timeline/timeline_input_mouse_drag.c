#include "input/timeline/timeline_input_mouse_drag.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline_snap.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_midi_trim.h"
#include "input/timeline_selection.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/effects_panel.h"
#include "ui/timeline_view.h"
#include "undo/undo_manager.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

#define MIN_CLIP_DURATION_FRAMES 1

static void timeline_marquee_clear(AppState* state) {
    if (!state) return;
    state->timeline_marquee_active = false;
    state->timeline_marquee_rect = (SDL_Rect){0,0,0,0};
    state->timeline_marquee_extend = false;
    state->timeline_marquee_start_x = 0;
    state->timeline_marquee_start_y = 0;
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

static bool clip_state_equal(const UndoClipState* a, const UndoClipState* b) {
    if (!a || !b) {
        return true;
    }
    if (a->track_index != b->track_index ||
        a->start_frame != b->start_frame ||
        a->offset_frames != b->offset_frames ||
        a->duration_frames != b->duration_frames ||
        a->fade_in_frames != b->fade_in_frames ||
        a->fade_out_frames != b->fade_out_frames ||
        a->fade_in_curve != b->fade_in_curve ||
        a->fade_out_curve != b->fade_out_curve ||
        fabsf(a->gain - b->gain) >= 0.0001f ||
        a->midi_note_count != b->midi_note_count) {
        return false;
    }
    if (a->midi_note_count <= 0) {
        return true;
    }
    if (!a->midi_notes || !b->midi_notes) {
        return false;
    }
    return memcmp(a->midi_notes,
                  b->midi_notes,
                  sizeof(EngineMidiNote) * (size_t)a->midi_note_count) == 0;
}

static const EngineClip* timeline_drag_current_clip(const AppState* state, const TimelineDragState* drag) {
    if (!state || !state->engine || !drag) {
        return NULL;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || drag->track_index < 0 || drag->track_index >= track_count) {
        return NULL;
    }
    const EngineTrack* track = &tracks[drag->track_index];
    if (drag->clip_index < 0 || drag->clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[drag->clip_index];
}

static bool timeline_find_clip_by_undo_state(const AppState* state,
                                             const UndoClipState* snapshot,
                                             int* out_track,
                                             int* out_clip) {
    if (!state || !snapshot || !out_track || !out_clip) {
        return false;
    }
    if (snapshot->sampler &&
        timeline_find_clip_by_sampler(state, snapshot->sampler, out_track, out_clip)) {
        return true;
    }
    if (snapshot->kind != ENGINE_CLIP_KIND_MIDI || snapshot->creation_index == 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks) {
        return false;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) {
            continue;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            const EngineClip* clip = &track->clips[c];
            if (clip && engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI &&
                clip->creation_index == snapshot->creation_index) {
                *out_track = t;
                *out_clip = c;
                return true;
            }
        }
    }
    return false;
}

static bool snap_to_neighbor_clip(const EngineTrack* track, int exclude_index, int sample_rate, float threshold_sec, float* inout_start_sec) {
    if (!track || !inout_start_sec || threshold_sec <= 0.0f || sample_rate <= 0) {
        return false;
    }
    bool snapped = false;
    float best_delta = threshold_sec + 1.0f;
    float start_sec = *inout_start_sec;
    for (int i = 0; i < track->clip_count; ++i) {
        if (i == exclude_index) continue;
        const EngineClip* other = &track->clips[i];
        if (!other || !other->sampler) continue;
        uint64_t frames = other->duration_frames;
        if (frames == 0) {
            frames = engine_sampler_get_frame_count(other->sampler);
        }
        float other_start = (float)other->timeline_start_frames / (float)sample_rate;
        float other_end = other_start + (float)frames / (float)sample_rate;
        float delta_start = fabsf(start_sec - other_start);
        if (delta_start <= threshold_sec && delta_start < best_delta) {
            best_delta = delta_start;
            *inout_start_sec = other_start;
            snapped = true;
        }
        float delta_end = fabsf(start_sec - other_end);
        if (delta_end <= threshold_sec && delta_end < best_delta) {
            best_delta = delta_end;
            *inout_start_sec = other_end;
            snapped = true;
        }
    }
    return snapped;
}

void timeline_input_mouse_drag_end(AppState* state) {
    if (!state) {
        return;
    }
    if (state->undo.active_drag_valid) {
        undo_manager_cancel_drag(&state->undo);
    }
    state->timeline_drag.active = false;
    state->timeline_drag.trimming_left = false;
    state->timeline_drag.trimming_right = false;
    state->timeline_drag.adjusting_fade_in = false;
    state->timeline_drag.adjusting_fade_out = false;
    state->timeline_drag.mode = TIMELINE_DRAG_MODE_SLIDE;
    if (state->timeline_drag.ripple_targets) {
        SDL_free(state->timeline_drag.ripple_targets);
        state->timeline_drag.ripple_targets = NULL;
    }
    if (state->timeline_drag.initial_midi_notes) {
        SDL_free(state->timeline_drag.initial_midi_notes);
        state->timeline_drag.initial_midi_notes = NULL;
    }
    state->timeline_drag.initial_midi_note_count = 0;
    state->timeline_drag.ripple_target_count = 0;
    state->timeline_drag.ripple_last_delta_frames = 0;
    state->timeline_drag.pending_shift_select = false;
    state->timeline_drag.pending_shift_remove = false;
    state->timeline_drag.pending_shift_track = -1;
    state->timeline_drag.pending_shift_clip = -1;
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
        state->timeline_drag.multi_initial_offset[i] = 0;
    }
    timeline_marquee_clear(state);
}

void timeline_input_mouse_drag_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    (void)manager;
    if (!state || !state->engine) {
        return;
    }
    if (state->timeline_automation_mode) {
        timeline_input_mouse_drag_end(state);
        return;
    }
    if (state->timeline_tempo_overlay_enabled && state->tempo_overlay_ui.dragging) {
        timeline_input_mouse_drag_end(state);
        return;
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
    SDL_Keymod mods = SDL_GetModState();
    bool alt_held = (mods & KMOD_ALT) != 0;

    if (!is_down && was_down) {
        if (drag->active && drag->started_moving && drag->mode == TIMELINE_DRAG_MODE_SLIDE && !drag->trimming_left && !drag->trimming_right &&
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
                if (timeline_apply_compound_drop(state, frame_delta, track_offset)) {
                    UndoMultiClipTransform* history = &state->undo.active_drag.data.multi_clip_transform;
                    for (int i = 0; i < history->count; ++i) {
                        int t, c;
                        if (timeline_find_clip_by_undo_state(state, &history->after[i], &t, &c))
                            add_unique_sampler(overlap_targets, &overlap_target_count, TIMELINE_MAX_SELECTION + 1,
                                               engine_get_tracks(state->engine)[t].clips[c].sampler);
                    }
                    effects_panel_sync_from_engine(state);
                } else {
                    overlap_target_count = 0;
                }
            } else if (state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_CLIP_TRANSFORM) {
                int track_offset = dst_track - drag->track_index;
                int64_t frame_delta = (int64_t)start_frame - (int64_t)drag->initial_start_frames;
                if (timeline_apply_compound_drop(state, frame_delta, track_offset)) effects_panel_sync_from_engine(state);
                overlap_target_count = 0;
            }

            if (state->undo.active_drag.clip_content_after) overlap_target_count = 0;
            for (int i = 0; i < overlap_target_count; ++i) {
                EngineSamplerSource* sampler = overlap_targets[i];
                if (drag->mode == TIMELINE_DRAG_MODE_RIPPLE && sampler == anchor_sampler) {
                    continue;
                }
                int target_track = -1;
                int target_clip = -1;
                if (timeline_find_clip_by_sampler(state, sampler, &target_track, &target_clip)) {
                    engine_track_apply_no_overlap(state->engine, target_track, sampler, &target_clip);
                }
            }
        }
        if (state->undo.active_drag_valid) {
            bool changed = state->undo.active_drag.clip_content_after != NULL;
            UndoCommand* cmd = &state->undo.active_drag;
            if (cmd->type == UNDO_CMD_CLIP_TRANSFORM) {
                UndoClipState after = {0};
                int found_track = -1;
                int found_clip = -1;
                if (timeline_find_clip_by_undo_state(state,
                                                     &cmd->data.clip_transform.before,
                                                     &found_track,
                                                     &found_clip)) {
                    const EngineTrack* final_tracks = engine_get_tracks(state->engine);
                    int final_track_count = engine_get_track_count(state->engine);
                    if (final_tracks && found_track >= 0 && found_track < final_track_count) {
                        const EngineTrack* final_track = &final_tracks[found_track];
                        if (final_track && found_clip >= 0 && found_clip < final_track->clip_count) {
                            if (undo_clip_state_capture(state->engine, &final_track->clips[found_clip], found_track, &after)) {
                                undo_clip_state_clear(&cmd->data.clip_transform.after);
                                cmd->data.clip_transform.after = after;
                                if (!clip_state_equal(&cmd->data.clip_transform.before, &cmd->data.clip_transform.after)) {
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            } else if (cmd->type == UNDO_CMD_MULTI_CLIP_TRANSFORM) {
                int count = cmd->data.multi_clip_transform.count;
                for (int i = 0; i < count; ++i) {
                    int found_track = -1;
                    int found_clip = -1;
                    if (!timeline_find_clip_by_undo_state(state,
                                                         &cmd->data.multi_clip_transform.before[i],
                                                         &found_track,
                                                         &found_clip)) {
                        continue;
                    }
                    const EngineTrack* final_tracks = engine_get_tracks(state->engine);
                    int final_track_count = engine_get_track_count(state->engine);
                    if (!final_tracks || found_track < 0 || found_track >= final_track_count) {
                        continue;
                    }
                    const EngineTrack* final_track = &final_tracks[found_track];
                    if (!final_track || found_clip < 0 || found_clip >= final_track->clip_count) {
                        continue;
                    }
                    UndoClipState after = {0};
                    if (undo_clip_state_capture(state->engine, &final_track->clips[found_clip], found_track, &after)) {
                        undo_clip_state_clear(&cmd->data.multi_clip_transform.after[i]);
                        cmd->data.multi_clip_transform.after[i] = after;
                        if (!clip_state_equal(&cmd->data.multi_clip_transform.before[i],
                                              &cmd->data.multi_clip_transform.after[i])) {
                            changed = true;
                        }
                    }
                }
            }
            if (changed) {
                undo_manager_commit_drag(&state->undo, cmd);
            } else {
                undo_manager_cancel_drag(&state->undo);
            }
        }
        timeline_input_mouse_drag_end(state);
        return;
    }

    if (!drag->active || !is_down || state->selected_clip_index < 0) {
        return;
    }

    const EngineTrack* refreshed_tracks = engine_get_tracks(state->engine);
    int refreshed_track_count = engine_get_track_count(state->engine);
    if (!refreshed_tracks || drag->track_index < 0 || drag->track_index >= refreshed_track_count) {
        timeline_input_mouse_drag_end(state);
        return;
    }
    const EngineTrack* drag_track = &refreshed_tracks[drag->track_index];
    if (!drag_track || drag->clip_index < 0 || drag->clip_index >= drag_track->clip_count) {
        timeline_input_mouse_drag_end(state);
        return;
    }
    const EngineClip* drag_clip = &drag_track->clips[drag->clip_index];
    bool drag_midi_clip = engine_clip_get_kind(drag_clip) == ENGINE_CLIP_KIND_MIDI;

    if (!drag->trimming_left && !drag->trimming_right && !drag->adjusting_fade_in && !drag->adjusting_fade_out) {
        if (drag->mode == TIMELINE_DRAG_MODE_SLIP || drag->mode == TIMELINE_DRAG_MODE_RIPPLE) {
            drag->destination_track_index = drag->track_index;
        } else {
            int hover_track = timeline_track_at_position(state, state->mouse_y, geom.track_height, geom.track_spacing);
            if (hover_track < 0) {
                hover_track = drag->track_index;
            }
            if (hover_track >= track_count) {
                hover_track = track_count > 0 ? track_count - 1 : drag->track_index;
            }
            drag->destination_track_index = hover_track;
        }
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
    if (drag_midi_clip) {
        trim_min_start_sec = 0.0f;
    }
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
                drag->pending_shift_select = false;
            } else {
                return;
            }
        }
        if (drag->mode == TIMELINE_DRAG_MODE_SLIP) {
            if (drag->multi_move) {
                (void)timeline_apply_compound_preview(state, (int64_t)llroundf(delta_sec * (float)sample_rate), true);
                return;
            }
            int64_t delta_frames = (int64_t)llroundf(delta_sec * (float)sample_rate);
            int64_t max_offset = (int64_t)drag->clip_total_frames - (int64_t)drag->initial_duration_frames;
            if (max_offset < 0) {
                max_offset = 0;
            }
            int64_t new_offset = (int64_t)drag->initial_offset_frames + delta_frames;
            if (new_offset < 0) new_offset = 0;
            if (new_offset > max_offset) new_offset = max_offset;

            engine_clip_set_region(state->engine,
                                   drag->track_index,
                                   drag->clip_index,
                                   (uint64_t)new_offset,
                                   drag->initial_duration_frames);
            inspector_input_set_clip(state, drag->track_index, drag->clip_index);


            return;
        }
        float new_start_sec = initial_start_sec + delta_sec;
        float snap_interval = timeline_get_snap_interval_seconds(state, geom.visible_seconds);
        bool allow_snap = state->timeline_snap_enabled &&
                          (!alt_held || drag->mode == TIMELINE_DRAG_MODE_RIPPLE);
        if (drag->started_moving && allow_snap) {
            new_start_sec = timeline_snap_seconds_to_grid(state, new_start_sec, geom.visible_seconds);
            snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        }
        if (new_start_sec < move_min_start_sec) {
            new_start_sec = move_min_start_sec;
        }

        int64_t target_start_frames = (int64_t)llroundf(new_start_sec * (float)sample_rate);
        if (target_start_frames < 0) {
            target_start_frames = 0;
        }
        uint64_t new_start_frames = (uint64_t)target_start_frames;
        if (drag->multi_move || drag->mode == TIMELINE_DRAG_MODE_RIPPLE) {
            if (timeline_apply_compound_preview(state, target_start_frames - (int64_t)drag->initial_start_frames, false)) {
                drag->current_start_seconds = (float)new_start_frames / (float)sample_rate;
                drag->ripple_last_delta_frames = target_start_frames - (int64_t)drag->initial_start_frames;
            }
            return;
        }
        int old_index = drag->clip_index;
        int new_index = drag->clip_index;
        if (engine_clip_set_timeline_start(state->engine, drag->track_index, drag->clip_index, (uint64_t)new_start_frames, &new_index)) {
            drag->clip_index = new_index;
            timeline_selection_update_index(state, drag->track_index, old_index, new_index);
            inspector_input_set_clip(state, drag->track_index, new_index);
        }



    } else if (drag->trimming_left) {
        float new_start_sec = mouse_seconds;
        if (new_start_sec < 0.0f) {
            new_start_sec = 0.0f;
        }
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float initial_duration_sec = (float)drag->initial_duration_frames / (float)sample_rate;
        float max_start_sec = initial_start_sec + initial_duration_sec - (1.0f / (float)sample_rate);
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        float snap_interval = timeline_get_snap_interval_seconds(state, geom.visible_seconds);
        bool allow_snap = state->timeline_snap_enabled &&
                          (!alt_held || drag->mode == TIMELINE_DRAG_MODE_RIPPLE);
        if (allow_snap) {
            new_start_sec = timeline_snap_seconds_to_grid(state, new_start_sec, geom.visible_seconds);
        }
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }
        if (allow_snap) {
            snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_start_sec);
        }
        if (new_start_sec < trim_min_start_sec) {
            new_start_sec = trim_min_start_sec;
        }
        if (new_start_sec > max_start_sec) {
            new_start_sec = max_start_sec;
        }

        uint64_t new_start_frames = (uint64_t)llroundf(new_start_sec * (float)sample_rate);
        if (drag_midi_clip) {
            int old_index = drag->clip_index;
            int new_index = drag->clip_index;
            if (timeline_midi_left_trim_apply_from_notes(state->engine,
                                                         drag->track_index,
                                                         &new_index,
                                                         drag->initial_start_frames,
                                                         drag->initial_duration_frames,
                                                         drag->initial_midi_notes,
                                                         drag->initial_midi_note_count,
                                                         new_start_frames)) {
                drag->clip_index = new_index;
                timeline_selection_update_index(state, drag->track_index, old_index, new_index);
                inspector_input_set_clip(state, drag->track_index, new_index);
            }
            return;
        }
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

        timeline_apply_audio_trim(state, new_start_frames, (uint64_t)new_offset, (uint64_t)new_duration);
    } else if (drag->trimming_right) {
        const EngineClip* current_clip = timeline_drag_current_clip(state, drag);
        bool midi_clip = current_clip && engine_clip_get_kind(current_clip) == ENGINE_CLIP_KIND_MIDI;
        float new_right_sec = mouse_seconds;
        if (new_right_sec < 0.0f) {
            new_right_sec = 0.0f;
        }
        float initial_start_sec = (float)drag->initial_start_frames / (float)sample_rate;
        float min_right_sec = initial_start_sec + (1.0f / (float)sample_rate);
        uint64_t midi_min_duration = 1u;
        if (midi_clip) {
            midi_min_duration = timeline_clip_midi_min_duration_frames(current_clip);
            float midi_min_right_sec = initial_start_sec + (float)midi_min_duration / (float)sample_rate;
            if (min_right_sec < midi_min_right_sec) {
                min_right_sec = midi_min_right_sec;
            }
        }
        float max_right_sec = 0.0f;
        if (!midi_clip) {
            max_right_sec = (float)drag->clip_total_frames / (float)sample_rate;
            max_right_sec -= (float)drag->initial_offset_frames / (float)sample_rate;
            max_right_sec += initial_start_sec;
        }
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (!midi_clip && new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }
        float snap_interval = timeline_get_snap_interval_seconds(state, geom.visible_seconds);
        bool allow_snap = state->timeline_snap_enabled &&
                          (!alt_held || drag->mode == TIMELINE_DRAG_MODE_RIPPLE);
        if (allow_snap) {
            new_right_sec = timeline_snap_seconds_to_grid(state, new_right_sec, geom.visible_seconds);
        }
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (!midi_clip && new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }
        if (allow_snap) {
            snap_to_neighbor_clip(drag_track, drag->clip_index, sample_rate, snap_interval, &new_right_sec);
        }
        if (new_right_sec < min_right_sec) {
            new_right_sec = min_right_sec;
        }
        if (!midi_clip && new_right_sec > max_right_sec) {
            new_right_sec = max_right_sec;
        }

        float new_duration_sec = new_right_sec - initial_start_sec;
        if (new_duration_sec < 1.0f / (float)sample_rate) {
            new_duration_sec = 1.0f / (float)sample_rate;
        }
        uint64_t new_duration_frames = (uint64_t)llroundf(new_duration_sec * (float)sample_rate);
        if (!midi_clip && drag->clip_total_frames > 0 &&
            drag->initial_offset_frames + new_duration_frames > drag->clip_total_frames) {
            new_duration_frames = drag->clip_total_frames - drag->initial_offset_frames;
        }
        if (midi_clip && new_duration_frames < midi_min_duration) {
            new_duration_frames = midi_min_duration;
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
