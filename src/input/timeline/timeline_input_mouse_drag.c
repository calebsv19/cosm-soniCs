#include "input/timeline/timeline_input_mouse_drag.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline_snap.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_selection.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/effects_panel.h"
#include "ui/timeline_view.h"
#include "undo/undo_manager.h"
#include <SDL2/SDL.h>
#include <math.h>

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

static bool clip_state_from_clip(const EngineClip* clip, int track_index, UndoClipState* out_state) {
    if (!clip || !out_state) {
        return false;
    }
    out_state->kind = engine_clip_get_kind(clip);
    out_state->sampler = clip->sampler;
    out_state->creation_index = clip->creation_index;
    out_state->track_index = track_index;
    out_state->start_frame = clip->timeline_start_frames;
    out_state->offset_frames = clip->offset_frames;
    out_state->duration_frames = clip->duration_frames;
    out_state->fade_in_frames = clip->fade_in_frames;
    out_state->fade_out_frames = clip->fade_out_frames;
    out_state->fade_in_curve = clip->fade_in_curve;
    out_state->fade_out_curve = clip->fade_out_curve;
    out_state->gain = clip->gain;
    if (out_state->duration_frames == 0 && clip->sampler) {
        out_state->duration_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return true;
}

static bool clip_state_equal(const UndoClipState* a, const UndoClipState* b) {
    if (!a || !b) {
        return true;
    }
    return a->track_index == b->track_index &&
           a->start_frame == b->start_frame &&
           a->offset_frames == b->offset_frames &&
           a->duration_frames == b->duration_frames &&
           a->fade_in_frames == b->fade_in_frames &&
           a->fade_out_frames == b->fade_out_frames &&
           a->fade_in_curve == b->fade_in_curve &&
           a->fade_out_curve == b->fade_out_curve &&
           fabsf(a->gain - b->gain) < 0.0001f;
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

// Applies a ripple delta to downstream clips based on a precomputed target list.
static void timeline_apply_ripple_delta(AppState* state,
                                        int track_index,
                                        EngineSamplerSource** targets,
                                        int target_count,
                                        int64_t delta_frames) {
    if (!state || !state->engine || !targets || target_count <= 0 || delta_frames == 0) {
        return;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return;
    }
    for (int i = 0; i < target_count; ++i) {
        EngineSamplerSource* sampler = targets[i];
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
        const EngineTrack* track = engine_get_tracks(state->engine);
        int track_total = engine_get_track_count(state->engine);
        if (!track || clip_track < 0 || clip_track >= track_total) {
            continue;
        }
        const EngineTrack* target_track = &track[clip_track];
        if (!target_track || clip_index < 0 || clip_index >= target_track->clip_count) {
            continue;
        }
        const EngineClip* clip = &target_track->clips[clip_index];
        if (!clip) {
            continue;
        }
        int64_t start_frames = (int64_t)clip->timeline_start_frames + delta_frames;
        if (start_frames < 0) {
            start_frames = 0;
        }
        int updated_index = clip_index;
        if (engine_clip_set_timeline_start(state->engine,
                                           clip_track,
                                           clip_index,
                                           (uint64_t)start_frames,
                                           &updated_index)) {
            timeline_selection_update_index(state, clip_track, clip_index, updated_index);
        }
    }
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
                effects_panel_sync_from_engine(state);
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
                    effects_panel_sync_from_engine(state);
                }
            }

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
            bool changed = false;
            UndoCommand* cmd = &state->undo.active_drag;
            if (cmd->type == UNDO_CMD_CLIP_TRANSFORM) {
                UndoClipState after = {0};
                int found_track = -1;
                int found_clip = -1;
                if (timeline_find_clip_by_sampler(state, cmd->data.clip_transform.before.sampler,
                                                  &found_track, &found_clip)) {
                    const EngineTrack* final_tracks = engine_get_tracks(state->engine);
                    int final_track_count = engine_get_track_count(state->engine);
                    if (final_tracks && found_track >= 0 && found_track < final_track_count) {
                        const EngineTrack* final_track = &final_tracks[found_track];
                        if (final_track && found_clip >= 0 && found_clip < final_track->clip_count) {
                            if (clip_state_from_clip(&final_track->clips[found_clip], found_track, &after)) {
                                cmd->data.clip_transform.after = after;
                                if (!clip_state_equal(&cmd->data.clip_transform.before, &after)) {
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            } else if (cmd->type == UNDO_CMD_MULTI_CLIP_TRANSFORM) {
                int count = cmd->data.multi_clip_transform.count;
                for (int i = 0; i < count; ++i) {
                    EngineSamplerSource* sampler = cmd->data.multi_clip_transform.before[i].sampler;
                    if (!sampler) {
                        continue;
                    }
                    int found_track = -1;
                    int found_clip = -1;
                    if (!timeline_find_clip_by_sampler(state, sampler, &found_track, &found_clip)) {
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
                    if (clip_state_from_clip(&final_track->clips[found_clip], found_track, &after)) {
                        cmd->data.multi_clip_transform.after[i] = after;
                        if (!clip_state_equal(&cmd->data.multi_clip_transform.before[i], &after)) {
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

            if (drag->multi_move) {
                EngineSamplerSource* anchor_sampler = drag_clip ? drag_clip->sampler : NULL;
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
                    uint64_t clip_total = engine_clip_get_total_frames(state->engine, clip_track, clip_idx);
                    uint64_t clip_duration = 0;
                    const EngineTrack* clip_tracks = engine_get_tracks(state->engine);
                    int clip_track_count = engine_get_track_count(state->engine);
                    if (clip_tracks && clip_track >= 0 && clip_track < clip_track_count) {
                        const EngineTrack* clip_track_ptr = &clip_tracks[clip_track];
                        if (clip_track_ptr && clip_idx >= 0 && clip_idx < clip_track_ptr->clip_count) {
                            const EngineClip* clip = &clip_track_ptr->clips[clip_idx];
                            clip_duration = clip->duration_frames;
                            if (clip_duration == 0 && clip->sampler) {
                                clip_duration = engine_sampler_get_frame_count(clip->sampler);
                            }
                        }
                    }
                    if (clip_duration == 0) {
                        continue;
                    }
                    int64_t max_clip_offset = (int64_t)clip_total - (int64_t)clip_duration;
                    if (max_clip_offset < 0) {
                        max_clip_offset = 0;
                    }
                    int64_t clip_offset = (int64_t)drag->multi_initial_offset[s] + delta_frames;
                    if (clip_offset < 0) clip_offset = 0;
                    if (clip_offset > max_clip_offset) clip_offset = max_clip_offset;
                    engine_clip_set_region(state->engine, clip_track, clip_idx,
                                           (uint64_t)clip_offset, clip_duration);
                }
            }
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
        int old_index = drag->clip_index;
        int new_index = drag->clip_index;
        if (engine_clip_set_timeline_start(state->engine, drag->track_index, drag->clip_index, (uint64_t)new_start_frames, &new_index)) {
            drag->clip_index = new_index;
            timeline_selection_update_index(state, drag->track_index, old_index, new_index);
            inspector_input_set_clip(state, drag->track_index, new_index);
        }
        if (drag->mode == TIMELINE_DRAG_MODE_RIPPLE && drag->ripple_target_count > 0) {
            int64_t delta_frames = (int64_t)new_start_frames - (int64_t)drag->initial_start_frames;
            int64_t delta_step = delta_frames - drag->ripple_last_delta_frames;
            if (delta_step != 0) {
                timeline_apply_ripple_delta(state,
                                            drag->track_index,
                                            drag->ripple_targets,
                                            drag->ripple_target_count,
                                            delta_step);
                drag->ripple_last_delta_frames = delta_frames;
            }
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
        if (drag->mode == TIMELINE_DRAG_MODE_RIPPLE && drag->ripple_target_count > 0) {
            uint64_t initial_end = drag->initial_start_frames + drag->initial_duration_frames;
            uint64_t new_end = drag->initial_start_frames + new_duration_frames;
            int64_t delta_frames = (int64_t)new_end - (int64_t)initial_end;
            int64_t delta_step = delta_frames - drag->ripple_last_delta_frames;
            if (delta_step != 0) {
                timeline_apply_ripple_delta(state,
                                            drag->track_index,
                                            drag->ripple_targets,
                                            drag->ripple_target_count,
                                            delta_step);
                drag->ripple_last_delta_frames = delta_frames;
            }
        }
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
