#include "undo/undo_manager.h"
#include "undo_manager_internal.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/midi_editor_input.h"
#include "input/timeline_drag.h"
#include "input/timeline_selection.h"
#include "input/library_input.h"
#include "ui/effects_panel.h"
#include "ui/library_browser.h"
#include "effects/param_utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void eq_curve_from_session(EqCurveState* dst, const SessionEqCurve* src) {
    if (!dst || !src) {
        return;
    }
    dst->low_cut.enabled = src->low_cut.enabled;
    dst->low_cut.freq_hz = clamp_float(src->low_cut.freq_hz, 20.0f, 20000.0f);
    dst->low_cut.slope = src->low_cut.slope;
    dst->high_cut.enabled = src->high_cut.enabled;
    dst->high_cut.freq_hz = clamp_float(src->high_cut.freq_hz, 20.0f, 20000.0f);
    dst->high_cut.slope = src->high_cut.slope;
    for (int i = 0; i < 4; ++i) {
        dst->bands[i].enabled = src->bands[i].enabled;
        dst->bands[i].freq_hz = clamp_float(src->bands[i].freq_hz, 20.0f, 20000.0f);
        dst->bands[i].gain_db = clamp_float(src->bands[i].gain_db, -20.0f, 20.0f);
        dst->bands[i].q_width = clamp_float(src->bands[i].q_width, 0.1f, 4.0f);
    }
    dst->selected_band = -1;
    dst->selected_handle = EQ_CURVE_HANDLE_NONE;
    dst->hover_band = -1;
    dst->hover_handle = EQ_CURVE_HANDLE_NONE;
    dst->hover_toggle_band = -1;
    dst->hover_toggle_low = false;
    dst->hover_toggle_high = false;
}

static void eq_curve_to_engine(const EqCurveState* src, EngineEqCurve* dst) {
    if (!src || !dst) {
        return;
    }
    dst->low_cut.enabled = src->low_cut.enabled;
    dst->low_cut.freq_hz = src->low_cut.freq_hz;
    dst->high_cut.enabled = src->high_cut.enabled;
    dst->high_cut.freq_hz = src->high_cut.freq_hz;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        dst->bands[i].enabled = src->bands[i].enabled;
        dst->bands[i].freq_hz = src->bands[i].freq_hz;
        dst->bands[i].gain_db = src->bands[i].gain_db;
        dst->bands[i].q_width = src->bands[i].q_width;
    }
}

static int find_clip_by_snapshot(const EngineTrack* track, const SessionClip* clip) {
    if (!track || !clip) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        const EngineClip* current = &track->clips[i];
        if (!current) {
            continue;
        }
        if (engine_clip_get_kind(current) != clip->kind) {
            continue;
        }
        if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
            if (current->timeline_start_frames == clip->start_frame &&
                current->duration_frames == clip->duration_frames &&
                strncmp(current->name, clip->name, ENGINE_CLIP_NAME_MAX) == 0) {
                return i;
            }
            continue;
        }
        if (!current->sampler) {
            continue;
        }
        const char* media_path = engine_clip_get_media_path(current);
        if (current->timeline_start_frames == clip->start_frame &&
            media_path && strncmp(media_path, clip->media_path, ENGINE_CLIP_PATH_MAX) == 0) {
            return i;
        }
    }
    return -1;
}

static bool apply_clip_add_remove(AppState* state, UndoClipAddRemove* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    bool do_add = apply_after ? edit->added : !edit->added;
    if (do_add) {
        int new_index = -1;
        if (edit->clip.kind == ENGINE_CLIP_KIND_MIDI) {
            if (!engine_add_midi_clip_to_track(state->engine,
                                               edit->track_index,
                                               edit->clip.start_frame,
                                               edit->clip.duration_frames,
                                               &new_index)) {
                return false;
            }
        } else {
            if (!engine_add_clip_to_track_with_id(state->engine,
                                                  edit->track_index,
                                                  edit->clip.media_path,
                                                  edit->clip.media_id,
                                                  edit->clip.start_frame,
                                                  &new_index)) {
                return false;
            }
        }
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (!tracks || edit->track_index < 0 ||
            edit->track_index >= engine_get_track_count(state->engine)) {
            return false;
        }
        const EngineTrack* track = &tracks[edit->track_index];
        if (new_index < 0 || new_index >= track->clip_count) {
            return false;
        }
        const EngineClip* clip = &track->clips[new_index];
        edit->sampler = clip->sampler;
        if (edit->clip.name[0] != '\0') {
            engine_clip_set_name(state->engine, edit->track_index, new_index, edit->clip.name);
        }
        engine_clip_set_gain(state->engine, edit->track_index, new_index, edit->clip.gain);
        engine_clip_set_region(state->engine, edit->track_index, new_index,
                               edit->clip.offset_frames, edit->clip.duration_frames);
        engine_clip_set_fades(state->engine, edit->track_index, new_index,
                              edit->clip.fade_in_frames, edit->clip.fade_out_frames);
        engine_clip_set_fade_curves(state->engine,
                                    edit->track_index,
                                    new_index,
                                    edit->clip.fade_in_curve,
                                    edit->clip.fade_out_curve);
        if (edit->clip.kind == ENGINE_CLIP_KIND_MIDI) {
            engine_clip_midi_set_instrument_preset(state->engine,
                                                   edit->track_index,
                                                   new_index,
                                                   edit->clip.instrument_preset);
            engine_clip_midi_set_instrument_params(state->engine,
                                                   edit->track_index,
                                                   new_index,
                                                   edit->clip.instrument_params);
            engine_clip_midi_set_inherits_track_instrument(state->engine,
                                                           edit->track_index,
                                                           new_index,
                                                           edit->clip.instrument_inherits_track);
            for (int n = 0; n < edit->clip.midi_note_count; ++n) {
                engine_clip_midi_add_note(state->engine,
                                          edit->track_index,
                                          new_index,
                                          edit->clip.midi_notes[n],
                                          NULL);
            }
        }
        if (edit->clip.automation_lanes && edit->clip.automation_lane_count > 0) {
            for (int l = 0; l < edit->clip.automation_lane_count; ++l) {
                SessionAutomationLane* lane = &edit->clip.automation_lanes[l];
                engine_clip_set_automation_lane_points(state->engine,
                                                       edit->track_index,
                                                       new_index,
                                                       lane->target,
                                                       (const EngineAutomationPoint*)lane->points,
                                                       lane->point_count);
            }
        }
        return true;
    }
    int clip_track = edit->track_index;
    int clip_index = -1;
    if (edit->sampler) {
        timeline_find_clip_by_sampler(state, edit->sampler, &clip_track, &clip_index);
    }
    if (clip_index < 0) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (tracks && clip_track >= 0 && clip_track < engine_get_track_count(state->engine)) {
            clip_index = find_clip_by_snapshot(&tracks[clip_track], &edit->clip);
        }
    }
    if (clip_track < 0 || clip_index < 0) {
        return false;
    }
    return engine_remove_clip(state->engine, clip_track, clip_index);
}

static bool apply_automation_edit(AppState* state, UndoAutomationEdit* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    const SessionAutomationLane* src_lanes = apply_after ? edit->after_lanes : edit->before_lanes;
    int lane_count = apply_after ? edit->after_lane_count : edit->before_lane_count;
    if (lane_count <= 0 || !src_lanes) {
        return engine_clip_set_automation_lanes(state->engine,
                                                edit->track_index,
                                                edit->clip_index,
                                                NULL,
                                                0);
    }
    EngineAutomationLane* lanes = (EngineAutomationLane*)calloc((size_t)lane_count, sizeof(EngineAutomationLane));
    if (!lanes) {
        return false;
    }
    for (int i = 0; i < lane_count; ++i) {
        lanes[i].target = src_lanes[i].target;
        lanes[i].points = (EngineAutomationPoint*)src_lanes[i].points;
        lanes[i].point_count = src_lanes[i].point_count;
        lanes[i].point_capacity = src_lanes[i].point_count;
    }
    bool ok = engine_clip_set_automation_lanes(state->engine,
                                               edit->track_index,
                                               edit->clip_index,
                                               lanes,
                                               lane_count);
    free(lanes);
    return ok;
}

// Resolves captured destination identity and rejects removed tracks without falling back to another index.
static int undo_clip_destination(const Engine* engine, const UndoClipState* target) {
    if (!target->track_runtime_id) return target->track_index;
    const EngineTrack* tracks = engine_get_tracks(engine);
    for (int t = 0; tracks && t < engine_get_track_count(engine); ++t)
        if (tracks[t].runtime_id == target->track_runtime_id) return t;
    return -1;
}

// Resolves stable clip identity and restores all transform fields with one engine commit.
static bool apply_clip_state(AppState* state, const UndoClipState* target) {
    if (!state || !target) {
        return false;
    }
    int current_track = -1;
    int current_clip = -1;
    if (target->creation_index > 0) {
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
                if (clip && clip->kind == target->kind &&
                    clip->creation_index == target->creation_index) {
                    current_track = t;
                    current_clip = c;
                    break;
                }
            }
            if (current_clip >= 0) {
                break;
            }
        }
        if (current_clip < 0) {
            return false;
        }
    } else if (!target->sampler ||
               !timeline_find_clip_by_sampler(state, target->sampler, &current_track, &current_clip)) {
        return false;
    }
    EngineClipTransform transform = {
        .start_frame = target->start_frame, .offset_frames = target->offset_frames,
        .duration_frames = target->duration_frames, .gain = target->gain,
        .fade_in_frames = target->fade_in_frames, .fade_out_frames = target->fade_out_frames,
        .fade_in_curve = target->fade_in_curve, .fade_out_curve = target->fade_out_curve,
        .instrument_preset = target->instrument_preset, .instrument_params = target->instrument_params,
        .instrument_inherits_track = target->instrument_inherits_track,
        .midi_notes = target->midi_notes, .midi_note_count = target->midi_note_count,
    };
    int destination = undo_clip_destination(state->engine, target);
    if (destination < 0) return false;
    return engine_transform_clip(state->engine, current_track, current_clip, destination, &transform, NULL);
}

// Resolves rename history by stable clip identity and rejects removed targets.
static bool apply_clip_rename(AppState* state, const UndoClipRename* edit, bool apply_after) {
    if (!state || !state->engine || !edit || !edit->creation_index) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; t < engine_get_track_count(state->engine); ++t) {
        for (int c = 0; c < tracks[t].clip_count; ++c) {
            if (tracks[t].clips[c].creation_index == edit->creation_index) {
                const char* name = apply_after ? edit->after_name : edit->before_name;
                return engine_clip_set_name(state->engine, t, c, name);
            }
        }
    }
    return false;
}

// Restores mixer/instrument history with one engine commit before updating the panel snapshot.
static bool apply_track_snapshot(AppState* state, const UndoTrackSnapshotEdit* edit, bool apply_after) {
    if (!state || !edit) {
        return false;
    }
    float gain = apply_after ? edit->gain_after : edit->gain_before;
    float pan = apply_after ? edit->pan_after : edit->pan_before;
    bool muted = apply_after ? edit->muted_after : edit->muted_before;
    bool solo = apply_after ? edit->solo_after : edit->solo_before;
    bool midi_enabled = apply_after ? edit->midi_instrument_enabled_after
                                    : edit->midi_instrument_enabled_before;
    EngineInstrumentPresetId midi_preset = apply_after ? edit->midi_instrument_preset_after
                                                       : edit->midi_instrument_preset_before;
    EngineInstrumentParams midi_params = apply_after ? edit->midi_instrument_params_after
                                                     : edit->midi_instrument_params_before;
    if (edit->is_master) {
        return false;
    }
    if (edit->track_index < 0) {
        return false;
    }
    EngineTrackSettings settings = {
        .gain = gain, .pan = pan, .muted = muted, .solo = solo,
        .instrument_enabled = midi_enabled,
        .instrument_preset = edit->instrument_state_captured || midi_enabled ? midi_preset : engine_track_midi_instrument_preset(state->engine, edit->track_index),
        .instrument_params = edit->instrument_state_captured || midi_enabled ? midi_params : engine_track_midi_instrument_params(state->engine, edit->track_index),
    };
    if (!engine_track_set_settings(state->engine, edit->track_index, &settings)) return false;
    state->effects_panel.track_snapshot.gain = gain;
    state->effects_panel.track_snapshot.pan = pan;
    state->effects_panel.track_snapshot.muted = muted;
    state->effects_panel.track_snapshot.solo = solo;
    return true;
}

static bool apply_track_rename(AppState* state, const UndoTrackRename* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    const char* name = apply_after ? edit->after_name : edit->before_name;
    if (!engine_track_set_name(state->engine, edit->track_index, name)) {
        return false;
    }
    effects_panel_sync_from_engine(state);
    return true;
}

static bool apply_library_rename(AppState* state, const UndoLibraryRename* edit, bool apply_after) {
    if (!state || !edit) {
        return false;
    }
    const char* from_name = apply_after ? edit->before_name : edit->after_name;
    const char* to_name = apply_after ? edit->after_name : edit->before_name;
    char from_path[512];
    char to_path[512];
    snprintf(from_path, sizeof(from_path), "%s/%s", edit->directory, from_name);
    snprintf(to_path, sizeof(to_path), "%s/%s", edit->directory, to_name);
    if (rename(from_path, to_path) != 0) {
        return false;
    }
    if (state->library.editing) {
        library_input_stop_edit(state);
    }
    library_browser_scan(&state->library, &state->media_registry);
    return true;
}

// Restores or removes a complete retained track without touching a neighboring row.
static bool apply_track_edit(AppState* state, const UndoCommand* command, bool apply_after) {
    const EngineClipContentSnapshot* target = apply_after ? command->clip_content_after : command->clip_content_before;
    const EngineClipContentSnapshot* removed = apply_after ? command->clip_content_before : command->clip_content_after;
    if (target) {
        int index = command->data.track_edit.track_index;
        int count = engine_get_track_count(state->engine);
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (index < 0 || index > count ||
            (index > 0 ? tracks[index - 1].runtime_id : 0) != command->data.track_edit.left_track_id ||
            (index < count ? tracks[index].runtime_id : 0) != command->data.track_edit.right_track_id) return false;
        return engine_track_history_insert(state->engine, index, target);
    }
    return removed && engine_track_history_remove(state->engine, removed);
}

// Reserves all track history before the engine can add or remove authored content.
bool undo_manager_edit_track(AppState* state, int track_index, bool add) {
    if (!state || !state->engine || state->undo.active_drag_valid || track_index < 0 ||
        track_index > engine_get_track_count(state->engine)) return false;
    EngineClipContentSnapshot* snapshot = engine_track_history_capture(state->engine, add ? -1 : track_index);
    if (!snapshot) return undo_manager_reject(&state->undo, "Track edit not applied: history preparation failed.");
    UndoCommand command = {.type = UNDO_CMD_TRACK_EDIT};
    command.data.track_edit.track_index = track_index;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    command.data.track_edit.left_track_id = track_index > 0 ? tracks[track_index - 1].runtime_id : 0;
    int right = add ? track_index : track_index + 1;
    command.data.track_edit.right_track_id = right < engine_get_track_count(state->engine) ? tracks[right].runtime_id : 0;
    if (add) command.clip_content_after = snapshot;
    else command.clip_content_before = snapshot;
    bool reserved = undo_manager_begin_drag(&state->undo, &command);
    engine_clip_content_release(snapshot);
    if (!reserved) return false;
    bool accepted = apply_track_edit(state, &state->undo.active_drag, true);
    if (!accepted) {
        undo_manager_cancel_drag(&state->undo);
        return undo_manager_reject(&state->undo, "Track edit not applied: target changed or preparation failed.");
    }
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag);
}

static int find_clip_by_creation_index(const EngineTrack* track, uint64_t creation_index) {
    if (!track || creation_index == 0) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].creation_index == creation_index) {
            return i;
        }
    }
    return -1;
}

static bool apply_midi_note_edit(AppState* state, const UndoMidiNoteEdit* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    const EngineMidiNote* notes = apply_after ? edit->after_notes : edit->before_notes;
    int note_count = apply_after ? edit->after_note_count : edit->before_note_count;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || edit->track_index < 0 || edit->track_index >= track_count) {
        return false;
    }
    int clip_index = find_clip_by_creation_index(&tracks[edit->track_index], edit->clip_creation_index);
    if (clip_index < 0) {
        return false;
    }
    if (!engine_clip_midi_set_notes(state->engine, edit->track_index, clip_index, notes, note_count)) {
        return false;
    }
    timeline_selection_set_selected_clip(state, edit->track_index, clip_index);
    midi_editor_input_set_selected_clip(state, edit->track_index, clip_index, edit->clip_creation_index);
    state->midi_editor_ui.selected_note_index = -1;
    return true;
}

static bool apply_eq_curve(AppState* state, const UndoEqCurveEdit* edit, bool apply_after) {
    if (!state || !edit) {
        return false;
    }
    const SessionEqCurve* curve = apply_after ? &edit->after : &edit->before;
    EqCurveState ui_curve = {0};
    eq_curve_from_session(&ui_curve, curve);
    EngineEqCurve engine_curve;
    eq_curve_to_engine(&ui_curve, &engine_curve);
    if (edit->is_master) {
        if (!engine_set_master_eq_curve(state->engine, &engine_curve)) return false;
        state->effects_panel.eq_curve_master = ui_curve;
        if (state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_MASTER) {
            state->effects_panel.eq_curve = ui_curve;
        }
        return true;
    }
    if (edit->track_index < 0) {
        return false;
    }
    if (!engine_set_track_eq_curve(state->engine, edit->track_index, &engine_curve)) return false;
    if (state->effects_panel.eq_curve_tracks &&
        edit->track_index < state->effects_panel.eq_curve_tracks_count) {
        state->effects_panel.eq_curve_tracks[edit->track_index] = ui_curve;
    }
    if (state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        state->effects_panel.target_track_index == edit->track_index) {
        state->effects_panel.eq_curve = ui_curve;
    }
    return true;
}

static bool apply_fx_edit(AppState* state, UndoFxEdit* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    bool is_track = (edit->target == UNDO_FX_TARGET_TRACK);
    int track_index = edit->track_index;
    if (is_track && track_index < 0) {
        return false;
    }
    switch (edit->kind) {
        case UNDO_FX_EDIT_REORDER: {
            int index = apply_after ? edit->after_index : edit->before_index;
            return is_track
                       ? engine_fx_track_reorder(state->engine, track_index, edit->id, index)
                       : engine_fx_master_reorder(state->engine, edit->id, index);
        }
        case UNDO_FX_EDIT_PARAM: {
            const SessionFxInstance* inst = apply_after ? &edit->after_state : &edit->before_state;
            uint32_t param_index = edit->param_index;
            if (param_index >= inst->param_count) {
                return false;
            }
            float value = inst->params[param_index];
            FxParamMode mode = inst->param_mode[param_index];
            float beat_value = inst->param_beats[param_index];
            if (mode != FX_PARAM_MODE_NATIVE) {
                return is_track
                           ? engine_fx_track_set_param_with_mode(state->engine, track_index, edit->id,
                                                                 param_index, value, mode, beat_value)
                           : engine_fx_master_set_param_with_mode(state->engine, edit->id,
                                                                  param_index, value, mode, beat_value);
            }
            return is_track
                       ? engine_fx_track_set_param(state->engine, track_index, edit->id, param_index, value)
                       : engine_fx_master_set_param(state->engine, edit->id, param_index, value);
        }
        case UNDO_FX_EDIT_ENABLE: {
            bool enabled = apply_after ? edit->after_state.enabled : edit->before_state.enabled;
            return is_track
                       ? engine_fx_track_set_enabled(state->engine, track_index, edit->id, enabled)
                       : engine_fx_master_set_enabled(state->engine, edit->id, enabled);
        }
        case UNDO_FX_EDIT_ADD:
        case UNDO_FX_EDIT_REMOVE: {
            bool do_add = (edit->kind == UNDO_FX_EDIT_ADD) ? apply_after : !apply_after;
            const SessionFxInstance* inst = apply_after ? &edit->after_state : &edit->before_state;
            if (do_add) {
                FxMasterInstanceInfo restored = {.id = edit->id, .type = inst->type,
                    .enabled = inst->enabled, .param_count = inst->param_count};
                if (inst->param_count > FX_MAX_PARAMS) return false;
                for (uint32_t p = 0; p < inst->param_count; ++p) {
                    restored.params[p] = inst->params[p];
                    restored.param_mode[p] = inst->param_mode[p];
                    restored.param_beats[p] = inst->param_beats[p];
                }
                int position = apply_after ? edit->after_index : edit->before_index;
                if (!engine_fx_restore_instance(state->engine, is_track ? track_index : -1, position, &restored)) return false;
            } else {
                bool removed = is_track
                                   ? engine_fx_track_remove(state->engine, track_index, edit->id)
                                   : engine_fx_master_remove(state->engine, edit->id);
                if (!removed) {
                    return false;
                }
            }
            effects_panel_sync_from_engine(state);
            return true;
        }
        default:
            return false;
    }
}

// Returns the track address fields for history families that act on track-owned processing.
static bool undo_command_track_fields(UndoCommand* command, int** index, uint64_t** identity) {
    if (command->type == UNDO_CMD_FX_EDIT && command->data.fx_edit.target == UNDO_FX_TARGET_TRACK) {
        *index = &command->data.fx_edit.track_index; *identity = &command->data.fx_edit.track_runtime_id;
    } else if (command->type == UNDO_CMD_EQ_CURVE && !command->data.eq_curve_edit.is_master) {
        *index = &command->data.eq_curve_edit.track_index; *identity = &command->data.eq_curve_edit.track_runtime_id;
    } else if (command->type == UNDO_CMD_TRACK_SNAPSHOT && !command->data.track_snapshot_edit.is_master) {
        *index = &command->data.track_snapshot_edit.track_index; *identity = &command->data.track_snapshot_edit.track_runtime_id;
    } else return false;
    return true;
}

// Captures track identity before an application history entry is reserved or recorded.
bool undo_command_bind_track(AppState* state, UndoCommand* command) {
    if (!state || !state->engine || !command) return false;
    int* index; uint64_t* identity;
    if (!undo_command_track_fields(command, &index, &identity)) return true;
    if (*index < 0 || *index >= engine_get_track_count(state->engine)) return false;
    *identity = engine_get_tracks(state->engine)[*index].runtime_id;
    return true;
}

bool undo_apply(AppState* state, UndoCommand* command, bool apply_after) {
    if (!state || !command || !state->engine) {
        return false;
    }
    int* track_index; uint64_t* track_id;
    if (undo_command_track_fields(command, &track_index, &track_id) && *track_id) {
        int resolved = -1;
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        for (int t = 0; t < engine_get_track_count(state->engine); ++t)
            if (tracks[t].runtime_id == *track_id) { resolved = t; break; }
        if (resolved < 0) return false;
        *track_index = resolved;
    }
    if (command->type == UNDO_CMD_TRACK_EDIT) return apply_track_edit(state, command, apply_after);
    if (command->type == UNDO_CMD_CLIP_CONTENT && (command->clip_content_before || command->clip_content_after)) {
        UndoClipContentSelection* edit = &command->data.clip_content_selection;
        if (edit->created_count > 0) {
            int expected = edit->created_start + (apply_after ? 0 : edit->created_count);
            if (!edit->created_tracks || engine_get_track_count(state->engine) != expected) return false;
            if (!apply_after) for (int i = 0; i < edit->created_count; ++i) {
                int t = edit->created_start + i;
                const EngineTrack* track = &engine_get_tracks(state->engine)[t];
                UndoCreatedTrack current; undo_created_track_capture(track, &current);
                FxMasterSnapshot fx;
                if (memcmp(&current, &edit->created_tracks[i], sizeof(current)) != 0 ||
                    track->midi_instrument_automation_lane_count != 0 ||
                    !engine_fx_track_snapshot(state->engine, t, &fx) || fx.count != 0) return false;
            }
        }
        bool ok = engine_clip_content_restore(state->engine, apply_after ? command->clip_content_after : command->clip_content_before);
        if (ok && apply_after) for (int i = 0; i < edit->created_count; ++i)
            undo_created_track_capture(&engine_get_tracks(state->engine)[edit->created_start + i], &edit->created_tracks[i]);
        return ok;
    }
    if ((command->clip_content_before || command->clip_content_after) &&
        !(command->type == UNDO_CMD_MULTI_CLIP_TRANSFORM && command->data.multi_clip_transform.created_count > 0))
        return engine_clip_content_restore(state->engine, apply_after ? command->clip_content_after : command->clip_content_before);
    switch (command->type) {
        case UNDO_CMD_CLIP_TRANSFORM: {
            const UndoClipTransform* clip = &command->data.clip_transform;
            const UndoClipState* target = apply_after ? &clip->after : &clip->before;
            return apply_clip_state(state, target);
        }
        case UNDO_CMD_CLIP_ADD_REMOVE:
            return apply_clip_add_remove(state, &command->data.clip_add_remove, apply_after);
        case UNDO_CMD_CLIP_RENAME:
            return apply_clip_rename(state, &command->data.clip_rename, apply_after);
        case UNDO_CMD_MULTI_CLIP_TRANSFORM: {
            UndoMultiClipTransform* multi = &command->data.multi_clip_transform;
            if (multi->count <= 0 || !multi->before || !multi->after) return false;
            if (multi->created_count > 0) {
                int expected = multi->created_start + (apply_after ? 0 : multi->created_count);
                if (!multi->created_tracks || engine_get_track_count(state->engine) != expected) return false;
                if (!apply_after) for (int i = 0; i < multi->created_count; ++i) {
                    int t = multi->created_start + i;
                    const EngineTrack* track = &engine_get_tracks(state->engine)[t];
                    UndoCreatedTrack current; undo_created_track_capture(track, &current);
                    FxMasterSnapshot fx;
                    if (memcmp(&current, &multi->created_tracks[i], sizeof(current)) != 0 ||
                        track->midi_instrument_automation_lane_count != 0 ||
                        !engine_fx_track_snapshot(state->engine, t, &fx) || fx.count != 0) return false;
                }
            }
            if (command->clip_content_before || command->clip_content_after) {
                bool ok = engine_clip_content_restore(state->engine, apply_after ? command->clip_content_after : command->clip_content_before);
                if (ok && apply_after) for (int i = 0; i < multi->created_count; ++i)
                    undo_created_track_capture(&engine_get_tracks(state->engine)[multi->created_start + i], &multi->created_tracks[i]);
                return ok;
            }
            EngineClipBatchTransform* edits = calloc((size_t)multi->count, sizeof(*edits));
            if (!edits) return false;
            for (int i = 0; i < multi->count; ++i) {
                const UndoClipState* target = apply_after ? &multi->after[i] : &multi->before[i];
                edits[i].creation_index = target->creation_index;
                edits[i].destination_track = undo_clip_destination(state->engine, target);
                if (apply_after && multi->created_count > 0 && target->track_index >= multi->created_start &&
                    target->track_index < multi->created_start + multi->created_count)
                    edits[i].destination_track = target->track_index;
                edits[i].transform = (EngineClipTransform){
                    .start_frame = target->start_frame, .offset_frames = target->offset_frames,
                    .duration_frames = target->duration_frames, .gain = target->gain,
                    .fade_in_frames = target->fade_in_frames, .fade_out_frames = target->fade_out_frames,
                    .fade_in_curve = target->fade_in_curve, .fade_out_curve = target->fade_out_curve,
                    .instrument_preset = target->instrument_preset, .instrument_params = target->instrument_params,
                    .instrument_inherits_track = target->instrument_inherits_track,
                    .midi_notes = target->midi_notes, .midi_note_count = target->midi_note_count,
                };
            }
            bool ok = !apply_after && multi->created_count > 0
                ? engine_transform_clips_trim_tracks(state->engine, edits, multi->count, multi->created_start)
                : engine_transform_clips(state->engine, edits, multi->count);
            if (ok && apply_after && multi->created_count > 0) {
                for (int i = 0; i < multi->created_count; ++i)
                    undo_created_track_capture(&engine_get_tracks(state->engine)[multi->created_start + i], &multi->created_tracks[i]);
                for (int i = 0; i < multi->count; ++i)
                    multi->after[i].track_runtime_id = engine_get_tracks(state->engine)[edits[i].destination_track].runtime_id;
            }
            free(edits);
            return ok;
        }
        case UNDO_CMD_AUTOMATION_EDIT:
            return apply_automation_edit(state, &command->data.automation_edit, apply_after);
        case UNDO_CMD_TEMPO_MAP_EDIT: {
            const UndoTempoMapEdit* edit = &command->data.tempo_map_edit;
            const TempoEvent* events = apply_after ? edit->after_events : edit->before_events;
            int count = apply_after ? edit->after_event_count : edit->before_event_count;
            if (!tempo_map_set_events(&state->tempo_map, events, count)) {
                return false;
            }
            if (state->tempo_overlay_ui.event_index >= state->tempo_map.event_count) {
                state->tempo_overlay_ui.event_index = -1;
            }
            return true;
        }
        case UNDO_CMD_EQ_CURVE:
            return apply_eq_curve(state, &command->data.eq_curve_edit, apply_after);
        case UNDO_CMD_TRACK_SNAPSHOT:
            return apply_track_snapshot(state, &command->data.track_snapshot_edit, apply_after);
        case UNDO_CMD_FX_EDIT:
            return apply_fx_edit(state, &command->data.fx_edit, apply_after);
        case UNDO_CMD_TRACK_EDIT:
            return apply_track_edit(state, command, apply_after);
        case UNDO_CMD_TRACK_RENAME:
            return apply_track_rename(state, &command->data.track_rename, apply_after);
        case UNDO_CMD_LIBRARY_RENAME:
            return apply_library_rename(state, &command->data.library_rename, apply_after);
        case UNDO_CMD_MIDI_NOTE_EDIT:
            return apply_midi_note_edit(state, &command->data.midi_note_edit, apply_after);
        case UNDO_CMD_NONE:
        default:
            return false;
    }
}

// Applies a discrete action through the same reservation and rejection rules as gestures.
bool undo_manager_apply_edit(AppState* state, UndoCommand* command) {
    if (!state || !command || !undo_command_bind_track(state, command) ||
        !undo_manager_begin_drag(&state->undo, command)) return false;
    if (!undo_apply(state, &state->undo.active_drag, true)) {
        undo_manager_cancel_drag(&state->undo);
        return undo_manager_reject(&state->undo, "Edit not applied: target changed or preparation failed.");
    }
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag);
}

// Transfers complete effect history from a prepared engine candidate without post-edit allocation.
FxInstId undo_manager_add_effect(AppState* state, int track_index, FxTypeId type) {
    if (!state || !state->engine) return 0;
    UndoCommand command = {.type = UNDO_CMD_FX_EDIT};
    command.data.fx_edit.target = track_index < 0 ? UNDO_FX_TARGET_MASTER : UNDO_FX_TARGET_TRACK;
    command.data.fx_edit.track_index = track_index;
    command.data.fx_edit.kind = UNDO_FX_EDIT_ADD;
    command.data.fx_edit.before_index = -1;
    if (!undo_command_bind_track(state, &command) || !undo_manager_begin_drag(&state->undo, &command)) return 0;
    FxMasterInstanceInfo result; int position;
    FxInstId id = engine_fx_add_capture(state->engine, track_index, type, &result, &position);
    if (!id) {
        undo_manager_cancel_drag(&state->undo);
        undo_manager_reject(&state->undo, "Effect not added: preparation failed.");
        return 0;
    }
    UndoFxEdit* edit = &state->undo.active_drag.data.fx_edit;
    edit->id = id; edit->after_index = position;
    edit->after_state.type = result.type; edit->after_state.enabled = result.enabled;
    edit->after_state.param_count = result.param_count;
    for (uint32_t p = 0; p < result.param_count; ++p) {
        edit->after_state.params[p] = result.params[p];
        edit->after_state.param_mode[p] = result.param_mode[p];
        edit->after_state.param_beats[p] = result.param_beats[p];
    }
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ? id : 0;
}
