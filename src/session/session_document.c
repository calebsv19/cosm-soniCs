#include "session.h"

#include "engine/audio_source.h"
#include "app_state.h"
#include "engine/engine.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <string.h>

static void copy_string(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strnlen(src, dst_len - 1);
    memmove(dst, src, len);
    dst[len] = '\0';
}

static void session_eq_curve_set_defaults(SessionEqCurve* curve) {
    if (!curve) {
        return;
    }
    curve->low_cut.enabled = false;
    curve->low_cut.freq_hz = 80.0f;
    curve->low_cut.slope = 12.0f;
    curve->high_cut.enabled = false;
    curve->high_cut.freq_hz = 12000.0f;
    curve->high_cut.slope = 12.0f;
    for (int i = 0; i < 4; ++i) {
        curve->bands[i].enabled = true;
        curve->bands[i].gain_db = 0.0f;
        curve->bands[i].q_width = 1.0f;
    }
    curve->bands[0].freq_hz = 120.0f;
    curve->bands[1].freq_hz = 500.0f;
    curve->bands[2].freq_hz = 2000.0f;
    curve->bands[3].freq_hz = 8000.0f;
}

static void session_eq_curve_from_state(SessionEqCurve* dst, const EqCurveState* src) {
    if (!dst || !src) {
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

static void session_store_active_eq_curve(EffectsPanelState* panel) {
    if (!panel) {
        return;
    }
    if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
        panel->target == FX_PANEL_TARGET_TRACK &&
        panel->target_track_index >= 0 &&
        panel->target_track_index < panel->eq_curve_tracks_count &&
        panel->eq_curve_tracks) {
        panel->eq_curve_tracks[panel->target_track_index] = panel->eq_curve;
    } else {
        panel->eq_curve_master = panel->eq_curve;
    }
}

void session_document_init(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    memset(doc, 0, sizeof(*doc));
    doc->version = SESSION_DOCUMENT_VERSION;
    config_set_defaults(&doc->engine);
    doc->tempo.bpm = 120.0f;
    doc->tempo.ts_num = 4;
    doc->tempo.ts_den = 4;
    doc->timeline.view_in_beats = false;
    doc->timeline.follow_mode = TIMELINE_FOLLOW_JUMP;
    doc->selected_track_index = -1;
    doc->selected_clip_index = -1;
    doc->effects_panel.view_mode = 0;
    doc->effects_panel.selected_index = -1;
    doc->effects_panel.open_index = -1;
    doc->effects_panel.list_detail_mode = 0;
    doc->effects_panel.eq_view_mode = 0;
    doc->effects_panel.meter_scope_mode = 0;
    doc->effects_panel.meter_lufs_mode = 1;
    doc->effects_panel.meter_spectrogram_mode = 0;
    doc->effects_panel.low_cut.enabled = false;
    doc->effects_panel.low_cut.freq_hz = 80.0f;
    doc->effects_panel.low_cut.slope = 12.0f;
    doc->effects_panel.high_cut.enabled = false;
    doc->effects_panel.high_cut.freq_hz = 12000.0f;
    doc->effects_panel.high_cut.slope = 12.0f;
    for (int i = 0; i < 4; ++i) {
        doc->effects_panel.bands[i].enabled = true;
        doc->effects_panel.bands[i].gain_db = 0.0f;
        doc->effects_panel.bands[i].q_width = 1.0f;
    }
    doc->effects_panel.bands[0].freq_hz = 120.0f;
    doc->effects_panel.bands[1].freq_hz = 500.0f;
    doc->effects_panel.bands[2].freq_hz = 2000.0f;
    doc->effects_panel.bands[3].freq_hz = 8000.0f;
    doc->clip_inspector.visible = false;
    doc->clip_inspector.track_index = -1;
    doc->clip_inspector.clip_index = -1;
    doc->clip_inspector.view_source = true;
    doc->clip_inspector.zoom = 1.0f;
    doc->clip_inspector.scroll = 0.0f;
    doc->master_fx = NULL;
    doc->master_fx_count = 0;
}

void session_document_free(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    if (doc->tracks) {
        for (int i = 0; i < doc->track_count; ++i) {
            if (doc->tracks[i].clips) {
                for (int c = 0; c < doc->tracks[i].clip_count; ++c) {
                    SessionClip* clip = &doc->tracks[i].clips[c];
                    if (clip->automation_lanes) {
                        for (int l = 0; l < clip->automation_lane_count; ++l) {
                            SessionAutomationLane* lane = &clip->automation_lanes[l];
                            if (lane->points) {
                                free(lane->points);
                                lane->points = NULL;
                            }
                            lane->point_count = 0;
                        }
                        free(clip->automation_lanes);
                        clip->automation_lanes = NULL;
                        clip->automation_lane_count = 0;
                    }
                }
                free(doc->tracks[i].clips);
                doc->tracks[i].clips = NULL;
            }
            if (doc->tracks[i].fx) {
                free(doc->tracks[i].fx);
                doc->tracks[i].fx = NULL;
            }
            doc->tracks[i].clip_count = 0;
            doc->tracks[i].fx_count = 0;
        }
        free(doc->tracks);
    }
    doc->tracks = NULL;
    doc->track_count = 0;
    if (doc->master_fx) {
        free(doc->master_fx);
        doc->master_fx = NULL;
    }
    doc->master_fx_count = 0;
}

void session_document_reset(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    session_document_free(doc);
    session_document_init(doc);
}

static int count_active_clips(const EngineTrack* track) {
    if (!track || track->clip_count <= 0 || !track->clips) {
        return 0;
    }
    int active = 0;
    for (int i = 0; i < track->clip_count; ++i) {
        const EngineClip* clip = &track->clips[i];
        const char* media_id = engine_clip_get_media_id(clip);
        const char* media_path = engine_clip_get_media_path(clip);
        if (clip->active &&
            ((media_id && media_id[0] != '\0') || (media_path && media_path[0] != '\0'))) {
            ++active;
        }
    }
    return active;
}

// Copies automation lanes from an engine clip into a session clip.
static bool session_clip_copy_automation(const EngineClip* src, SessionClip* dst) {
    if (!src || !dst) {
        return false;
    }
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
    for (int i = 0; i < src->automation_lane_count; ++i) {
        const EngineAutomationLane* src_lane = &src->automation_lanes[i];
        SessionAutomationLane* dst_lane = &dst->automation_lanes[i];
        dst_lane->target = src_lane->target;
        dst_lane->point_count = src_lane->point_count;
        if (src_lane->point_count > 0) {
            dst_lane->points = (SessionAutomationPoint*)calloc((size_t)src_lane->point_count,
                                                               sizeof(SessionAutomationPoint));
            if (!dst_lane->points) {
                return false;
            }
            for (int p = 0; p < src_lane->point_count; ++p) {
                dst_lane->points[p].frame = src_lane->points[p].frame;
                dst_lane->points[p].value = src_lane->points[p].value;
            }
        }
    }
    return true;
}

bool session_document_capture(const AppState* state, SessionDocument* out_doc) {
    if (!state || !out_doc) {
        return false;
    }

    session_document_reset(out_doc);
    out_doc->version = SESSION_DOCUMENT_VERSION;

    const EngineRuntimeConfig* runtime_cfg = NULL;
    if (state->engine) {
        runtime_cfg = engine_get_config(state->engine);
    }
    if (runtime_cfg) {
        out_doc->engine = *runtime_cfg;
    } else {
        out_doc->engine = state->runtime_cfg;
    }
    TempoState tempo_copy = state->tempo;
    tempo_copy.sample_rate = out_doc->engine.sample_rate;
    tempo_state_clamp(&tempo_copy);
    out_doc->tempo.bpm = (float)tempo_copy.bpm;
    out_doc->tempo.ts_num = tempo_copy.ts_num;
    out_doc->tempo.ts_den = tempo_copy.ts_den;

    out_doc->transport_playing = state->engine ? engine_transport_is_playing(state->engine) : false;
    out_doc->transport_frame = state->engine ? engine_get_transport_frame(state->engine) : 0;

    out_doc->loop.enabled = state->loop_enabled && state->loop_end_frame > state->loop_start_frame;
    out_doc->loop.start_frame = state->loop_start_frame;
    out_doc->loop.end_frame = state->loop_end_frame;

    out_doc->timeline.visible_seconds = state->timeline_visible_seconds;
    out_doc->timeline.window_start_seconds = state->timeline_window_start_seconds;
    out_doc->timeline.vertical_scale = state->timeline_vertical_scale;
    out_doc->timeline.show_all_grid_lines = state->timeline_show_all_grid_lines;
    out_doc->timeline.view_in_beats = state->timeline_view_in_beats;
    out_doc->timeline.follow_mode = (int)state->timeline_follow_mode;
    out_doc->timeline.playhead_frame = out_doc->transport_frame;
    out_doc->selected_track_index = state->selected_track_index;
    out_doc->selected_clip_index = state->selected_clip_index;
    EffectsPanelState* panel = &((AppState*)state)->effects_panel;
    session_store_active_eq_curve(panel);
    const EqCurveState* master_curve = &panel->eq_curve_master;
    out_doc->effects_panel.view_mode = (int)panel->view_mode;
    out_doc->effects_panel.selected_index = panel->selected_slot_index;
    out_doc->effects_panel.open_index = panel->list_open_slot_index;
    out_doc->effects_panel.list_detail_mode = (int)panel->list_detail_mode;
    out_doc->effects_panel.eq_view_mode = (int)panel->eq_detail.view_mode;
    out_doc->effects_panel.meter_scope_mode = (int)panel->meter_scope_mode;
    out_doc->effects_panel.meter_lufs_mode = (int)panel->meter_lufs_mode;
    out_doc->effects_panel.meter_spectrogram_mode = (int)panel->meter_spectrogram_mode;
    out_doc->effects_panel.low_cut.enabled = master_curve->low_cut.enabled;
    out_doc->effects_panel.low_cut.freq_hz = master_curve->low_cut.freq_hz;
    out_doc->effects_panel.low_cut.slope = master_curve->low_cut.slope;
    out_doc->effects_panel.high_cut.enabled = master_curve->high_cut.enabled;
    out_doc->effects_panel.high_cut.freq_hz = master_curve->high_cut.freq_hz;
    out_doc->effects_panel.high_cut.slope = master_curve->high_cut.slope;
    for (int i = 0; i < 4; ++i) {
        out_doc->effects_panel.bands[i].enabled = master_curve->bands[i].enabled;
        out_doc->effects_panel.bands[i].freq_hz = master_curve->bands[i].freq_hz;
        out_doc->effects_panel.bands[i].gain_db = master_curve->bands[i].gain_db;
        out_doc->effects_panel.bands[i].q_width = master_curve->bands[i].q_width;
    }
    out_doc->clip_inspector.visible = state->inspector.visible;
    out_doc->clip_inspector.track_index = state->inspector.track_index;
    out_doc->clip_inspector.clip_index = state->inspector.clip_index;
    out_doc->clip_inspector.view_source = state->inspector.waveform.view_source;
    out_doc->clip_inspector.zoom = state->inspector.waveform.zoom;
    out_doc->clip_inspector.scroll = state->inspector.waveform.scroll;

    out_doc->layout.transport_ratio = state->layout_runtime.transport_ratio;
    out_doc->layout.library_ratio = state->layout_runtime.library_ratio;
    out_doc->layout.mixer_ratio = state->layout_runtime.mixer_ratio;

    copy_string(out_doc->library.directory, sizeof(out_doc->library.directory), state->library.directory);
    out_doc->library.selected_index = state->library.selected_index;

    int engine_track_count = state->engine ? engine_get_track_count(state->engine) : 0;
    const EngineTrack* engine_tracks = state->engine ? engine_get_tracks(state->engine) : NULL;
    int total_tracks = engine_track_count;
    if (total_tracks > 0) {
        out_doc->tracks = (SessionTrack*)calloc((size_t)total_tracks, sizeof(SessionTrack));
        if (!out_doc->tracks) {
            SDL_Log("session_document_capture: failed to allocate %d tracks", total_tracks);
            session_document_reset(out_doc);
            return false;
        }
    }
    out_doc->track_count = total_tracks;

    for (int t = 0; t < engine_track_count; ++t) {
        const EngineTrack* src_track = &engine_tracks[t];
        SessionTrack* dst_track = &out_doc->tracks[t];
        copy_string(dst_track->name, sizeof(dst_track->name), src_track->name);
        dst_track->gain = src_track->gain;
        dst_track->pan = src_track->pan;
        dst_track->muted = src_track->muted;
        dst_track->solo = src_track->solo;
        session_eq_curve_set_defaults(&dst_track->eq);
        if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index == t) {
            session_eq_curve_from_state(&dst_track->eq, &panel->eq_curve);
        } else if (panel->eq_curve_tracks && t < panel->eq_curve_tracks_count) {
            session_eq_curve_from_state(&dst_track->eq, &panel->eq_curve_tracks[t]);
        } else {
            session_eq_curve_from_state(&dst_track->eq, master_curve);
        }
        dst_track->fx = NULL;
        dst_track->fx_count = 0;

        int active_clips = count_active_clips(src_track);
        dst_track->clip_count = active_clips;
        if (active_clips > 0) {
            dst_track->clips = (SessionClip*)calloc((size_t)active_clips, sizeof(SessionClip));
            if (!dst_track->clips) {
                SDL_Log("session_document_capture: failed to allocate %d clips for track %d", active_clips, t);
                session_document_reset(out_doc);
                return false;
            }
        }

        int clip_out = 0;
        for (int c = 0; c < src_track->clip_count; ++c) {
            const EngineClip* src_clip = &src_track->clips[c];
            const char* media_id = engine_clip_get_media_id(src_clip);
            const char* media_path = engine_clip_get_media_path(src_clip);
            if (!src_clip->active) {
                continue;
            }
            if ((!media_id || media_id[0] == '\0') && (!media_path || media_path[0] == '\0')) {
                continue;
            }
            SessionClip* dst_clip = &dst_track->clips[clip_out++];
            copy_string(dst_clip->name, sizeof(dst_clip->name), src_clip->name);
            copy_string(dst_clip->media_id, sizeof(dst_clip->media_id), media_id ? media_id : "");
            copy_string(dst_clip->media_path, sizeof(dst_clip->media_path), media_path ? media_path : "");
            dst_clip->start_frame = src_clip->timeline_start_frames;
            dst_clip->duration_frames = src_clip->duration_frames;
            dst_clip->offset_frames = src_clip->offset_frames;
            dst_clip->fade_in_frames = src_clip->fade_in_frames;
            dst_clip->fade_out_frames = src_clip->fade_out_frames;
            dst_clip->fade_in_curve = src_clip->fade_in_curve;
            dst_clip->fade_out_curve = src_clip->fade_out_curve;
            if (!session_clip_copy_automation(src_clip, dst_clip)) {
                SDL_Log("session_document_capture: failed to copy automation for track %d clip %d", t, c);
                session_document_reset(out_doc);
                return false;
            }
            dst_clip->gain = src_clip->gain;
            dst_clip->selected = src_clip->selected;
        }

        FxMasterSnapshot track_fx = {0};
        if (state->engine && engine_fx_track_snapshot(state->engine, t, &track_fx) && track_fx.count > 0) {
            int fx_count = track_fx.count;
            if (fx_count > FX_MASTER_MAX) {
                fx_count = FX_MASTER_MAX;
            }
            SessionFxInstance* fx = (SessionFxInstance*)calloc((size_t)fx_count, sizeof(SessionFxInstance));
            if (!fx) {
                SDL_Log("session_document_capture: failed to allocate fx for track %d", t);
                session_document_reset(out_doc);
                return false;
            }
            dst_track->fx = fx;
            dst_track->fx_count = fx_count;
            for (int i = 0; i < fx_count; ++i) {
                SessionFxInstance* dst_fx = &fx[i];
                const FxMasterInstanceInfo* src = &track_fx.items[i];
                dst_fx->type = src->type;
                dst_fx->enabled = src->enabled;
                dst_fx->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
                for (uint32_t p = 0; p < dst_fx->param_count; ++p) {
                    dst_fx->params[p] = src->params[p];
                    dst_fx->param_mode[p] = src->param_mode[p];
                    dst_fx->param_beats[p] = src->param_beats[p];
                }
                dst_fx->name[0] = '\0';
                if (state->engine) {
                    FxDesc desc = {0};
                    if (engine_fx_registry_get_desc(state->engine, dst_fx->type, &desc) && desc.name) {
                        strncpy(dst_fx->name, desc.name, SESSION_FX_NAME_MAX - 1);
                        dst_fx->name[SESSION_FX_NAME_MAX - 1] = '\0';
                    }
                }
            }
        }
    }

    out_doc->master_fx_count = 0;
    out_doc->master_fx = NULL;
    FxMasterSnapshot snap = {0};
    if (state->engine && engine_fx_master_snapshot(state->engine, &snap) && snap.count > 0) {
        int count = snap.count;
        if (count > FX_MASTER_MAX) {
            count = FX_MASTER_MAX;
        }
        SessionFxInstance* fx = (SessionFxInstance*)calloc((size_t)count, sizeof(SessionFxInstance));
        if (!fx) {
            SDL_Log("session_document_capture: failed to allocate master fx array");
            session_document_reset(out_doc);
            return false;
        }
        out_doc->master_fx = fx;
        out_doc->master_fx_count = count;
        for (int i = 0; i < count; ++i) {
            SessionFxInstance* dst = &fx[i];
            const FxMasterInstanceInfo* src = &snap.items[i];
            dst->type = src->type;
            dst->enabled = src->enabled;
            dst->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < dst->param_count; ++p) {
                dst->params[p] = src->params[p];
                dst->param_mode[p] = src->param_mode[p];
                dst->param_beats[p] = src->param_beats[p];
            }
            dst->name[0] = '\0';
            if (state->engine) {
                FxDesc desc = {0};
                if (engine_fx_registry_get_desc(state->engine, dst->type, &desc) && desc.name) {
                    strncpy(dst->name, desc.name, SESSION_FX_NAME_MAX - 1);
                    dst->name[SESSION_FX_NAME_MAX - 1] = '\0';
                }
            }
        }
    }

    return true;
}
