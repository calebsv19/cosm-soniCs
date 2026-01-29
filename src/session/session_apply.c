#include "session.h"
#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel.h"
#include "ui/library_browser.h"
#include "ui/timeline_view.h"
#include "input/inspector_input.h"
#include "time/tempo.h"
#include "effects/param_utils.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

static void safe_copy_string(char* dst, size_t dst_len, const char* src) {
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

static float clamp_ratio(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
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

static float compute_total_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    if (max_frames == 0) {
        return 0.0f;
    }
    return (float)max_frames / (float)sample_rate;
}

static void clear_pending_track_fx(AppState* state) {
    if (!state) {
        return;
    }
    if (state->pending_track_fx) {
        free(state->pending_track_fx);
        state->pending_track_fx = NULL;
    }
    state->pending_track_fx_count = 0;
    state->pending_track_fx_dirty = false;
}

bool session_apply_document(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return false;
    }
    char error[256] = {0};
    if (!session_document_validate(doc, error, sizeof(error))) {
        SDL_Log("session_apply_document: document invalid: %s", error[0] ? error : "unknown error");
        return false;
    }

    if (state->engine) {
        engine_stop(state->engine);
        engine_destroy(state->engine);
        state->engine = NULL;
    }

    state->runtime_cfg = doc->engine;
    state->engine = engine_create(&state->runtime_cfg);
    if (!state->engine) {
        SDL_Log("session_apply_document: failed to create engine");
        return false;
    }

    int existing_tracks = engine_get_track_count(state->engine);
    while (existing_tracks > 0) {
        engine_remove_track(state->engine, existing_tracks - 1);
        existing_tracks = engine_get_track_count(state->engine);
    }

    state->timeline_visible_seconds = doc->timeline.visible_seconds;
    state->timeline_window_start_seconds = doc->timeline.window_start_seconds;
    state->timeline_vertical_scale = doc->timeline.vertical_scale;
    state->timeline_show_all_grid_lines = doc->timeline.show_all_grid_lines;
    state->timeline_view_in_beats = doc->timeline.view_in_beats;
    state->timeline_follow_mode = (TimelineFollowMode)doc->timeline.follow_mode;
    if (state->timeline_follow_mode < TIMELINE_FOLLOW_OFF ||
        state->timeline_follow_mode > TIMELINE_FOLLOW_SMOOTH) {
        state->timeline_follow_mode = TIMELINE_FOLLOW_JUMP;
    }
    state->effects_panel.view_mode = doc->effects_panel.view_mode == FX_PANEL_VIEW_LIST
                                         ? FX_PANEL_VIEW_LIST
                                         : FX_PANEL_VIEW_STACK;
    if (doc->effects_panel.list_detail_mode == FX_LIST_DETAIL_EQ) {
        state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EQ;
    } else if (doc->effects_panel.list_detail_mode == FX_LIST_DETAIL_METER) {
        state->effects_panel.list_detail_mode = FX_LIST_DETAIL_METER;
    } else {
        state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EFFECT;
    }
    state->effects_panel.eq_detail.view_mode =
        doc->effects_panel.eq_view_mode == EQ_DETAIL_VIEW_TRACK ? EQ_DETAIL_VIEW_TRACK : EQ_DETAIL_VIEW_MASTER;
    if (doc->effects_panel.meter_scope_mode == FX_METER_SCOPE_LEFT_RIGHT) {
        state->effects_panel.meter_scope_mode = FX_METER_SCOPE_LEFT_RIGHT;
    } else {
        state->effects_panel.meter_scope_mode = FX_METER_SCOPE_MID_SIDE;
    }
    if (doc->effects_panel.meter_lufs_mode == FX_METER_LUFS_MOMENTARY) {
        state->effects_panel.meter_lufs_mode = FX_METER_LUFS_MOMENTARY;
    } else if (doc->effects_panel.meter_lufs_mode == FX_METER_LUFS_INTEGRATED) {
        state->effects_panel.meter_lufs_mode = FX_METER_LUFS_INTEGRATED;
    } else {
        state->effects_panel.meter_lufs_mode = FX_METER_LUFS_SHORT_TERM;
    }
    if (doc->effects_panel.meter_spectrogram_mode == FX_METER_SPECTROGRAM_BLACK_WHITE) {
        state->effects_panel.meter_spectrogram_mode = FX_METER_SPECTROGRAM_BLACK_WHITE;
    } else if (doc->effects_panel.meter_spectrogram_mode == FX_METER_SPECTROGRAM_HEAT) {
        state->effects_panel.meter_spectrogram_mode = FX_METER_SPECTROGRAM_HEAT;
    } else {
        state->effects_panel.meter_spectrogram_mode = FX_METER_SPECTROGRAM_WHITE_BLACK;
    }
    state->effects_panel.selected_slot_index = -1;
    state->effects_panel.list_open_slot_index = -1;
    state->effects_panel.restore_selected_index = doc->effects_panel.selected_index;
    state->effects_panel.restore_open_index = doc->effects_panel.open_index;
    state->effects_panel.restore_pending = true;
    state->effects_panel.eq_curve.selected_band = -1;
    state->effects_panel.eq_curve.selected_handle = EQ_CURVE_HANDLE_NONE;
    state->effects_panel.eq_curve.hover_band = -1;
    state->effects_panel.eq_curve.hover_handle = EQ_CURVE_HANDLE_NONE;
    state->effects_panel.eq_curve.hover_toggle_band = -1;
    state->effects_panel.eq_curve.hover_toggle_low = false;
    state->effects_panel.eq_curve.hover_toggle_high = false;
    state->effects_panel.eq_curve_master.selected_band = -1;
    state->effects_panel.eq_curve_master.selected_handle = EQ_CURVE_HANDLE_NONE;
    state->effects_panel.eq_curve_master.hover_band = -1;
    state->effects_panel.eq_curve_master.hover_handle = EQ_CURVE_HANDLE_NONE;
    state->effects_panel.eq_curve_master.hover_toggle_band = -1;
    state->effects_panel.eq_curve_master.hover_toggle_low = false;
    state->effects_panel.eq_curve_master.hover_toggle_high = false;
    state->effects_panel.eq_curve_master.low_cut.enabled = doc->effects_panel.low_cut.enabled;
    state->effects_panel.eq_curve_master.low_cut.freq_hz =
        clamp_float(doc->effects_panel.low_cut.freq_hz, 20.0f, 20000.0f);
    state->effects_panel.eq_curve_master.low_cut.slope = doc->effects_panel.low_cut.slope;
    state->effects_panel.eq_curve_master.high_cut.enabled = doc->effects_panel.high_cut.enabled;
    state->effects_panel.eq_curve_master.high_cut.freq_hz =
        clamp_float(doc->effects_panel.high_cut.freq_hz, 20.0f, 20000.0f);
    state->effects_panel.eq_curve_master.high_cut.slope = doc->effects_panel.high_cut.slope;
    if (state->effects_panel.eq_curve_master.low_cut.enabled &&
        state->effects_panel.eq_curve_master.high_cut.enabled &&
        state->effects_panel.eq_curve_master.low_cut.freq_hz > state->effects_panel.eq_curve_master.high_cut.freq_hz) {
        state->effects_panel.eq_curve_master.high_cut.freq_hz =
            clamp_float(state->effects_panel.eq_curve_master.low_cut.freq_hz * 1.02f, 20.0f, 20000.0f);
    }
    for (int i = 0; i < 4; ++i) {
        state->effects_panel.eq_curve_master.bands[i].enabled = doc->effects_panel.bands[i].enabled;
        state->effects_panel.eq_curve_master.bands[i].freq_hz =
            clamp_float(doc->effects_panel.bands[i].freq_hz, 20.0f, 20000.0f);
        state->effects_panel.eq_curve_master.bands[i].gain_db =
            clamp_float(doc->effects_panel.bands[i].gain_db, -20.0f, 20.0f);
        state->effects_panel.eq_curve_master.bands[i].q_width =
            clamp_float(doc->effects_panel.bands[i].q_width, 0.1f, 4.0f);
    }
    state->effects_panel.eq_curve = state->effects_panel.eq_curve_master;
    if (state->engine) {
        EngineEqCurve curve;
        eq_curve_to_engine(&state->effects_panel.eq_curve_master, &curve);
        engine_set_master_eq_curve(state->engine, &curve);
    }

    state->layout_runtime.transport_ratio = clamp_ratio(doc->layout.transport_ratio);
    state->layout_runtime.library_ratio = clamp_ratio(doc->layout.library_ratio);
    state->layout_runtime.mixer_ratio = clamp_ratio(doc->layout.mixer_ratio);

    state->loop_enabled = doc->loop.enabled && doc->loop.end_frame > doc->loop.start_frame;
    state->loop_start_frame = doc->loop.start_frame;
    state->loop_end_frame = doc->loop.end_frame;
    state->loop_restart_pending = false;
    if (state->engine) {
        engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame);
    }

    library_browser_init(&state->library, doc->library.directory[0] ? doc->library.directory : "assets/audio");
    library_browser_scan(&state->library, &state->media_registry);
    if (doc->library.selected_index >= 0 && doc->library.selected_index < state->library.count) {
        state->library.selected_index = doc->library.selected_index;
    } else {
        state->library.selected_index = state->library.count > 0 ? 0 : -1;
    }

    state->active_track_index = -1;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    bool selected_from_clip = false;

    // Tempo: apply document values with clamping and align sample rate to current engine config.
    state->tempo_map.sample_rate = state->runtime_cfg.sample_rate;
    if (doc->tempo_event_count > 0 && doc->tempo_events) {
        TempoEvent* events = (TempoEvent*)calloc((size_t)doc->tempo_event_count, sizeof(TempoEvent));
        if (events) {
            for (int i = 0; i < doc->tempo_event_count; ++i) {
                events[i].beat = doc->tempo_events[i].beat;
                events[i].bpm = doc->tempo_events[i].bpm;
            }
            tempo_map_set_events(&state->tempo_map, events, doc->tempo_event_count);
            free(events);
        }
    } else {
        TempoEvent default_event = {.beat = 0.0, .bpm = doc->tempo.bpm > 0.0f ? doc->tempo.bpm : 120.0f};
        tempo_map_set_events(&state->tempo_map, &default_event, 1);
    }
    if (doc->time_signature_event_count > 0 && doc->time_signature_events) {
        TimeSignatureEvent* events =
            (TimeSignatureEvent*)calloc((size_t)doc->time_signature_event_count, sizeof(TimeSignatureEvent));
        if (events) {
            for (int i = 0; i < doc->time_signature_event_count; ++i) {
                events[i].beat = doc->time_signature_events[i].beat;
                events[i].ts_num = doc->time_signature_events[i].ts_num;
                events[i].ts_den = doc->time_signature_events[i].ts_den;
            }
            time_signature_map_set_events(&state->time_signature_map, events, doc->time_signature_event_count);
            free(events);
        }
    } else {
        TimeSignatureEvent default_event = {.beat = 0.0, .ts_num = doc->tempo.ts_num, .ts_den = doc->tempo.ts_den};
        time_signature_map_set_events(&state->time_signature_map, &default_event, 1);
    }

    state->tempo = tempo_state_default(state->runtime_cfg.sample_rate);
    const TempoEvent* base_tempo = tempo_map_event_at_beat(&state->tempo_map, 0.0);
    const TimeSignatureEvent* base_sig = time_signature_map_event_at_beat(&state->time_signature_map, 0.0);
    if (base_tempo) {
        state->tempo.bpm = base_tempo->bpm;
    } else if (doc->tempo.bpm > 0.0f) {
        state->tempo.bpm = doc->tempo.bpm;
    }
    if (base_sig) {
        state->tempo.ts_num = base_sig->ts_num;
        state->tempo.ts_den = base_sig->ts_den;
    } else {
        if (doc->tempo.ts_num > 0) {
            state->tempo.ts_num = doc->tempo.ts_num;
        }
        if (doc->tempo.ts_den > 0) {
            state->tempo.ts_den = doc->tempo.ts_den;
        }
    }
    state->tempo.sample_rate = state->runtime_cfg.sample_rate;
    tempo_state_clamp(&state->tempo);

    clear_pending_track_fx(state);
    if (doc->track_count > 0) {
        state->pending_track_fx = (PendingTrackFxEntry*)calloc((size_t)doc->track_count, sizeof(PendingTrackFxEntry));
        if (state->pending_track_fx) {
            state->pending_track_fx_count = doc->track_count;
        }
    }

    effects_panel_ensure_eq_curve_tracks(state, doc->track_count);
    int migrated_media_id_count = 0;
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track_doc = &doc->tracks[t];
        int track_index = engine_add_track(state->engine);
        if (track_index < 0) {
            SDL_Log("session_apply_document: failed to add track %d", t);
            continue;
        }
        engine_track_set_name(state->engine, track_index, track_doc->name);
        engine_track_set_gain(state->engine, track_index, track_doc->gain == 0.0f ? 1.0f : track_doc->gain);
        engine_track_set_pan(state->engine, track_index, track_doc->pan);
        engine_track_set_muted(state->engine, track_index, track_doc->muted);
        engine_track_set_solo(state->engine, track_index, track_doc->solo);
        if (state->effects_panel.eq_curve_tracks && t < state->effects_panel.eq_curve_tracks_count) {
            eq_curve_from_session(&state->effects_panel.eq_curve_tracks[t], &track_doc->eq);
            if (state->engine) {
                EngineEqCurve curve;
                eq_curve_to_engine(&state->effects_panel.eq_curve_tracks[t], &curve);
                engine_set_track_eq_curve(state->engine, track_index, &curve);
            }
        }

        for (int c = 0; c < track_doc->clip_count; ++c) {
            SessionClip* clip_doc = &track_doc->clips[c];
            const char* resolved_path = clip_doc->media_path;
            const char* resolved_id = clip_doc->media_id;
            MediaRegistryEntry resolved_entry = {0};
            if (clip_doc->media_id[0] != '\0') {
                const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, clip_doc->media_id);
                if (entry && entry->path[0] != '\0') {
                    resolved_path = entry->path;
                } else if (clip_doc->media_path[0] != '\0') {
                    if (media_registry_ensure_for_path(&state->media_registry,
                                                       clip_doc->media_path,
                                                       clip_doc->name,
                                                       &resolved_entry)) {
                        resolved_id = resolved_entry.id;
                        resolved_path = resolved_entry.path;
                        migrated_media_id_count++;
                    }
                }
            } else if (clip_doc->media_path[0] != '\0') {
                if (media_registry_ensure_for_path(&state->media_registry,
                                                   clip_doc->media_path,
                                                   clip_doc->name,
                                                   &resolved_entry)) {
                    resolved_id = resolved_entry.id;
                    migrated_media_id_count++;
                }
            }
            if (resolved_id && resolved_id[0] != '\0') {
                safe_copy_string(clip_doc->media_id, sizeof(clip_doc->media_id), resolved_id);
            }
            if (resolved_path && resolved_path[0] != '\0' &&
                strcmp(clip_doc->media_path, resolved_path) != 0) {
                safe_copy_string(clip_doc->media_path, sizeof(clip_doc->media_path), resolved_path);
            }
            if (!resolved_path || resolved_path[0] == '\0') {
                SDL_Log("session_apply_document: track %d clip %d missing media path", t, c);
                continue;
            }
            int clip_index = -1;
            if (!engine_add_clip_to_track_with_id(state->engine,
                                                  track_index,
                                                  resolved_path,
                                                  resolved_id,
                                                  clip_doc->start_frame,
                                                  &clip_index)) {
                SDL_Log("session_apply_document: failed to load clip %s", resolved_path);
                continue;
            }
            engine_clip_set_region(state->engine, track_index, clip_index, clip_doc->offset_frames, clip_doc->duration_frames);
            engine_clip_set_gain(state->engine, track_index, clip_index, clip_doc->gain == 0.0f ? 1.0f : clip_doc->gain);
            engine_clip_set_name(state->engine, track_index, clip_index, clip_doc->name);
            engine_clip_set_fades(state->engine, track_index, clip_index, clip_doc->fade_in_frames, clip_doc->fade_out_frames);
            engine_clip_set_fade_curves(state->engine,
                                        track_index,
                                        clip_index,
                                        clip_doc->fade_in_curve,
                                        clip_doc->fade_out_curve);
            if (clip_doc->automation_lanes && clip_doc->automation_lane_count > 0) {
                for (int l = 0; l < clip_doc->automation_lane_count; ++l) {
                    SessionAutomationLane* lane = &clip_doc->automation_lanes[l];
                    engine_clip_set_automation_lane_points(state->engine,
                                                           track_index,
                                                           clip_index,
                                                           lane->target,
                                                           (const EngineAutomationPoint*)lane->points,
                                                           lane->point_count);
                }
            }
            if (clip_doc->selected && state->selected_track_index == -1) {
                state->selected_track_index = track_index;
                state->selected_clip_index = clip_index;
                state->active_track_index = track_index;
                selected_from_clip = true;
            }
        }

        if (state->pending_track_fx && t < state->pending_track_fx_count) {
            PendingTrackFxEntry* pending = &state->pending_track_fx[t];
            int count = track_doc->fx_count;
            if (count > FX_MASTER_MAX) count = FX_MASTER_MAX;
            pending->fx_count = count;
            for (int f = 0; f < count; ++f) {
                pending->fx[f] = track_doc->fx[f];
            }
        }
    }
    if (migrated_media_id_count > 0) {
        SDL_Log("session_apply_document: migrated %d clips to media_id", migrated_media_id_count);
    }

    if (!selected_from_clip && doc->selected_track_index >= 0 && doc->selected_track_index < doc->track_count) {
        state->selected_track_index = doc->selected_track_index;
        state->active_track_index = doc->selected_track_index;
        int clip_count = doc->tracks[doc->selected_track_index].clip_count;
        if (doc->selected_clip_index >= 0 && doc->selected_clip_index < clip_count) {
            state->selected_clip_index = doc->selected_clip_index;
        } else {
            state->selected_clip_index = -1;
        }
    }
    if (state->selected_track_index == -1 && doc->track_count > 0) {
        state->active_track_index = 0;
        state->selected_track_index = 0;
        state->selected_clip_index = doc->tracks[0].clip_count > 0 ? 0 : -1;
    }

    if (doc->clip_inspector.visible &&
        doc->clip_inspector.track_index >= 0 &&
        doc->clip_inspector.track_index < engine_get_track_count(state->engine)) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        const EngineTrack* track = tracks ? &tracks[doc->clip_inspector.track_index] : NULL;
        if (track &&
            doc->clip_inspector.clip_index >= 0 &&
            doc->clip_inspector.clip_index < track->clip_count) {
            inspector_input_show(state,
                                 doc->clip_inspector.track_index,
                                 doc->clip_inspector.clip_index,
                                 &track->clips[doc->clip_inspector.clip_index]);
            state->inspector.waveform.view_source = doc->clip_inspector.view_source;
            state->inspector.waveform.zoom = doc->clip_inspector.zoom > 0.0f ? doc->clip_inspector.zoom : 1.0f;
            state->inspector.waveform.scroll = doc->clip_inspector.scroll;
        } else {
            inspector_input_init(state);
        }
    } else {
        inspector_input_init(state);
    }

    memset(state->pending_master_fx, 0, sizeof(state->pending_master_fx));
    state->pending_master_fx_count = 0;
    state->pending_master_fx_dirty = false;
    if (doc->master_fx_count > 0) {
        int count = doc->master_fx_count;
        if (count > FX_MASTER_MAX) {
            count = FX_MASTER_MAX;
        }
        state->pending_master_fx_count = count;
        for (int i = 0; i < count; ++i) {
            PendingMasterFx* dst = &state->pending_master_fx[i];
            const SessionFxInstance* src = &doc->master_fx[i];
            dst->type_id = src->type;
            dst->enabled = src->enabled;
            dst->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < dst->param_count; ++p) {
                dst->param_values[p] = src->params[p];
                dst->param_mode[p] = src->param_mode[p];
                dst->param_beats[p] = src->param_beats[p];
            }
        }
        state->pending_master_fx_dirty = true;
    }

    if (state->pending_track_fx_count > 0 && state->pending_track_fx) {
        state->pending_track_fx_dirty = true;
    }

    state->timeline_visible_seconds = clamp_float(state->timeline_visible_seconds,
                                                  TIMELINE_MIN_VISIBLE_SECONDS,
                                                  TIMELINE_MAX_VISIBLE_SECONDS);
    float total_seconds = compute_total_seconds(state);
    float max_start = total_seconds > state->timeline_visible_seconds
                          ? total_seconds - state->timeline_visible_seconds
                          : 0.0f;
    if (max_start < 0.0f) {
        max_start = 0.0f;
    }
    state->timeline_window_start_seconds = clamp_float(state->timeline_window_start_seconds, 0.0f, max_start);

    return true;
}

void session_apply_pending_master_fx(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (state->pending_master_fx_count <= 0) {
        state->pending_master_fx_dirty = false;
        return;
    }

    FxMasterSnapshot existing = {0};
    if (engine_fx_master_snapshot(state->engine, &existing)) {
        for (int i = 0; i < existing.count; ++i) {
            engine_fx_master_remove(state->engine, existing.items[i].id);
        }
    }

    for (int i = 0; i < state->pending_master_fx_count && i < FX_MASTER_MAX; ++i) {
        const PendingMasterFx* fx = &state->pending_master_fx[i];
        if (fx->type_id == 0) {
            continue;
        }
        FxInstId id = engine_fx_master_add(state->engine, fx->type_id);
        if (!id) {
            continue;
        }
        FxDesc desc = {0};
        engine_fx_registry_get_desc(state->engine, fx->type_id, &desc);
        uint32_t count = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
        for (uint32_t p = 0; p < count; ++p) {
            FxParamMode mode = fx->param_mode[p];
            float beat_value = fx->param_beats[p];
            float native_value = fx->param_values[p];
            FxParamKind kind = fx_param_kind_from_name((p < desc.num_params) ? desc.param_names[p] : NULL);
            bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_kind_is_syncable(kind);
            if (use_sync) {
                native_value = fx_param_beats_to_native(kind, beat_value, &state->tempo);
            }
            if (use_sync) {
                engine_fx_master_set_param_with_mode(state->engine, id, p, native_value, mode, beat_value);
            } else {
                engine_fx_master_set_param(state->engine, id, p, native_value);
            }
        }
        if (!fx->enabled) {
            engine_fx_master_set_enabled(state->engine, id, false);
        }
    }
    state->pending_master_fx_dirty = false;
}

void session_apply_pending_track_fx(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (!state->pending_track_fx_dirty || state->pending_track_fx_count <= 0 || !state->pending_track_fx) {
        state->pending_track_fx_dirty = false;
        return;
    }

    engine_fx_set_track_count(state->engine, engine_get_track_count(state->engine));
    int track_count = engine_get_track_count(state->engine);
    for (int t = 0; t < state->pending_track_fx_count && t < track_count; ++t) {
        const PendingTrackFxEntry* pending = &state->pending_track_fx[t];
        if (!pending || pending->fx_count <= 0) {
            continue;
        }
        for (int f = 0; f < pending->fx_count && f < FX_MASTER_MAX; ++f) {
            const SessionFxInstance* fx = &pending->fx[f];
            if (!fx || fx->type == 0) {
                continue;
            }
            FxInstId id = engine_fx_track_add(state->engine, t, fx->type);
            if (id == 0) {
                continue;
            }
            FxDesc desc = {0};
            engine_fx_registry_get_desc(state->engine, fx->type, &desc);
            uint32_t pcount = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
            for (uint32_t p = 0; p < pcount; ++p) {
                FxParamMode mode = fx->param_mode[p];
                float beat_value = fx->param_beats[p];
                float native_value = fx->params[p];
                FxParamKind kind = fx_param_kind_from_name((p < desc.num_params) ? desc.param_names[p] : NULL);
                bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_kind_is_syncable(kind);
                if (use_sync) {
                    native_value = fx_param_beats_to_native(kind, beat_value, &state->tempo);
                }
                if (use_sync) {
                    engine_fx_track_set_param_with_mode(state->engine, t, id, p, native_value, mode, beat_value);
                } else {
                    engine_fx_track_set_param(state->engine, t, id, p, native_value);
                }
            }
            if (!fx->enabled) {
                engine_fx_track_set_enabled(state->engine, t, id, false);
            }
        }
    }
    state->pending_track_fx_dirty = false;
}

bool session_load_from_file(AppState* state, const char* path) {
    if (!state) {
        SDL_Log("session_load_from_file: app state is null");
        return false;
    }
    SessionDocument doc;
    session_document_init(&doc);
    if (!session_document_read_file(path, &doc)) {
        session_document_free(&doc);
        return false;
    }
    bool applied = session_apply_document(state, &doc);
    session_document_free(&doc);
    return applied;
}
