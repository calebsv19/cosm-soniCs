#include "input/timeline/timeline_input_mouse_clip_press.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/input_manager.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_input.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "input/timeline_selection.h"
#include "undo/undo_manager.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"

#include <SDL2/SDL.h>
#include <string.h>

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
        if (!undo_clip_state_from_engine_clip(&track->clips[clip_index], track_index, &cmd.data.clip_transform.before)) {
            return;
        }
        if (!undo_clip_state_clone(&cmd.data.clip_transform.after, &cmd.data.clip_transform.before)) {
            undo_clip_state_clear(&cmd.data.clip_transform.before);
            return;
        }
        undo_manager_begin_drag(&state->undo, &cmd);
        undo_clip_state_clear(&cmd.data.clip_transform.before);
        undo_clip_state_clear(&cmd.data.clip_transform.after);
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
        if (undo_clip_state_from_engine_clip(&sel_track->clips[entry.clip_index], entry.track_index, &before[filled])) {
            if (undo_clip_state_clone(&after[filled], &before[filled])) {
                filled++;
            } else {
                undo_clip_state_clear(&before[filled]);
            }
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
    for (int i = 0; i < filled; ++i) {
        undo_clip_state_clear(&before[i]);
        undo_clip_state_clear(&after[i]);
    }
}

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
    if (undo_clip_state_from_engine_clip(&track->clips[anchor_clip_index], track_index, &before[filled])) {
        if (undo_clip_state_clone(&after[filled], &before[filled])) {
            filled++;
        } else {
            undo_clip_state_clear(&before[filled]);
        }
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
        if (undo_clip_state_from_engine_clip(&track->clips[clip_index], track_index, &before[filled])) {
            if (undo_clip_state_clone(&after[filled], &before[filled])) {
                filled++;
            } else {
                undo_clip_state_clear(&before[filled]);
            }
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
    for (int i = 0; i < filled; ++i) {
        undo_clip_state_clear(&before[i]);
        undo_clip_state_clear(&after[i]);
    }
    SDL_free(before);
    SDL_free(after);
}

bool timeline_input_mouse_handle_clip_press(InputManager* manager,
                                            AppState* state,
                                            const TimelineGeometry* geom,
                                            int sample_rate,
                                            int hit_track,
                                            int hit_clip,
                                            bool hit_left,
                                            bool hit_right,
                                            bool shift_held,
                                            bool alt_held,
                                            bool over_timeline) {
    if (!manager || !state || !state->engine || !geom) {
        return false;
    }

    TimelineDragState* drag = &state->timeline_drag;
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
        effects_panel_sync_from_engine(state);

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
            return true;
        }

        const EngineClip* clip = &track->clips[hit_clip];
        bool midi_clip = engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI;

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
            return true;
        }

        drag->multi_move = (state->selection_count > 1 && timeline_selection_contains(state, hit_track, hit_clip, NULL));

        drag->active = true;
        bool fade_left = !midi_clip && hit_left && !hit_right && alt_held;
        bool fade_right = !midi_clip && hit_right && !hit_left && alt_held;
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
        } else if (shift_held && !midi_clip) {
            drag->mode = TIMELINE_DRAG_MODE_SLIP;
        } else if (alt_held) {
            drag->mode = TIMELINE_DRAG_MODE_RIPPLE;
        }
        drag->track_index = hit_track;
        drag->clip_index = hit_clip;
        drag->start_mouse_x = state->mouse_x;
        drag->start_mouse_seconds = timeline_x_to_seconds(geom, state->mouse_x);
        drag->initial_start_frames = clip->timeline_start_frames;
        drag->initial_offset_frames = clip->offset_frames;
        drag->initial_duration_frames = clip->duration_frames;
        drag->started_moving = false;
        if (drag->initial_duration_frames == 0 && clip->sampler) {
            drag->initial_duration_frames = engine_sampler_get_frame_count(clip->sampler);
        }
        drag->clip_total_frames = engine_clip_get_total_frames(state->engine, hit_track, hit_clip);
        if (drag->clip_total_frames == 0) {
            drag->clip_total_frames = drag->initial_offset_frames + drag->initial_duration_frames;
        }
        if (drag->initial_midi_notes) {
            SDL_free(drag->initial_midi_notes);
            drag->initial_midi_notes = NULL;
        }
        drag->initial_midi_note_count = 0;
        if (midi_clip) {
            int note_count = engine_clip_midi_note_count(clip);
            const EngineMidiNote* notes = engine_clip_midi_notes(clip);
            if (note_count > 0 && notes) {
                drag->initial_midi_notes = (EngineMidiNote*)SDL_malloc(sizeof(EngineMidiNote) * (size_t)note_count);
                if (drag->initial_midi_notes) {
                    memcpy(drag->initial_midi_notes, notes, sizeof(EngineMidiNote) * (size_t)note_count);
                    drag->initial_midi_note_count = note_count;
                }
            }
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
        return true;
    }

    if (!hit_left && !hit_right && over_timeline) {
        if (state->layout_runtime.drag.active) {
            return true;
        }
        timeline_selection_clear(state);
        effects_panel_sync_from_engine(state);
        timeline_input_mouse_drag_end(state);
        return true;
    }

    return false;
}
