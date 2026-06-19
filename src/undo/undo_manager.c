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

// Builds a parameter array using spec IDs when available to preserve stable mapping.
static uint32_t undo_fx_collect_params(const SessionFxInstance* fx,
                                       const EffectParamSpec* specs,
                                       uint32_t spec_count,
                                       float* out_values,
                                       FxParamMode* out_modes,
                                       float* out_beats) {
    if (!fx || !out_values || !out_modes || !out_beats) {
        return 0;
    }
    uint32_t count = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
    if (!specs || spec_count == 0) {
        for (uint32_t p = 0; p < count; ++p) {
            out_values[p] = fx->params[p];
            out_modes[p] = fx->param_mode[p];
            out_beats[p] = fx->param_beats[p];
        }
        return count;
    }
    count = spec_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : spec_count;
    for (uint32_t p = 0; p < count; ++p) {
        out_values[p] = specs[p].default_value;
        out_modes[p] = FX_PARAM_MODE_NATIVE;
        out_beats[p] = 0.0f;
    }
    if (fx->param_id_count > 0) {
        for (uint32_t i = 0; i < fx->param_id_count && i < FX_MAX_PARAMS; ++i) {
            int idx = fx_param_spec_find_index(specs, count, fx->param_ids[i]);
            if (idx < 0) {
                continue;
            }
            out_values[idx] = fx->param_values_by_id[i];
            out_modes[idx] = fx->param_modes_by_id[i];
            out_beats[idx] = fx->param_beats_by_id[i];
        }
    } else {
        uint32_t copy_count = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
        if (copy_count > count) {
            copy_count = count;
        }
        for (uint32_t p = 0; p < copy_count; ++p) {
            out_values[p] = fx->params[p];
            out_modes[p] = fx->param_mode[p];
            out_beats[p] = fx->param_beats[p];
        }
    }
    return count;
}

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

static bool apply_clip_state(AppState* state, const UndoClipState* target) {
    if (!state || !target) {
        return false;
    }
    int current_track = -1;
    int current_clip = -1;
    if (target->sampler) {
        if (!timeline_find_clip_by_sampler(state, target->sampler, &current_track, &current_clip)) {
            return false;
        }
    } else if (target->kind == ENGINE_CLIP_KIND_MIDI && target->creation_index > 0) {
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
                if (clip && clip->kind == ENGINE_CLIP_KIND_MIDI &&
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
    } else {
        return false;
    }
    int final_track = current_track;
    int final_clip = current_clip;
    if (target->track_index >= 0 && target->track_index != current_track) {
        int moved_index = timeline_move_clip_to_track(state, current_track, current_clip,
                                                      target->track_index, target->start_frame);
        if (moved_index >= 0) {
            final_track = target->track_index;
            final_clip = moved_index;
        }
    }
    if (final_track < 0 || final_clip < 0) {
        return false;
    }
    bool is_midi = target->kind == ENGINE_CLIP_KIND_MIDI;
    if (is_midi) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        int track_count = engine_get_track_count(state->engine);
        if (!tracks || final_track < 0 || final_track >= track_count) {
            return false;
        }
        const EngineTrack* track = &tracks[final_track];
        if (!track || final_clip < 0 || final_clip >= track->clip_count) {
            return false;
        }
        const uint64_t current_duration = track->clips[final_clip].duration_frames;
        if (target->duration_frames < current_duration) {
            if (!engine_clip_midi_set_notes(state->engine,
                                            final_track,
                                            final_clip,
                                            target->midi_notes,
                                            target->midi_note_count)) {
                return false;
            }
            if (!engine_clip_set_region(state->engine,
                                        final_track,
                                        final_clip,
                                        0,
                                        target->duration_frames)) {
                return false;
            }
        } else {
            if (!engine_clip_set_region(state->engine,
                                        final_track,
                                        final_clip,
                                        0,
                                        target->duration_frames)) {
                return false;
            }
            if (!engine_clip_midi_set_notes(state->engine,
                                            final_track,
                                            final_clip,
                                            target->midi_notes,
                                            target->midi_note_count)) {
                return false;
            }
        }
    } else {
        engine_clip_set_region(state->engine, final_track, final_clip,
                               target->offset_frames, target->duration_frames);
    }
    engine_clip_set_timeline_start(state->engine, final_track, final_clip,
                                   target->start_frame, NULL);
    engine_clip_set_fades(state->engine, final_track, final_clip,
                          target->fade_in_frames, target->fade_out_frames);
    engine_clip_set_fade_curves(state->engine,
                                final_track,
                                final_clip,
                                target->fade_in_curve,
                                target->fade_out_curve);
    engine_clip_set_gain(state->engine, final_track, final_clip, target->gain);
    if (is_midi) {
        engine_clip_midi_set_instrument_preset(state->engine,
                                               final_track,
                                               final_clip,
                                               target->instrument_preset);
        engine_clip_midi_set_instrument_params(state->engine,
                                               final_track,
                                               final_clip,
                                               target->instrument_params);
        engine_clip_midi_set_inherits_track_instrument(state->engine,
                                                       final_track,
                                                       final_clip,
                                                       target->instrument_inherits_track);
    }
    return true;
}

static bool apply_clip_rename(AppState* state, const UndoClipRename* edit, bool apply_after) {
    if (!state || !edit || !edit->sampler) {
        return false;
    }
    int track_index = -1;
    int clip_index = -1;
    if (!timeline_find_clip_by_sampler(state, edit->sampler, &track_index, &clip_index)) {
        return false;
    }
    const char* name = apply_after ? edit->after_name : edit->before_name;
    return engine_clip_set_name(state->engine, track_index, clip_index, name);
}

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
    engine_track_set_gain(state->engine, edit->track_index, gain);
    engine_track_set_pan(state->engine, edit->track_index, pan);
    engine_track_set_muted(state->engine, edit->track_index, muted);
    engine_track_set_solo(state->engine, edit->track_index, solo);
    engine_track_midi_set_instrument_enabled(state->engine, edit->track_index, midi_enabled);
    if (midi_enabled) {
        engine_track_midi_set_instrument_preset(state->engine, edit->track_index, midi_preset);
        engine_track_midi_set_instrument_params(state->engine, edit->track_index, midi_params);
    }
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

static bool apply_session_track(AppState* state, int track_index, const SessionTrack* track) {
    if (!state || !track || !state->engine) {
        return false;
    }
    engine_track_set_name(state->engine, track_index, track->name);
    engine_track_set_gain(state->engine, track_index, track->gain == 0.0f ? 1.0f : track->gain);
    engine_track_set_pan(state->engine, track_index, track->pan);
    engine_track_set_muted(state->engine, track_index, track->muted);
    engine_track_set_solo(state->engine, track_index, track->solo);
    engine_track_midi_set_instrument_enabled(state->engine,
                                             track_index,
                                             track->midi_instrument_enabled);
    if (track->midi_instrument_enabled) {
        engine_track_midi_set_instrument_preset(state->engine,
                                                track_index,
                                                track->midi_instrument_preset);
        engine_track_midi_set_instrument_params(state->engine,
                                                track_index,
                                                track->midi_instrument_params);
    }

    if (state->effects_panel.eq_curve_tracks &&
        track_index < state->effects_panel.eq_curve_tracks_count) {
        EqCurveState eq_curve = {0};
        eq_curve_from_session(&eq_curve, &track->eq);
        state->effects_panel.eq_curve_tracks[track_index] = eq_curve;
        EngineEqCurve engine_curve;
        eq_curve_to_engine(&eq_curve, &engine_curve);
        engine_set_track_eq_curve(state->engine, track_index, &engine_curve);
    }

    for (int c = 0; c < track->clip_count; ++c) {
        const SessionClip* clip = &track->clips[c];
        int clip_index = -1;
        if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
            if (!engine_add_midi_clip_to_track(state->engine,
                                               track_index,
                                               clip->start_frame,
                                               clip->duration_frames,
                                               &clip_index)) {
                continue;
            }
        } else {
            if (clip->media_path[0] == '\0') {
                continue;
            }
            if (!engine_add_clip_to_track_with_id(state->engine,
                                                  track_index,
                                                  clip->media_path,
                                                  clip->media_id,
                                                  clip->start_frame,
                                                  &clip_index)) {
                continue;
            }
        }
        engine_clip_set_region(state->engine, track_index, clip_index, clip->offset_frames, clip->duration_frames);
        engine_clip_set_gain(state->engine, track_index, clip_index, clip->gain == 0.0f ? 1.0f : clip->gain);
        engine_clip_set_name(state->engine, track_index, clip_index, clip->name);
        engine_clip_set_fades(state->engine, track_index, clip_index, clip->fade_in_frames, clip->fade_out_frames);
        engine_clip_set_fade_curves(state->engine,
                                    track_index,
                                    clip_index,
                                    clip->fade_in_curve,
                                    clip->fade_out_curve);
        if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
            engine_clip_midi_set_instrument_preset(state->engine,
                                                   track_index,
                                                   clip_index,
                                                   clip->instrument_preset);
            engine_clip_midi_set_instrument_params(state->engine,
                                                   track_index,
                                                   clip_index,
                                                   clip->instrument_params);
            engine_clip_midi_set_inherits_track_instrument(state->engine,
                                                           track_index,
                                                           clip_index,
                                                           clip->instrument_inherits_track);
        }
        for (int n = 0; n < clip->midi_note_count; ++n) {
            engine_clip_midi_add_note(state->engine, track_index, clip_index, clip->midi_notes[n], NULL);
        }
    }

    if (track->fx_count > 0) {
        engine_fx_set_track_count(state->engine, engine_get_track_count(state->engine));
        for (int f = 0; f < track->fx_count && f < FX_MASTER_MAX; ++f) {
            const SessionFxInstance* fx = &track->fx[f];
            if (!fx || fx->type == 0) {
                continue;
            }
            FxInstId id = engine_fx_track_add(state->engine, track_index, fx->type);
            if (id == 0) {
                continue;
            }
            const EffectParamSpec* specs = NULL;
            uint32_t spec_count = 0;
            engine_fx_registry_get_param_specs(state->engine, fx->type, &specs, &spec_count);
            float values[FX_MAX_PARAMS];
            FxParamMode modes[FX_MAX_PARAMS];
            float beats[FX_MAX_PARAMS];
            uint32_t pcount = undo_fx_collect_params(fx, specs, spec_count, values, modes, beats);
            for (uint32_t p = 0; p < pcount; ++p) {
                FxParamMode mode = modes[p];
                float beat_value = beats[p];
                float native_value = values[p];
                const EffectParamSpec* spec = (specs && p < spec_count) ? &specs[p] : NULL;
                bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_spec_is_syncable(spec);
                if (use_sync) {
                    native_value = fx_param_spec_beats_to_native(spec, beat_value, &state->tempo);
                }
                if (use_sync) {
                    engine_fx_track_set_param_with_mode(state->engine, track_index, id, p, native_value, mode, beat_value);
                } else {
                    engine_fx_track_set_param(state->engine, track_index, id, p, native_value);
                }
            }
            if (!fx->enabled) {
                engine_fx_track_set_enabled(state->engine, track_index, id, false);
            }
        }
    }
    return true;
}

static bool apply_track_edit(AppState* state, const UndoTrackEdit* edit, bool apply_after) {
    if (!state || !edit || !state->engine) {
        return false;
    }
    const SessionTrack* target = NULL;
    if (apply_after) {
        if (edit->has_after) {
            target = &edit->after;
        }
    } else {
        if (edit->has_before) {
            target = &edit->before;
        }
    }
    int track_index = edit->track_index;
    int track_count = engine_get_track_count(state->engine);
    if (!target) {
        if (track_index >= 0 && track_index < track_count) {
            return engine_remove_track(state->engine, track_index);
        }
        return false;
    }
    if (track_index < 0) {
        track_index = track_count;
    }
    if (track_index < track_count) {
        engine_remove_track(state->engine, track_index);
    }
    if (!engine_insert_track(state->engine, track_index)) {
        return false;
    }
    effects_panel_ensure_eq_curve_tracks(state, engine_get_track_count(state->engine));
    return apply_session_track(state, track_index, target);
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
        state->effects_panel.eq_curve_master = ui_curve;
        if (state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_MASTER) {
            state->effects_panel.eq_curve = ui_curve;
        }
        engine_set_master_eq_curve(state->engine, &engine_curve);
        return true;
    }
    if (edit->track_index < 0) {
        return false;
    }
    if (state->effects_panel.eq_curve_tracks &&
        edit->track_index < state->effects_panel.eq_curve_tracks_count) {
        state->effects_panel.eq_curve_tracks[edit->track_index] = ui_curve;
    }
    if (state->effects_panel.eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        state->effects_panel.target_track_index == edit->track_index) {
        state->effects_panel.eq_curve = ui_curve;
    }
    engine_set_track_eq_curve(state->engine, edit->track_index, &engine_curve);
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
            const SessionFxInstance* inst = do_add ? &edit->after_state : &edit->before_state;
            if (do_add) {
                FxInstId id = is_track
                                  ? engine_fx_track_add(state->engine, track_index, inst->type)
                                  : engine_fx_master_add(state->engine, inst->type);
                if (id == 0) {
                    return false;
                }
                edit->id = id;
                for (uint32_t i = 0; i < inst->param_count; ++i) {
                    FxParamMode mode = inst->param_mode[i];
                    float beat_value = inst->param_beats[i];
                    if (mode != FX_PARAM_MODE_NATIVE) {
                        is_track
                            ? engine_fx_track_set_param_with_mode(state->engine, track_index, id, i,
                                                                  inst->params[i], mode, beat_value)
                            : engine_fx_master_set_param_with_mode(state->engine, id, i,
                                                                   inst->params[i], mode, beat_value);
                    } else {
                        is_track
                            ? engine_fx_track_set_param(state->engine, track_index, id, i, inst->params[i])
                            : engine_fx_master_set_param(state->engine, id, i, inst->params[i]);
                    }
                }
                if (inst->enabled) {
                    is_track
                        ? engine_fx_track_set_enabled(state->engine, track_index, id, true)
                        : engine_fx_master_set_enabled(state->engine, id, true);
                }
                int index = apply_after ? edit->after_index : edit->before_index;
                if (index >= 0) {
                    is_track
                        ? engine_fx_track_reorder(state->engine, track_index, id, index)
                        : engine_fx_master_reorder(state->engine, id, index);
                }
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

bool undo_apply(AppState* state, UndoCommand* command, bool apply_after) {
    if (!state || !command || !state->engine) {
        return false;
    }
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
            const UndoMultiClipTransform* multi = &command->data.multi_clip_transform;
            bool ok = true;
            for (int i = 0; i < multi->count; ++i) {
                const UndoClipState* target = apply_after ? &multi->after[i] : &multi->before[i];
                if (!apply_clip_state(state, target)) {
                    ok = false;
                }
            }
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
            return apply_track_edit(state, &command->data.track_edit, apply_after);
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
