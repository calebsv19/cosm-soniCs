#include "session_restore_clip_ui.h"

#include "app_state.h"
#include "input/midi_editor_input.h"
#include "input/timeline_selection.h"

// Checks whether the restored primary clip can own MIDI editor controls.
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

// Restores saved MIDI editor preferences and clears transient instrument gestures.
static void session_apply_midi_editor_state(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return;
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_menu_scroll_row = 0;
    state->midi_editor_ui.instrument_menu_expanded_category = -1;
    state->midi_editor_ui.instrument_param_drag_active = false;
    state->midi_editor_ui.instrument_gesture = (DawInstrumentGesture){0};
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

// Maps a document clip identity to the engine's chronological, creation-order sorting.
int session_restored_clip_index(const SessionDocument* doc, int track_index, int clip_index) {
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

// Reads the creation identity of an already sorted engine clip.
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

// Restores valid timeline selection entries in saved order.
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

// Rebinds the independent time and pitch viewports to their rebuilt clips.
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

// Restores the prepared project's clip selection and MIDI view after engine sorting.
void session_restore_candidate_clip_ui(AppState* state,
                                       const SessionDocument* doc,
                                       bool selected_from_clip) {
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
}
