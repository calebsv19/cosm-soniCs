#include "undo/undo_manager.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/timeline_drag.h"
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

#define UNDO_DEFAULT_CAPACITY 32
#define UNDO_DEFAULT_LIMIT 256

static void session_track_clear(SessionTrack* track) {
    if (!track) {
        return;
    }
    if (track->clips) {
        for (int i = 0; i < track->clip_count; ++i) {
            SessionClip* clip = &track->clips[i];
            if (clip->automation_lanes) {
                for (int l = 0; l < clip->automation_lane_count; ++l) {
                    SessionAutomationLane* lane = &clip->automation_lanes[l];
                    free(lane->points);
                    lane->points = NULL;
                    lane->point_count = 0;
                }
                free(clip->automation_lanes);
                clip->automation_lanes = NULL;
                clip->automation_lane_count = 0;
            }
        }
    }
    free(track->clips);
    free(track->fx);
    track->clips = NULL;
    track->fx = NULL;
    track->clip_count = 0;
    track->fx_count = 0;
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

static bool session_clip_clone(SessionClip* dst, const SessionClip* src) {
    if (!dst || !src) {
        return false;
    }
    *dst = *src;
    dst->automation_lanes = NULL;
    dst->automation_lane_count = 0;
    if (!src->automation_lanes || src->automation_lane_count <= 0) {
        return true;
    }
    dst->automation_lanes = (SessionAutomationLane*)calloc((size_t)src->automation_lane_count,
                                                           sizeof(SessionAutomationLane));
    if (!dst->automation_lanes) {
        return false;
    }
    dst->automation_lane_count = src->automation_lane_count;
    for (int l = 0; l < src->automation_lane_count; ++l) {
        const SessionAutomationLane* src_lane = &src->automation_lanes[l];
        SessionAutomationLane* dst_lane = &dst->automation_lanes[l];
        dst_lane->target = src_lane->target;
        dst_lane->point_count = src_lane->point_count;
        if (src_lane->point_count > 0) {
            dst_lane->points = (SessionAutomationPoint*)calloc((size_t)src_lane->point_count,
                                                               sizeof(SessionAutomationPoint));
            if (!dst_lane->points) {
                return false;
            }
            memcpy(dst_lane->points,
                   src_lane->points,
                   sizeof(SessionAutomationPoint) * (size_t)src_lane->point_count);
        }
    }
    return true;
}

static bool automation_lanes_clone(SessionAutomationLane** dst,
                                   int* dst_count,
                                   const SessionAutomationLane* src,
                                   int src_count) {
    if (!dst || !dst_count) {
        return false;
    }
    *dst = NULL;
    *dst_count = 0;
    if (!src || src_count <= 0) {
        return true;
    }
    SessionAutomationLane* lanes = (SessionAutomationLane*)calloc((size_t)src_count, sizeof(SessionAutomationLane));
    if (!lanes) {
        return false;
    }
    for (int l = 0; l < src_count; ++l) {
        lanes[l].target = src[l].target;
        lanes[l].point_count = src[l].point_count;
        if (src[l].point_count > 0) {
            lanes[l].points = (SessionAutomationPoint*)calloc((size_t)src[l].point_count,
                                                              sizeof(SessionAutomationPoint));
            if (!lanes[l].points) {
                for (int j = 0; j <= l; ++j) {
                    free(lanes[j].points);
                    lanes[j].points = NULL;
                }
                free(lanes);
                return false;
            }
            memcpy(lanes[l].points,
                   src[l].points,
                   sizeof(SessionAutomationPoint) * (size_t)src[l].point_count);
        }
    }
    *dst = lanes;
    *dst_count = src_count;
    return true;
}

static void automation_lanes_clear(SessionAutomationLane** lanes, int* lane_count) {
    if (!lanes || !*lanes || !lane_count) {
        return;
    }
    for (int l = 0; l < *lane_count; ++l) {
        free((*lanes)[l].points);
        (*lanes)[l].points = NULL;
        (*lanes)[l].point_count = 0;
    }
    free(*lanes);
    *lanes = NULL;
    *lane_count = 0;
}

static bool tempo_events_clone(TempoEvent** dst, int* dst_count, const TempoEvent* src, int src_count) {
    if (!dst || !dst_count) {
        return false;
    }
    *dst = NULL;
    *dst_count = 0;
    if (!src || src_count <= 0) {
        return true;
    }
    TempoEvent* events = (TempoEvent*)calloc((size_t)src_count, sizeof(TempoEvent));
    if (!events) {
        return false;
    }
    memcpy(events, src, sizeof(TempoEvent) * (size_t)src_count);
    *dst = events;
    *dst_count = src_count;
    return true;
}

static void tempo_events_clear(TempoEvent** events, int* event_count) {
    if (!events || !*events || !event_count) {
        return;
    }
    free(*events);
    *events = NULL;
    *event_count = 0;
}

static bool session_track_clone(SessionTrack* dst, const SessionTrack* src) {
    if (!dst || !src) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->clips = NULL;
    dst->fx = NULL;
    if (src->clip_count > 0) {
        dst->clips = (SessionClip*)calloc((size_t)src->clip_count, sizeof(SessionClip));
        if (!dst->clips) {
            return false;
        }
        for (int i = 0; i < src->clip_count; ++i) {
            if (!session_clip_clone(&dst->clips[i], &src->clips[i])) {
                session_track_clear(dst);
                return false;
            }
        }
    }
    if (src->fx_count > 0) {
        dst->fx = (SessionFxInstance*)malloc(sizeof(SessionFxInstance) * (size_t)src->fx_count);
        if (!dst->fx) {
            free(dst->clips);
            dst->clips = NULL;
            return false;
        }
        memcpy(dst->fx, src->fx, sizeof(SessionFxInstance) * (size_t)src->fx_count);
    }
    return true;
}

static bool undo_command_clone(UndoCommand* dst, const UndoCommand* src) {
    if (!dst || !src) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    switch (src->type) {
        case UNDO_CMD_CLIP_TRANSFORM:
            dst->data.clip_transform = src->data.clip_transform;
            return true;
        case UNDO_CMD_CLIP_ADD_REMOVE:
            dst->data.clip_add_remove = src->data.clip_add_remove;
            dst->data.clip_add_remove.clip.automation_lanes = NULL;
            dst->data.clip_add_remove.clip.automation_lane_count = 0;
            if (!session_clip_clone(&dst->data.clip_add_remove.clip, &src->data.clip_add_remove.clip)) {
                return false;
            }
            return true;
        case UNDO_CMD_CLIP_RENAME:
            dst->data.clip_rename = src->data.clip_rename;
            return true;
        case UNDO_CMD_MULTI_CLIP_TRANSFORM: {
            const UndoMultiClipTransform* msrc = &src->data.multi_clip_transform;
            UndoMultiClipTransform* mdst = &dst->data.multi_clip_transform;
            mdst->count = msrc->count;
            if (msrc->count <= 0) {
                mdst->before = NULL;
                mdst->after = NULL;
                return true;
            }
            mdst->before = (UndoClipState*)malloc(sizeof(UndoClipState) * (size_t)msrc->count);
            mdst->after = (UndoClipState*)malloc(sizeof(UndoClipState) * (size_t)msrc->count);
            if (!mdst->before || !mdst->after) {
                free(mdst->before);
                free(mdst->after);
                mdst->before = NULL;
                mdst->after = NULL;
                mdst->count = 0;
                return false;
            }
            memcpy(mdst->before, msrc->before, sizeof(UndoClipState) * (size_t)msrc->count);
            memcpy(mdst->after, msrc->after, sizeof(UndoClipState) * (size_t)msrc->count);
            return true;
        }
        case UNDO_CMD_AUTOMATION_EDIT: {
            const UndoAutomationEdit* asrc = &src->data.automation_edit;
            UndoAutomationEdit* adst = &dst->data.automation_edit;
            *adst = *asrc;
            adst->before_lanes = NULL;
            adst->after_lanes = NULL;
            if (!automation_lanes_clone(&adst->before_lanes, &adst->before_lane_count,
                                        asrc->before_lanes, asrc->before_lane_count)) {
                return false;
            }
            if (!automation_lanes_clone(&adst->after_lanes, &adst->after_lane_count,
                                        asrc->after_lanes, asrc->after_lane_count)) {
                automation_lanes_clear(&adst->before_lanes, &adst->before_lane_count);
                return false;
            }
            return true;
        }
        case UNDO_CMD_TEMPO_MAP_EDIT: {
            const UndoTempoMapEdit* tsrc = &src->data.tempo_map_edit;
            UndoTempoMapEdit* tdst = &dst->data.tempo_map_edit;
            *tdst = *tsrc;
            tdst->before_events = NULL;
            tdst->after_events = NULL;
            if (!tempo_events_clone(&tdst->before_events, &tdst->before_event_count,
                                    tsrc->before_events, tsrc->before_event_count)) {
                return false;
            }
            if (!tempo_events_clone(&tdst->after_events, &tdst->after_event_count,
                                    tsrc->after_events, tsrc->after_event_count)) {
                tempo_events_clear(&tdst->before_events, &tdst->before_event_count);
                return false;
            }
            return true;
        }
        case UNDO_CMD_TRACK_EDIT: {
            const UndoTrackEdit* tsrc = &src->data.track_edit;
            UndoTrackEdit* tdst = &dst->data.track_edit;
            *tdst = *tsrc;
            tdst->before.clips = NULL;
            tdst->before.fx = NULL;
            tdst->after.clips = NULL;
            tdst->after.fx = NULL;
            if (tsrc->has_before && !session_track_clone(&tdst->before, &tsrc->before)) {
                return false;
            }
            if (tsrc->has_after && !session_track_clone(&tdst->after, &tsrc->after)) {
                if (tsrc->has_before) {
                    session_track_clear(&tdst->before);
                }
                return false;
            }
            return true;
        }
        case UNDO_CMD_TRACK_RENAME:
            dst->data.track_rename = src->data.track_rename;
            return true;
        case UNDO_CMD_FX_EDIT:
            dst->data.fx_edit = src->data.fx_edit;
            return true;
        case UNDO_CMD_EQ_CURVE:
            dst->data.eq_curve_edit = src->data.eq_curve_edit;
            return true;
        case UNDO_CMD_TRACK_SNAPSHOT:
            dst->data.track_snapshot_edit = src->data.track_snapshot_edit;
            return true;
        case UNDO_CMD_LIBRARY_RENAME:
            dst->data.library_rename = src->data.library_rename;
            return true;
        case UNDO_CMD_NONE:
        default:
            return true;
    }
}

static int find_clip_by_snapshot(const EngineTrack* track, const SessionClip* clip) {
    if (!track || !clip) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        const EngineClip* current = &track->clips[i];
        if (!current || !current->sampler) {
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
        if (!engine_add_clip_to_track_with_id(state->engine,
                                              edit->track_index,
                                              edit->clip.media_path,
                                              edit->clip.media_id,
                                              edit->clip.start_frame,
                                              &new_index)) {
            return false;
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
    if (!state || !target || !target->sampler) {
        return false;
    }
    int current_track = -1;
    int current_clip = -1;
    if (!timeline_find_clip_by_sampler(state, target->sampler, &current_track, &current_clip)) {
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
    engine_clip_set_region(state->engine, final_track, final_clip,
                           target->offset_frames, target->duration_frames);
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
        if (clip->media_path[0] == '\0') {
            continue;
        }
        int clip_index = -1;
        if (!engine_add_clip_to_track_with_id(state->engine,
                                              track_index,
                                              clip->media_path,
                                              clip->media_id,
                                              clip->start_frame,
                                              &clip_index)) {
            continue;
        }
        engine_clip_set_region(state->engine, track_index, clip_index, clip->offset_frames, clip->duration_frames);
        engine_clip_set_gain(state->engine, track_index, clip_index, clip->gain == 0.0f ? 1.0f : clip->gain);
        engine_clip_set_name(state->engine, track_index, clip_index, clip->name);
        engine_clip_set_fades(state->engine, track_index, clip_index, clip->fade_in_frames, clip->fade_out_frames);
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

static bool undo_apply(AppState* state, UndoCommand* command, bool apply_after) {
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
        case UNDO_CMD_NONE:
        default:
            return false;
    }
}

static void undo_command_destroy(UndoCommand* cmd) {
    if (!cmd) {
        return;
    }
    switch (cmd->type) {
        case UNDO_CMD_MULTI_CLIP_TRANSFORM:
            free(cmd->data.multi_clip_transform.before);
            free(cmd->data.multi_clip_transform.after);
            cmd->data.multi_clip_transform.before = NULL;
            cmd->data.multi_clip_transform.after = NULL;
            cmd->data.multi_clip_transform.count = 0;
            break;
        case UNDO_CMD_TRACK_EDIT:
            if (cmd->data.track_edit.has_before) {
                session_track_clear(&cmd->data.track_edit.before);
            }
            if (cmd->data.track_edit.has_after) {
                session_track_clear(&cmd->data.track_edit.after);
            }
            break;
        case UNDO_CMD_AUTOMATION_EDIT:
            automation_lanes_clear(&cmd->data.automation_edit.before_lanes,
                                   &cmd->data.automation_edit.before_lane_count);
            automation_lanes_clear(&cmd->data.automation_edit.after_lanes,
                                   &cmd->data.automation_edit.after_lane_count);
            break;
        case UNDO_CMD_TEMPO_MAP_EDIT:
            tempo_events_clear(&cmd->data.tempo_map_edit.before_events,
                               &cmd->data.tempo_map_edit.before_event_count);
            tempo_events_clear(&cmd->data.tempo_map_edit.after_events,
                               &cmd->data.tempo_map_edit.after_event_count);
            break;
        case UNDO_CMD_CLIP_ADD_REMOVE:
            if (cmd->data.clip_add_remove.clip.automation_lanes) {
                for (int l = 0; l < cmd->data.clip_add_remove.clip.automation_lane_count; ++l) {
                    SessionAutomationLane* lane = &cmd->data.clip_add_remove.clip.automation_lanes[l];
                    free(lane->points);
                    lane->points = NULL;
                    lane->point_count = 0;
                }
                free(cmd->data.clip_add_remove.clip.automation_lanes);
                cmd->data.clip_add_remove.clip.automation_lanes = NULL;
                cmd->data.clip_add_remove.clip.automation_lane_count = 0;
            }
            break;
        default:
            break;
    }
    cmd->type = UNDO_CMD_NONE;
}

static bool undo_stack_ensure(UndoCommand** stack, int* capacity, int count_needed) {
    if (!stack || !capacity) {
        return false;
    }
    if (*capacity >= count_needed) {
        return true;
    }
    int next = (*capacity > 0) ? *capacity : UNDO_DEFAULT_CAPACITY;
    while (next < count_needed) {
        next *= 2;
    }
    UndoCommand* resized = (UndoCommand*)realloc(*stack, sizeof(UndoCommand) * (size_t)next);
    if (!resized) {
        return false;
    }
    *stack = resized;
    *capacity = next;
    return true;
}

void undo_manager_init(UndoManager* manager) {
    if (!manager) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
    manager->max_commands = UNDO_DEFAULT_LIMIT;
}

void undo_manager_free(UndoManager* manager) {
    if (!manager) {
        return;
    }
    undo_manager_clear(manager);
    free(manager->undo_stack);
    free(manager->redo_stack);
    manager->undo_stack = NULL;
    manager->redo_stack = NULL;
    manager->undo_capacity = 0;
    manager->redo_capacity = 0;
}

void undo_manager_clear(UndoManager* manager) {
    if (!manager) {
        return;
    }
    for (int i = 0; i < manager->undo_count; ++i) {
        undo_command_destroy(&manager->undo_stack[i]);
    }
    for (int i = 0; i < manager->redo_count; ++i) {
        undo_command_destroy(&manager->redo_stack[i]);
    }
    manager->undo_count = 0;
    manager->redo_count = 0;
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
}

void undo_manager_set_limit(UndoManager* manager, int max_commands) {
    if (!manager) {
        return;
    }
    manager->max_commands = max_commands;
}

bool undo_manager_push(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1)) {
        return false;
    }
    UndoCommand* slot = &manager->undo_stack[manager->undo_count];
    if (!undo_command_clone(slot, command)) {
        return false;
    }
    manager->undo_count += 1;
    for (int i = 0; i < manager->redo_count; ++i) {
        undo_command_destroy(&manager->redo_stack[i]);
    }
    manager->redo_count = 0;
    if (manager->max_commands > 0 && manager->undo_count > manager->max_commands) {
        undo_command_destroy(&manager->undo_stack[0]);
        memmove(manager->undo_stack, manager->undo_stack + 1, sizeof(UndoCommand) * (size_t)(manager->undo_count - 1));
        manager->undo_count -= 1;
    }
    return true;
}

bool undo_manager_begin_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
    if (!undo_command_clone(&manager->active_drag, command)) {
        return false;
    }
    manager->active_drag_valid = true;
    return true;
}

bool undo_manager_commit_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    bool pushed = undo_manager_push(manager, command);
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
    return pushed;
}

void undo_manager_cancel_drag(UndoManager* manager) {
    if (!manager || !manager->active_drag_valid) {
        return;
    }
    undo_command_destroy(&manager->active_drag);
    manager->active_drag_valid = false;
}

bool undo_manager_can_undo(const UndoManager* manager) {
    return manager && manager->undo_count > 0;
}

bool undo_manager_can_redo(const UndoManager* manager) {
    return manager && manager->redo_count > 0;
}

bool undo_manager_undo(UndoManager* manager, AppState* state) {
    if (!manager || !state || manager->undo_count <= 0) {
        return false;
    }
    UndoCommand cmd = manager->undo_stack[manager->undo_count - 1];
    manager->undo_count -= 1;
    bool ok = undo_apply(state, &cmd, false);
    if (!undo_stack_ensure(&manager->redo_stack, &manager->redo_capacity, manager->redo_count + 1)) {
        undo_command_destroy(&cmd);
        return ok;
    }
    manager->redo_stack[manager->redo_count++] = cmd;
    return ok;
}

bool undo_manager_redo(UndoManager* manager, AppState* state) {
    if (!manager || !state || manager->redo_count <= 0) {
        return false;
    }
    UndoCommand cmd = manager->redo_stack[manager->redo_count - 1];
    manager->redo_count -= 1;
    bool ok = undo_apply(state, &cmd, true);
    if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1)) {
        undo_command_destroy(&cmd);
        return ok;
    }
    manager->undo_stack[manager->undo_count++] = cmd;
    return ok;
}
