#include "undo/undo_manager.h"
#include "undo_manager_internal.h"

#include "engine/sampler.h"

#include <stdlib.h>
#include <string.h>

static void session_clip_clear(SessionClip* clip) {
    if (!clip) {
        return;
    }
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
    free(clip->midi_notes);
    clip->midi_notes = NULL;
    clip->midi_note_count = 0;
}

static void session_track_clear(SessionTrack* track) {
    if (!track) {
        return;
    }
    if (track->clips) {
        for (int i = 0; i < track->clip_count; ++i) {
            session_clip_clear(&track->clips[i]);
        }
    }
    free(track->clips);
    free(track->fx);
    track->clips = NULL;
    track->fx = NULL;
    track->clip_count = 0;
    track->fx_count = 0;
}

static bool session_clip_clone(SessionClip* dst, const SessionClip* src) {
    if (!dst || !src) {
        return false;
    }
    *dst = *src;
    dst->automation_lanes = NULL;
    dst->automation_lane_count = 0;
    dst->midi_notes = NULL;
    dst->midi_note_count = 0;
    if (src->midi_notes && src->midi_note_count > 0) {
        dst->midi_notes = (EngineMidiNote*)calloc((size_t)src->midi_note_count, sizeof(EngineMidiNote));
        if (!dst->midi_notes) {
            return false;
        }
        memcpy(dst->midi_notes, src->midi_notes, sizeof(EngineMidiNote) * (size_t)src->midi_note_count);
        dst->midi_note_count = src->midi_note_count;
    }
    if (!src->automation_lanes || src->automation_lane_count <= 0) {
        return true;
    }
    dst->automation_lanes = (SessionAutomationLane*)calloc((size_t)src->automation_lane_count,
                                                           sizeof(SessionAutomationLane));
    if (!dst->automation_lanes) {
        session_clip_clear(dst);
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
                session_clip_clear(dst);
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

static bool midi_notes_clone(EngineMidiNote** dst, int* dst_count, const EngineMidiNote* src, int src_count) {
    if (!dst || !dst_count || src_count < 0) {
        return false;
    }
    *dst = NULL;
    *dst_count = 0;
    if (src_count == 0) {
        return true;
    }
    if (!src) {
        return false;
    }
    EngineMidiNote* notes = (EngineMidiNote*)calloc((size_t)src_count, sizeof(EngineMidiNote));
    if (!notes) {
        return false;
    }
    memcpy(notes, src, sizeof(EngineMidiNote) * (size_t)src_count);
    *dst = notes;
    *dst_count = src_count;
    return true;
}

static void midi_notes_clear(EngineMidiNote** notes, int* note_count) {
    if (!notes || !note_count) {
        return;
    }
    free(*notes);
    *notes = NULL;
    *note_count = 0;
}

void undo_clip_state_clear(UndoClipState* state) {
    if (!state) {
        return;
    }
    midi_notes_clear(&state->midi_notes, &state->midi_note_count);
}

bool undo_clip_state_clone(UndoClipState* dst, const UndoClipState* src) {
    if (!dst || !src) {
        return false;
    }
    *dst = *src;
    dst->midi_notes = NULL;
    dst->midi_note_count = 0;
    return midi_notes_clone(&dst->midi_notes,
                            &dst->midi_note_count,
                            src->midi_notes,
                            src->midi_note_count);
}

// Binds a captured transform to the existing track instead of its current array position.
bool undo_clip_state_capture(const Engine* engine, const EngineClip* clip, int track_index, UndoClipState* out_state) {
    const EngineTrack* tracks = engine_get_tracks(engine);
    if (!tracks || track_index < 0 || track_index >= engine_get_track_count(engine)) return false;
    if (!undo_clip_state_from_engine_clip(clip, track_index, out_state)) return false;
    out_state->track_runtime_id = tracks[track_index].runtime_id;
    return true;
}

bool undo_clip_state_from_engine_clip(const EngineClip* clip,
                                      int track_index,
                                      UndoClipState* out_state) {
    if (!clip || !out_state) {
        return false;
    }
    memset(out_state, 0, sizeof(*out_state));
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
    out_state->instrument_preset = engine_clip_midi_instrument_preset(clip);
    out_state->instrument_params = engine_clip_midi_instrument_params(clip);
    out_state->instrument_inherits_track = engine_clip_midi_inherits_track_instrument(clip);
    if (out_state->duration_frames == 0 && clip->sampler) {
        out_state->duration_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    if (out_state->kind == ENGINE_CLIP_KIND_MIDI) {
        if (!midi_notes_clone(&out_state->midi_notes,
                              &out_state->midi_note_count,
                              engine_clip_midi_notes(clip),
                              engine_clip_midi_note_count(clip))) {
            undo_clip_state_clear(out_state);
            return false;
        }
    }
    return true;
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

// Builds a normalized metadata guard so later track edits cannot be silently discarded by undo.
void undo_created_track_capture(const EngineTrack* track, UndoCreatedTrack* out) {
    memset(out, 0, sizeof(*out));
    out->runtime_id = track->runtime_id;
    out->settings.gain = track->gain; out->settings.pan = track->pan;
    out->settings.muted = track->muted; out->settings.solo = track->solo;
    out->settings.instrument_enabled = track->midi_instrument_enabled;
    out->settings.instrument_preset = track->midi_instrument_preset;
    out->settings.instrument_params = track->midi_instrument_params;
    out->eq.low_cut.enabled = track->track_eq.curve.low_cut.enabled;
    out->eq.low_cut.freq_hz = track->track_eq.curve.low_cut.freq_hz;
    out->eq.high_cut.enabled = track->track_eq.curve.high_cut.enabled;
    out->eq.high_cut.freq_hz = track->track_eq.curve.high_cut.freq_hz;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        out->eq.bands[i].enabled = track->track_eq.curve.bands[i].enabled;
        out->eq.bands[i].freq_hz = track->track_eq.curve.bands[i].freq_hz;
        out->eq.bands[i].gain_db = track->track_eq.curve.bands[i].gain_db;
        out->eq.bands[i].q_width = track->track_eq.curve.bands[i].q_width;
    }
    SDL_strlcpy(out->name, track->name, sizeof(out->name));
}

static bool undo_command_clone_fields(UndoCommand* dst, const UndoCommand* src) {
    if (!dst || !src) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    switch (src->type) {
        case UNDO_CMD_CLIP_TRANSFORM:
            if (!undo_clip_state_clone(&dst->data.clip_transform.before,
                                       &src->data.clip_transform.before)) {
                return false;
            }
            if (!undo_clip_state_clone(&dst->data.clip_transform.after,
                                       &src->data.clip_transform.after)) {
                undo_clip_state_clear(&dst->data.clip_transform.before);
                return false;
            }
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
            mdst->before = (UndoClipState*)calloc((size_t)msrc->count, sizeof(UndoClipState));
            mdst->after = (UndoClipState*)calloc((size_t)msrc->count, sizeof(UndoClipState));
            if (!mdst->before || !mdst->after) {
                free(mdst->before);
                free(mdst->after);
                mdst->before = NULL;
                mdst->after = NULL;
                mdst->count = 0;
                return false;
            }
            for (int i = 0; i < msrc->count; ++i) {
                if (!undo_clip_state_clone(&mdst->before[i], &msrc->before[i]) ||
                    !undo_clip_state_clone(&mdst->after[i], &msrc->after[i])) {
                    for (int j = 0; j <= i; ++j) {
                        undo_clip_state_clear(&mdst->before[j]);
                        undo_clip_state_clear(&mdst->after[j]);
                    }
                    free(mdst->before);
                    free(mdst->after);
                    mdst->before = NULL;
                    mdst->after = NULL;
                    mdst->count = 0;
                    return false;
                }
            }
            if (msrc->created_count > 0) {
                mdst->created_tracks = malloc((size_t)msrc->created_count * sizeof(*mdst->created_tracks));
                if (!mdst->created_tracks) { undo_command_destroy(dst); return false; }
                memcpy(mdst->created_tracks, msrc->created_tracks, (size_t)msrc->created_count * sizeof(*mdst->created_tracks));
                mdst->created_start = msrc->created_start; mdst->created_count = msrc->created_count;
            }
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
        case UNDO_CMD_MIDI_NOTE_EDIT: {
            const UndoMidiNoteEdit* msrc = &src->data.midi_note_edit;
            UndoMidiNoteEdit* mdst = &dst->data.midi_note_edit;
            *mdst = *msrc;
            mdst->before_notes = NULL;
            mdst->after_notes = NULL;
            mdst->before_note_count = 0;
            mdst->after_note_count = 0;
            if (!midi_notes_clone(&mdst->before_notes,
                                  &mdst->before_note_count,
                                  msrc->before_notes,
                                  msrc->before_note_count)) {
                return false;
            }
            if (!midi_notes_clone(&mdst->after_notes,
                                  &mdst->after_note_count,
                                  msrc->after_notes,
                                  msrc->after_note_count)) {
                midi_notes_clear(&mdst->before_notes, &mdst->before_note_count);
                return false;
            }
            return true;
        }
        case UNDO_CMD_CLIP_CONTENT: {
            const UndoClipContentSelection* source = &src->data.clip_content_selection;
            UndoClipContentSelection* target = &dst->data.clip_content_selection;
            target->count = source->count; target->before = NULL; target->after = NULL;
            target->created_start = source->created_start; target->created_count = source->created_count;
            if (source->created_count < 0 || (source->created_count && !source->created_tracks)) return false;
            if (source->count <= 0 || !source->before || !source->after) return false;
            target->before = calloc((size_t)source->count, sizeof(uint64_t));
            target->after = calloc((size_t)source->count, sizeof(uint64_t));
            if (!target->before || !target->after) {
                free(target->before); free(target->after); target->before = target->after = NULL; return false;
            }
            if (source->created_count) {
                target->created_tracks = calloc((size_t)source->created_count, sizeof(*target->created_tracks));
                if (!target->created_tracks) {
                    free(target->before); free(target->after); target->before = target->after = NULL; return false;
                }
                memcpy(target->created_tracks, source->created_tracks, (size_t)source->created_count * sizeof(*target->created_tracks));
            }
            memcpy(target->before, source->before, (size_t)source->count * sizeof(uint64_t));
            memcpy(target->after, source->after, (size_t)source->count * sizeof(uint64_t));
            return true;
        }
        case UNDO_CMD_NONE:
        default:
            return true;
    }
}

// Clones command fields and shares immutable retained content without duplicating media pins.
bool undo_command_clone(UndoCommand* dst, const UndoCommand* src) {
    if (!undo_command_clone_fields(dst, src)) return false;
    if (src->clip_content_before) {
        if (!engine_clip_content_retain(src->clip_content_before)) { undo_command_destroy(dst); return false; }
        dst->clip_content_before = src->clip_content_before;
    }
    if (src->clip_content_after) {
        if (!engine_clip_content_retain(src->clip_content_after)) { undo_command_destroy(dst); return false; }
        dst->clip_content_after = src->clip_content_after;
    }
    return true;
}

void undo_command_destroy(UndoCommand* cmd) {
    if (!cmd) {
        return;
    }
    engine_clip_content_release(cmd->clip_content_before);
    engine_clip_content_release(cmd->clip_content_after);
    cmd->clip_content_before = cmd->clip_content_after = NULL;
    switch (cmd->type) {
        case UNDO_CMD_CLIP_CONTENT:
            free(cmd->data.clip_content_selection.created_tracks);
            free(cmd->data.clip_content_selection.before);
            free(cmd->data.clip_content_selection.after);
            break;
        case UNDO_CMD_CLIP_TRANSFORM:
            undo_clip_state_clear(&cmd->data.clip_transform.before);
            undo_clip_state_clear(&cmd->data.clip_transform.after);
            break;
        case UNDO_CMD_MULTI_CLIP_TRANSFORM:
            for (int i = 0; i < cmd->data.multi_clip_transform.count; ++i) {
                undo_clip_state_clear(&cmd->data.multi_clip_transform.before[i]);
                undo_clip_state_clear(&cmd->data.multi_clip_transform.after[i]);
            }
            free(cmd->data.multi_clip_transform.created_tracks);
            cmd->data.multi_clip_transform.created_tracks = NULL;
            cmd->data.multi_clip_transform.created_count = 0;
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
            session_clip_clear(&cmd->data.clip_add_remove.clip);
            break;
        case UNDO_CMD_MIDI_NOTE_EDIT:
            midi_notes_clear(&cmd->data.midi_note_edit.before_notes,
                             &cmd->data.midi_note_edit.before_note_count);
            midi_notes_clear(&cmd->data.midi_note_edit.after_notes,
                             &cmd->data.midi_note_edit.after_note_count);
            break;
        default:
            break;
    }
    cmd->type = UNDO_CMD_NONE;
}
