#include "app/media_import.h"
#include "session.h"
#include "app_state.h"
#include "daw/data_paths.h"
#include "engine/engine.h"
#include "input/timeline_selection.h"
#include "ui/effects_panel.h"
#include "ui/library_browser.h"
#include "ui/timeline_view.h"
#include "input/inspector_input.h"
#include "time/tempo.h"
#include "effects/param_utils.h"
#include "input/midi_editor_input.h"
#include "undo/undo_manager.h"

#include <string.h>

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <unistd.h>
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

static void session_apply_data_paths(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return;
    }
    if (!daw_data_paths_valid(&state->data_paths)) {
        daw_data_paths_set_defaults(&state->data_paths);
    }
    if (doc->version >= 17) {
        if (doc->data_paths.input_root[0] != '\0') {
            safe_copy_string(state->data_paths.input_root,
                             sizeof(state->data_paths.input_root),
                             doc->data_paths.input_root);
        }
        if (doc->data_paths.output_root[0] != '\0') {
            safe_copy_string(state->data_paths.output_root,
                             sizeof(state->data_paths.output_root),
                             doc->data_paths.output_root);
        }
        if (doc->data_paths.library_copy_root[0] != '\0') {
            safe_copy_string(state->data_paths.library_copy_root,
                             sizeof(state->data_paths.library_copy_root),
                             doc->data_paths.library_copy_root);
        }
    }
    daw_data_paths_apply_runtime_policy(&state->data_paths);
}

static bool session_selected_clip_is_midi(const AppState* state) {
    if (!state || !state->engine ||
        state->selected_track_index < 0 ||
        state->selected_clip_index < 0) {
        return false;
    }
    int track_count = engine_get_track_count(state->engine);
    if (state->selected_track_index >= track_count) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    if (!tracks) {
        return false;
    }
    const EngineTrack* track = &tracks[state->selected_track_index];
    if (state->selected_clip_index >= track->clip_count) {
        return false;
    }
    return engine_clip_get_kind(&track->clips[state->selected_clip_index]) == ENGINE_CLIP_KIND_MIDI;
}

static void session_apply_midi_editor_state(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return;
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_menu_scroll_row = 0;
    state->midi_editor_ui.instrument_menu_expanded_category = -1;
    state->midi_editor_ui.instrument_param_drag_active = false;
    state->midi_editor_ui.instrument_param_drag_index = -1;
    state->midi_editor_ui.instrument_param_drag_start_y = 0;
    state->midi_editor_ui.instrument_param_drag_start_value = 0.0f;

    if (doc->midi_editor.instrument_active_group >= 0 &&
        doc->midi_editor.instrument_active_group < engine_instrument_param_group_count()) {
        state->midi_editor_ui.instrument_active_group =
            (EngineInstrumentParamGroupId)doc->midi_editor.instrument_active_group;
    } else {
        state->midi_editor_ui.instrument_active_group = ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT;
    }
    state->midi_editor_ui.quantize_division = doc->midi_editor.quantize_division > 0
                                                  ? doc->midi_editor.quantize_division
                                                  : 16;
    state->midi_editor_ui.default_velocity = doc->midi_editor.default_velocity;
    if (state->midi_editor_ui.default_velocity < 0.0f) {
        state->midi_editor_ui.default_velocity = 0.0f;
    } else if (state->midi_editor_ui.default_velocity > 1.0f) {
        state->midi_editor_ui.default_velocity = 1.0f;
    }
    state->midi_editor_ui.qwerty_octave_offset = doc->midi_editor.qwerty_octave_offset;

    if (doc->midi_editor.panel_mode == MIDI_REGION_PANEL_INSTRUMENT &&
        session_selected_clip_is_midi(state)) {
        state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;
        state->midi_editor_ui.qwerty_record_armed = false;
        state->midi_editor_ui.qwerty_test_enabled = false;
    }
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

// Maps a document clip identity to the engine's chronological, creation-order sorting.
static int session_restored_clip_index(const SessionDocument* doc, int track_index, int clip_index) {
    if (track_index < 0 || track_index >= doc->track_count || clip_index < 0 ||
        clip_index >= doc->tracks[track_index].clip_count) return -1;
    const SessionTrack* track = &doc->tracks[track_index];
    int result = 0;
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].start_frame < track->clips[clip_index].start_frame ||
            (track->clips[i].start_frame == track->clips[clip_index].start_frame && i < clip_index)) ++result;
    }
    return result;
}

static bool session_resolve_clip_creation_index(const AppState* state,
                                                int track_index,
                                                int clip_index,
                                                uint64_t* out_creation_index) {
    if (!state || !state->engine || !out_creation_index) {
        return false;
    }
    int track_count = engine_get_track_count(state->engine);
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return false;
    }
    const EngineTrack* track = &tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    *out_creation_index = track->clips[clip_index].creation_index;
    return true;
}

static void session_restore_timeline_selection(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return;
    }
    timeline_selection_restore_clear_entries(state);
    if (!state->engine || doc->selection_count <= 0) {
        return;
    }
    int track_count = engine_get_track_count(state->engine);
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    if (!tracks) {
        return;
    }
    for (int i = 0; i < doc->selection_count; ++i) {
        int track_index = doc->selection[i].track_index;
        int clip_index = session_restored_clip_index(doc, track_index, doc->selection[i].clip_index);
        if (track_index < 0 || track_index >= track_count) {
            continue;
        }
        if (clip_index < 0 || clip_index >= tracks[track_index].clip_count) {
            continue;
        }
        if (!timeline_selection_restore_append_entry(state, track_index, clip_index)) {
            break;
        }
    }
}

static void session_restore_midi_editor_viewports(AppState* state, const SessionDocument* doc) {
    uint64_t creation_index = 0;
    if (!state || !doc) {
        return;
    }
    state->midi_editor_ui.viewport_track_index = doc->midi_editor.viewport_track_index;
    state->midi_editor_ui.viewport_clip_index = session_restored_clip_index(doc, doc->midi_editor.viewport_track_index, doc->midi_editor.viewport_clip_index);
    state->midi_editor_ui.viewport_start_frame = doc->midi_editor.viewport_start_frame;
    state->midi_editor_ui.viewport_span_frames = doc->midi_editor.viewport_span_frames;
    if (session_resolve_clip_creation_index(state,
                                            doc->midi_editor.viewport_track_index,
                                            state->midi_editor_ui.viewport_clip_index,
                                            &creation_index)) {
        state->midi_editor_ui.viewport_clip_creation_index = creation_index;
    } else {
        state->midi_editor_ui.viewport_track_index = -1;
        state->midi_editor_ui.viewport_clip_index = -1;
        state->midi_editor_ui.viewport_clip_creation_index = 0;
        state->midi_editor_ui.viewport_start_frame = 0;
        state->midi_editor_ui.viewport_span_frames = 0;
    }

    state->midi_editor_ui.pitch_viewport_track_index = doc->midi_editor.pitch_viewport_track_index;
    state->midi_editor_ui.pitch_viewport_clip_index = session_restored_clip_index(doc, doc->midi_editor.pitch_viewport_track_index, doc->midi_editor.pitch_viewport_clip_index);
    state->midi_editor_ui.pitch_viewport_top_note = doc->midi_editor.pitch_viewport_top_note;
    state->midi_editor_ui.pitch_viewport_row_count = doc->midi_editor.pitch_viewport_row_count;
    if (session_resolve_clip_creation_index(state,
                                            doc->midi_editor.pitch_viewport_track_index,
                                            state->midi_editor_ui.pitch_viewport_clip_index,
                                            &creation_index)) {
        state->midi_editor_ui.pitch_viewport_clip_creation_index = creation_index;
    } else {
        state->midi_editor_ui.pitch_viewport_track_index = -1;
        state->midi_editor_ui.pitch_viewport_clip_index = -1;
        state->midi_editor_ui.pitch_viewport_clip_creation_index = 0;
    }
}

// Names the failed preparation operation while retaining the original project for retry.
static bool session_require_operation(bool accepted, const char* operation) {
    if (!accepted) SDL_Log("session restore: candidate rejected at %s", operation);
    return accepted;
}

// Populates only an isolated replacement state; any failed required edit rejects the candidate.
static bool session_prepare_document(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return false;
    }
    char error[256] = {0};
    if (!session_document_validate(doc, error, sizeof(error))) {
        SDL_Log("session_apply_document: document invalid: %s", error[0] ? error : "unknown error");
        return false;
    }

    state->runtime_cfg = doc->engine;
    state->engine = engine_create(&state->runtime_cfg);
    if (!state->engine) {
        SDL_Log("session_apply_document: failed to create engine");
        return false;
    }

    int existing_tracks = engine_get_track_count(state->engine);
    while (existing_tracks > 0) {
        if (!session_require_operation(engine_remove_track(state->engine, existing_tracks - 1), "engine_remove_track")) return false;
        existing_tracks = engine_get_track_count(state->engine);
    }

    state->timeline_visible_seconds = doc->timeline.visible_seconds;
    state->timeline_window_start_seconds = doc->timeline.window_start_seconds;
    state->timeline_vertical_scale = doc->timeline.vertical_scale;
    state->timeline_show_all_grid_lines = doc->timeline.show_all_grid_lines;
    state->timeline_view_in_beats = doc->timeline.view_in_beats;
    state->timeline_snap_enabled = doc->timeline.snap_enabled;
    state->timeline_automation_mode = doc->timeline.automation_mode;
    state->timeline_automation_labels_enabled = doc->timeline.automation_labels_enabled;
    state->timeline_tempo_overlay_enabled = doc->timeline.tempo_overlay_enabled;
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
        if (!session_require_operation(engine_set_master_eq_curve(state->engine, &curve), "engine_set_master_eq_curve")) return false;
    }

    state->layout_runtime.transport_ratio = clamp_ratio(doc->layout.transport_ratio);
    state->layout_runtime.library_ratio = clamp_ratio(doc->layout.library_ratio);
    state->layout_runtime.mixer_ratio = clamp_ratio(doc->layout.mixer_ratio);

    state->loop_enabled = doc->loop.enabled && doc->loop.end_frame > doc->loop.start_frame;
    state->loop_start_frame = doc->loop.start_frame;
    state->loop_end_frame = doc->loop.end_frame;
    state->loop_restart_pending = false;
    if (state->engine) {
        if (!session_require_operation(engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame), "engine_transport_set_loop")) return false;
    }

    session_apply_data_paths(state, doc);
    bool doc_has_input_root = doc->version >= 17 && doc->data_paths.input_root[0] != '\0';
    const char* library_root = doc_has_input_root
                                   ? state->data_paths.input_root
                                   : (doc->library.directory[0]
                                          ? doc->library.directory
                                          : daw_data_paths_library_root(&state->data_paths));
    library_browser_init(&state->library, library_root);
    // Library scanning and registry persistence happen only after commit.
    if (doc->library.selected_index >= 0 && doc->library.selected_index < state->library.count) {
        state->library.selected_index = doc->library.selected_index;
    } else {
        state->library.selected_index = state->library.count > 0 ? 0 : -1;
    }
    if (doc->library.panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        state->library.panel_mode = LIBRARY_PANEL_MODE_IN_PROJECT;
    } else {
        state->library.panel_mode = LIBRARY_PANEL_MODE_SOURCE;
    }

    timeline_selection_restore_clear(state);
    bool selected_from_clip = false;

    // Tempo: apply document values with clamping and align sample rate to current engine config.
    state->tempo_map.sample_rate = state->runtime_cfg.sample_rate;
    if (doc->tempo_event_count > 0 && doc->tempo_events) {
        TempoEvent* events = (TempoEvent*)calloc((size_t)doc->tempo_event_count, sizeof(TempoEvent));
        if (!events) return false;
        if (events) {
            for (int i = 0; i < doc->tempo_event_count; ++i) {
                events[i].beat = doc->tempo_events[i].beat;
                events[i].bpm = doc->tempo_events[i].bpm;
            }
            bool ok = tempo_map_set_events(&state->tempo_map, events, doc->tempo_event_count);
            free(events);
            if (!ok) return false;
        }
    } else {
        TempoEvent default_event = {.beat = 0.0, .bpm = doc->tempo.bpm > 0.0f ? doc->tempo.bpm : 120.0f};
        if (!session_require_operation(tempo_map_set_events(&state->tempo_map, &default_event, 1), "tempo_map_set_events")) return false;
    }
    if (doc->time_signature_event_count > 0 && doc->time_signature_events) {
        TimeSignatureEvent* events =
            (TimeSignatureEvent*)calloc((size_t)doc->time_signature_event_count, sizeof(TimeSignatureEvent));
        if (!events) return false;
        if (events) {
            for (int i = 0; i < doc->time_signature_event_count; ++i) {
                events[i].beat = doc->time_signature_events[i].beat;
                events[i].ts_num = doc->time_signature_events[i].ts_num;
                events[i].ts_den = doc->time_signature_events[i].ts_den;
            }
            bool ok = time_signature_map_set_events(&state->time_signature_map, events, doc->time_signature_event_count);
            free(events);
            if (!ok) return false;
        }
    } else {
        TimeSignatureEvent default_event = {.beat = 0.0, .ts_num = doc->tempo.ts_num, .ts_den = doc->tempo.ts_den};
        if (!session_require_operation(time_signature_map_set_events(&state->time_signature_map, &default_event, 1), "time_signature_map_set_events")) return false;
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
    if (state->engine) {
        if (!session_require_operation(engine_set_tempo_state(state->engine, &state->tempo), "engine_set_tempo_state")) return false;
    }

    clear_pending_track_fx(state);
    if (doc->track_count > 0) {
        state->pending_track_fx = (PendingTrackFxEntry*)calloc((size_t)doc->track_count, sizeof(PendingTrackFxEntry));
        if (!state->pending_track_fx) return false;
        if (state->pending_track_fx) {
            state->pending_track_fx_count = doc->track_count;
        }
    }

    effects_panel_ensure_eq_curve_tracks(state, doc->track_count);
    if (state->effects_panel.eq_curve_tracks_count != doc->track_count) return false;
    int migrated_media_id_count = 0;
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track_doc = &doc->tracks[t];
        int track_index = engine_add_track(state->engine);
        if (track_index < 0) {
            SDL_Log("session_apply_document: failed to add track %d", t);
            return false;
        }
        if (!session_require_operation(engine_track_set_name(state->engine, track_index, track_doc->name), "engine_track_set_name")) return false;
        if (!session_require_operation(engine_track_set_gain(state->engine, track_index, track_doc->gain), "engine_track_set_gain")) return false;
        if (!session_require_operation(engine_track_set_pan(state->engine, track_index, track_doc->pan), "engine_track_set_pan")) return false;
        if (!session_require_operation(engine_track_set_muted(state->engine, track_index, track_doc->muted), "engine_track_set_muted")) return false;
        if (!session_require_operation(engine_track_set_solo(state->engine, track_index, track_doc->solo), "engine_track_set_solo")) return false;
        if (track_doc->midi_instrument_enabled) {
            if (!session_require_operation(engine_track_midi_set_instrument_preset(state->engine,
                                                    track_index,
                                                    track_doc->midi_instrument_preset), "engine_track_midi_set_instrument_preset")) return false;
            if (!session_require_operation(engine_track_midi_set_instrument_params(state->engine,
                                                    track_index,
                                                    track_doc->midi_instrument_params), "engine_track_midi_set_instrument_params")) return false;
        }
        if (track_doc->midi_instrument_automation_lanes &&
            track_doc->midi_instrument_automation_lane_count > 0) {
            for (int l = 0; l < track_doc->midi_instrument_automation_lane_count; ++l) {
                const SessionAutomationLane* lane = &track_doc->midi_instrument_automation_lanes[l];
                if (!session_require_operation(engine_track_midi_set_instrument_automation_lane_points(state->engine,
                                                                        track_index,
                                                                        lane->target,
                                                                        (const EngineAutomationPoint*)lane->points,
                                                                        lane->point_count), "engine_track_midi_set_instrument_automation_lane_points")) return false;
            }
        } else {
            if (!session_require_operation(engine_track_midi_set_instrument_automation_lanes(state->engine, track_index, NULL, 0), "engine_track_midi_set_instrument_automation_lanes")) return false;
        }
        if (state->effects_panel.eq_curve_tracks && t < state->effects_panel.eq_curve_tracks_count) {
            eq_curve_from_session(&state->effects_panel.eq_curve_tracks[t], &track_doc->eq);
            if (state->engine) {
                EngineEqCurve curve;
                eq_curve_to_engine(&state->effects_panel.eq_curve_tracks[t], &curve);
                if (!session_require_operation(engine_set_track_eq_curve(state->engine, track_index, &curve), "engine_set_track_eq_curve")) return false;
            }
        }

        for (int c = 0; c < track_doc->clip_count; ++c) {
            const SessionClip* clip_doc = &track_doc->clips[c];
            if (clip_doc->kind == ENGINE_CLIP_KIND_MIDI) {
                int clip_index = -1;
                if (!engine_add_midi_clip_to_track(state->engine,
                                                   track_index,
                                                   clip_doc->start_frame,
                                                   clip_doc->duration_frames,
                                                   &clip_index)) {
                    SDL_Log("session_apply_document: failed to create MIDI clip %d:%d", t, c);
                    return false;
                }
                EngineClipTransform transform = {
                    .start_frame = clip_doc->start_frame, .offset_frames = clip_doc->offset_frames,
                    .duration_frames = clip_doc->duration_frames, .gain = clip_doc->gain,
                    .fade_in_frames = clip_doc->fade_in_frames, .fade_out_frames = clip_doc->fade_out_frames,
                    .fade_in_curve = clip_doc->fade_in_curve, .fade_out_curve = clip_doc->fade_out_curve,
                    .instrument_preset = clip_doc->instrument_preset, .instrument_params = clip_doc->instrument_params,
                    .instrument_inherits_track = clip_doc->instrument_inherits_track,
                    .midi_notes = clip_doc->midi_notes, .midi_note_count = clip_doc->midi_note_count,
                };
                if (!session_require_operation(engine_transform_clip(state->engine, track_index, clip_index,
                        track_index, &transform, &clip_index), "MIDI clip contents")) return false;
                if (!engine_clip_set_name(state->engine, track_index, clip_index, clip_doc->name)) return false;
                if (clip_doc->automation_lanes && clip_doc->automation_lane_count > 0) {
                    for (int l = 0; l < clip_doc->automation_lane_count; ++l) {
                        const SessionAutomationLane* lane = &clip_doc->automation_lanes[l];
                        if (!session_require_operation(engine_clip_set_automation_lane_points(state->engine,
                                                               track_index,
                                                               clip_index,
                                                               lane->target,
                                                               (const EngineAutomationPoint*)lane->points,
                                                               lane->point_count), "engine_clip_set_automation_lane_points")) return false;
                    }
                }
                if (clip_doc->selected && state->selected_track_index == -1) {
                    timeline_selection_restore_primary(state, track_index, session_restored_clip_index(doc, t, c), track_index);
                    selected_from_clip = true;
                }
                continue;
            }
            const char* resolved_path = clip_doc->media_path;
            const char* resolved_id = clip_doc->media_id;
            MediaRegistryEntry resolved_entry = {0};
            if (clip_doc->media_id[0]) {
                const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, clip_doc->media_id);
                if (entry && entry->path[0] && access(entry->path, R_OK) == 0) resolved_path = entry->path;
            }
            if (!resolved_path[0] || !media_registry_ensure_for_path(&state->media_registry,
                    resolved_path, clip_doc->name, &resolved_entry)) return false;
            resolved_path = resolved_entry.path;
            resolved_id = resolved_entry.id;
            if (strcmp(clip_doc->media_id, resolved_id)) ++migrated_media_id_count;
            if (!resolved_path || resolved_path[0] == '\0') {
                SDL_Log("session_apply_document: track %d clip %d missing media path", t, c);
                return false;
            }
            int clip_index = -1;
            if (!engine_add_clip_to_track_with_id(state->engine,
                                                  track_index,
                                                  resolved_path,
                                                  resolved_id,
                                                  clip_doc->start_frame,
                                                  &clip_index)) {
                SDL_Log("session_apply_document: failed to load clip %s", resolved_path);
                return false;
            }
            uint64_t media_frames = engine_clip_get_total_frames(state->engine, track_index, clip_index);
            if (clip_doc->offset_frames >= media_frames ||
                (clip_doc->duration_frames && clip_doc->duration_frames > media_frames - clip_doc->offset_frames)) return false;
            if (!session_require_operation(engine_clip_set_region(state->engine, track_index, clip_index, clip_doc->offset_frames, clip_doc->duration_frames), "engine_clip_set_region")) return false;
            if (!session_require_operation(engine_clip_set_gain(state->engine, track_index, clip_index, clip_doc->gain), "engine_clip_set_gain")) return false;
            if (!session_require_operation(engine_clip_set_name(state->engine, track_index, clip_index, clip_doc->name), "engine_clip_set_name")) return false;
            if (!session_require_operation(engine_clip_set_fades(state->engine, track_index, clip_index, clip_doc->fade_in_frames, clip_doc->fade_out_frames), "engine_clip_set_fades")) return false;
            if (!session_require_operation(engine_clip_set_fade_curves(state->engine,
                                        track_index,
                                        clip_index,
                                        clip_doc->fade_in_curve,
                                        clip_doc->fade_out_curve), "engine_clip_set_fade_curves")) return false;
            if (clip_doc->automation_lanes && clip_doc->automation_lane_count > 0) {
                for (int l = 0; l < clip_doc->automation_lane_count; ++l) {
                    const SessionAutomationLane* lane = &clip_doc->automation_lanes[l];
                    if (!session_require_operation(engine_clip_set_automation_lane_points(state->engine,
                                                           track_index,
                                                           clip_index,
                                                           lane->target,
                                                           (const EngineAutomationPoint*)lane->points,
                                                           lane->point_count), "engine_clip_set_automation_lane_points")) return false;
                }
            }
            if (clip_doc->selected && state->selected_track_index == -1) {
                timeline_selection_restore_primary(state, track_index, session_restored_clip_index(doc, t, c), track_index);
                selected_from_clip = true;
            }
        }

        EngineTrackSettings settings = {
            .gain = track_doc->gain, .pan = track_doc->pan, .muted = track_doc->muted, .solo = track_doc->solo,
            .instrument_enabled = track_doc->midi_instrument_enabled,
            .instrument_preset = track_doc->midi_instrument_preset, .instrument_params = track_doc->midi_instrument_params,
        };
        if (!session_require_operation(engine_track_set_settings(state->engine, track_index, &settings), "engine_track_set_settings")) return false;

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
        int active_track_index = doc->selected_track_index;
        if (doc->active_track_index >= 0 && doc->active_track_index < doc->track_count) {
            active_track_index = doc->active_track_index;
        }
        int clip_count = doc->tracks[doc->selected_track_index].clip_count;
        int selected_clip_index = -1;
        if (doc->selected_clip_index >= 0 && doc->selected_clip_index < clip_count) {
            selected_clip_index = session_restored_clip_index(doc, doc->selected_track_index, doc->selected_clip_index);
        }
        timeline_selection_restore_primary(state, doc->selected_track_index, selected_clip_index, active_track_index);
    }
    if (state->selected_track_index == -1 && doc->track_count > 0) {
        timeline_selection_restore_primary(state, 0, doc->tracks[0].clip_count > 0 ? 0 : -1, 0);
    }
    if (state->active_track_index < 0) {
        timeline_selection_restore_primary(state,
                                           state->selected_track_index,
                                           state->selected_clip_index,
                                           state->selected_track_index);
    }
    session_restore_timeline_selection(state, doc);
    session_apply_midi_editor_state(state, doc);
    if (session_selected_clip_is_midi(state)) {
        uint64_t clip_creation_index = 0;
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (tracks) {
            const EngineTrack* track = &tracks[state->selected_track_index];
            clip_creation_index = track->clips[state->selected_clip_index].creation_index;
        }
        midi_editor_input_set_selected_clip(state,
                                            state->selected_track_index,
                                            state->selected_clip_index,
                                            clip_creation_index);
    } else {
        midi_editor_input_clear_selected_clip(state);
    }
    session_restore_midi_editor_viewports(state, doc);

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
            dst->param_id_count = src->param_id_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_id_count;
            for (uint32_t p = 0; p < dst->param_id_count; ++p) {
                strncpy(dst->param_ids[p], src->param_ids[p], sizeof(dst->param_ids[p]) - 1);
                dst->param_ids[p][sizeof(dst->param_ids[p]) - 1] = '\0';
                dst->param_values_by_id[p] = src->param_values_by_id[p];
                dst->param_modes_by_id[p] = src->param_modes_by_id[p];
                dst->param_beats_by_id[p] = src->param_beats_by_id[p];
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

// Builds a parameter array using spec IDs when available to preserve stable mapping.
static uint32_t session_fx_collect_params(const SessionFxInstance* fx,
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

// Builds a parameter array for pending master FX using spec IDs when available.
static uint32_t session_pending_fx_collect_params(const PendingMasterFx* fx,
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
            out_values[p] = fx->param_values[p];
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
            out_values[p] = fx->param_values[p];
            out_modes[p] = fx->param_mode[p];
            out_beats[p] = fx->param_beats[p];
        }
    }
    return count;
}

bool session_apply_pending_master_fx(AppState* state) {
    if (!state || !state->engine) {
        return true;
    }
    if (!state->pending_master_fx_dirty || state->pending_master_fx_count <= 0) {
        state->pending_master_fx_dirty = false;
        return true;
    }

    FxMasterSnapshot existing = {0};
    if (!engine_fx_master_snapshot(state->engine, &existing)) return false;
    {
        for (int i = 0; i < existing.count; ++i) {
            if (!engine_fx_master_remove(state->engine, existing.items[i].id)) return false;
        }
    }

    for (int i = 0; i < state->pending_master_fx_count && i < FX_MASTER_MAX; ++i) {
        const PendingMasterFx* fx = &state->pending_master_fx[i];
        if (fx->type_id == 0) {
            return false;
        }
        FxInstId id = engine_fx_master_add(state->engine, fx->type_id);
        if (!id) {
            return false;
        }
        const EffectParamSpec* specs = NULL;
        uint32_t spec_count = 0;
        engine_fx_registry_get_param_specs(state->engine, fx->type_id, &specs, &spec_count);
        float values[FX_MAX_PARAMS];
        FxParamMode modes[FX_MAX_PARAMS];
        float beats[FX_MAX_PARAMS];
        uint32_t count = session_pending_fx_collect_params(fx, specs, spec_count, values, modes, beats);
        for (uint32_t p = 0; p < count; ++p) {
            FxParamMode mode = modes[p];
            float beat_value = beats[p];
            float native_value = values[p];
            const EffectParamSpec* spec = (specs && p < spec_count) ? &specs[p] : NULL;
            bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_spec_is_syncable(spec);
            if (use_sync) {
                native_value = fx_param_spec_beats_to_native(spec, beat_value, &state->tempo);
            }
            if (use_sync) {
                if (!engine_fx_master_set_param_with_mode(state->engine, id, p, native_value, mode, beat_value)) return false;
            } else {
                if (!engine_fx_master_set_param(state->engine, id, p, native_value)) return false;
            }
        }
        if (!fx->enabled) {
            if (!engine_fx_master_set_enabled(state->engine, id, false)) return false;
        }
    }
    state->pending_master_fx_dirty = false;
    return true;
}

bool session_apply_pending_track_fx(AppState* state) {
    if (!state || !state->engine) {
        return true;
    }
    if (!state->pending_track_fx_dirty || state->pending_track_fx_count <= 0 || !state->pending_track_fx) {
        state->pending_track_fx_dirty = false;
        return true;
    }

    if (!engine_fx_set_track_count(state->engine, engine_get_track_count(state->engine))) return false;
    int track_count = engine_get_track_count(state->engine);
    for (int t = 0; t < state->pending_track_fx_count && t < track_count; ++t) {
        const PendingTrackFxEntry* pending = &state->pending_track_fx[t];
        if (!pending || pending->fx_count <= 0) {
            continue;
        }
        for (int f = 0; f < pending->fx_count && f < FX_MASTER_MAX; ++f) {
            const SessionFxInstance* fx = &pending->fx[f];
            if (!fx || fx->type == 0) {
                return false;
            }
            FxInstId id = engine_fx_track_add(state->engine, t, fx->type);
            if (id == 0) {
                return false;
            }
            const EffectParamSpec* specs = NULL;
            uint32_t spec_count = 0;
            engine_fx_registry_get_param_specs(state->engine, fx->type, &specs, &spec_count);
            float values[FX_MAX_PARAMS];
            FxParamMode modes[FX_MAX_PARAMS];
            float beats[FX_MAX_PARAMS];
            uint32_t pcount = session_fx_collect_params(fx, specs, spec_count, values, modes, beats);
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
                    if (!engine_fx_track_set_param_with_mode(state->engine, t, id, p, native_value, mode, beat_value)) return false;
                } else {
                    if (!engine_fx_track_set_param(state->engine, t, id, p, native_value)) return false;
                }
            }
            if (!fx->enabled) {
                if (!engine_fx_track_set_enabled(state->engine, t, id, false)) return false;
            }
        }
    }
    state->pending_track_fx_dirty = false;
    return true;
}

// Restores inspector presentation only after every fallible project preparation step succeeds.
static void session_restore_inspector(AppState* state, const SessionDocument* doc) {
    int clip_index = session_restored_clip_index(doc, doc->clip_inspector.track_index, doc->clip_inspector.clip_index);
    if (doc->clip_inspector.visible &&
        doc->clip_inspector.track_index >= 0 &&
        doc->clip_inspector.track_index < engine_get_track_count(state->engine)) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        const EngineTrack* track = tracks ? &tracks[doc->clip_inspector.track_index] : NULL;
        if (track &&
            clip_index >= 0 &&
            clip_index < track->clip_count) {
            inspector_input_show(state,
                                 doc->clip_inspector.track_index,
                                 clip_index,
                                 &track->clips[clip_index]);
            state->inspector.waveform.view_source = doc->clip_inspector.view_source;
            state->inspector.waveform.zoom = doc->clip_inspector.zoom > 0.0f ? doc->clip_inspector.zoom : 1.0f;
            state->inspector.waveform.scroll = doc->clip_inspector.scroll;
        } else {
            inspector_input_init(state);
        }
    } else {
        inspector_input_init(state);
    }

}

// Releases resources privately owned by a candidate without persisting its registry or touching shared UI state.
static void session_discard_candidate(AppState* candidate) {
    if (!candidate) return;
    engine_destroy(candidate->engine);
    tempo_map_free(&candidate->tempo_map);
    time_signature_map_free(&candidate->time_signature_map);
    free(candidate->effects_panel.eq_curve_tracks);
    free(candidate->pending_track_fx);
    free(candidate->media_registry.entries);
    free(candidate);
}

// Replaces the project only after a complete offline engine, maps, automation, and effects are ready.
bool session_apply_document(AppState* state, const SessionDocument* doc) {
    if (!state || !doc || (state->engine && !engine_is_control_thread(state->engine)) ||
        daw_audio_recording_is_active(&state->audio_recording) || state->audio_recording.take_frame_count > 0 ||
        state->audio_recording.capture_device_open || state->audio_recording.capture_device_started ||
        state->audio_recording.record_armed_engine || state->bounce_active) return false;
    char error[256];
    if (!session_document_validate(doc, error, sizeof(error))) return false;
    AppState* candidate = malloc(sizeof(*candidate));
    if (!candidate) return false;
    *candidate = *state;
    candidate->engine = NULL;
    candidate->tempo_map = (TempoMap){0};
    candidate->time_signature_map = (TimeSignatureMap){0};
    candidate->pending_track_fx = NULL;
    candidate->pending_track_fx_count = 0;
    candidate->effects_panel.eq_curve_tracks = NULL;
    candidate->effects_panel.eq_curve_tracks_count = 0;
    candidate->media_registry.entries = NULL;
    candidate->media_registry.capacity = candidate->media_registry.count;
    if (candidate->media_registry.count > 0) {
        candidate->media_registry.entries = malloc((size_t)candidate->media_registry.count * sizeof(MediaRegistryEntry));
        if (!candidate->media_registry.entries) { session_discard_candidate(candidate); return false; }
        memcpy(candidate->media_registry.entries, state->media_registry.entries,
               (size_t)candidate->media_registry.count * sizeof(MediaRegistryEntry));
    }
    if (!session_require_operation(session_prepare_document(candidate, doc), "project preparation") ||
        !session_require_operation(session_apply_pending_master_fx(candidate), "master FX") ||
        !session_require_operation(session_apply_pending_track_fx(candidate), "track FX")) {
        session_discard_candidate(candidate);
        return false;
    }
    // No fallible project operation follows: retire old ownership, then publish the prepared state.
    daw_media_import_invalidate(state);
    engine_destroy(state->engine);
    tempo_map_free(&state->tempo_map);
    time_signature_map_free(&state->time_signature_map);
    free(state->effects_panel.eq_curve_tracks);
    free(state->effects_panel.last_open_track_fx_ids);
    candidate->effects_panel.last_open_track_fx_ids = NULL;
    candidate->effects_panel.last_open_track_fx_count = 0;
    free(state->pending_track_fx);
    free(state->media_registry.entries);
    undo_manager_clear(&state->undo);
    candidate->undo = state->undo;
    free(state->timeline_drag.initial_midi_notes);
    SDL_free(state->timeline_drag.ripple_targets);
    candidate->timeline_drag = (TimelineDragState){0};
    candidate->timeline_drag.track_index = candidate->timeline_drag.clip_index = -1;
    candidate->midi_editor_ui.drag_active = false;
    candidate->midi_editor_ui.qwerty_record_armed = false;
    candidate->midi_editor_ui.qwerty_test_enabled = false;
    memset(candidate->midi_editor_ui.qwerty_active_notes, 0, sizeof(candidate->midi_editor_ui.qwerty_active_notes));
    candidate->midi_editor_ui.note_press_pending = false;
    candidate->midi_editor_ui.shift_note_pending = false;
    candidate->track_name_editor.editing = false;
    candidate->dragging_library = false;
    candidate->timeline_marquee_active = false;
    candidate->effects_panel.target = FX_PANEL_TARGET_MASTER;
    candidate->effects_panel.target_track_index = -1;
    candidate->effects_panel.chain_count = 0;
    *state = *candidate;
    free(candidate);
    session_restore_inspector(state, doc);
    library_browser_scan(&state->library, &state->media_registry);
    if (doc->library.selected_index >= 0 && doc->library.selected_index < state->library.count)
        state->library.selected_index = doc->library.selected_index;
    return true;
}

bool session_load_from_file(AppState* state, const char* path) {
    if (!state) {
        SDL_Log("session_load_from_file: app state is null");
        return false;
    }
    SessionDocument doc;
    session_document_init(&doc);
    if (!session_document_read_recoverable(path, &doc, NULL)) {
        session_document_free(&doc);
        return false;
    }
    bool applied = session_apply_document(state, &doc);
    session_document_free(&doc);
    return applied;
}
