#include "daw/preset_actions.h"
#include "daw/sound_inspection.h"
#include "app_state.h"
#include "ui/preset_panel.h"
#include <string.h>
// Compares both authored and inherited sound state before saving a captured editor sound.
static bool snapshot_equal(const DawInstrumentSnapshot* a, const DawInstrumentSnapshot* b) {
    DawUserPreset stored = {.base = a->stored.preset, .params = a->stored.params};
    DawUserPreset effective = {.base = a->effective_preset, .params = a->effective_params};
    return a->stored.enabled == b->stored.enabled && a->stored.inherits_track == b->stored.inherits_track &&
        daw_preset_sound_equal(&stored, b->stored.preset, b->stored.params) &&
        daw_preset_sound_equal(&effective, b->effective_preset, b->effective_params);
}
// Publishes a reusable sound independently of project history after exact target-state validation.
DawPresetSaveResult daw_action_save_preset(AppState* state, const DawInstrumentSnapshot* captured,
    const char* name, const char* id, const char* revision) {
    DawPresetSaveResult result = {.status = DAW_PRESET_CONFLICT};
    if (!state || !captured) return result;
    DawActionResult current = daw_action_get_instrument(state, captured->target);
    if (!current.has_snapshot || !snapshot_equal(captured, &current.snapshot)) return result;
    if (state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording)) {
        result.status = DAW_PRESET_BUSY; return result;
    }
    result.status = daw_preset_library_save(name, captured->effective_preset, captured->effective_params,
        id, revision, &result.preset, &result.durable);
    if (result.status == DAW_PRESET_OK) {
        daw_preset_panel_refresh(state);
        daw_preset_panel_remember(state, captured->target, &result.preset);
    }
    return result;
}
// Reuses the instrument action's complete validation, publication and scalar undo reservation.
DawActionResult daw_action_apply_preset(AppState* state, DawActionTarget target, const char* id, const char* revision) {
    DawActionResult before = daw_action_get_instrument(state, target);
    if (!before.has_snapshot) return before;
    DawUserPreset preset; DawPresetStatus status = daw_preset_library_get(id, revision, &preset);
    if (status != DAW_PRESET_OK) return (DawActionResult){.status = DAW_ACTION_INVALID, .message = daw_preset_status_message(status)};
    DawInstrumentRequest request = {.target = target, .set_preset = true, .preset = preset.base,
        .params = preset.params, .parameter_mask = (UINT32_C(1) << ENGINE_INSTRUMENT_PARAM_COUNT) - 1};
    DawActionResult result = daw_action_set_instrument(state, &request);
    if (result.status <= DAW_ACTION_UNCHANGED) daw_preset_panel_remember(state, target, &preset);
    return result;
}
// Starts sample-counted notes, preserving timeline notes, transport and project undo history.
DawActionResult daw_action_audition_sound(AppState* state, DawActionTarget target, DawSoundSpec sound) {
    bool chord = sound.chord;
    DawActionResult result = daw_action_get_instrument(state, target);
    if (!result.has_snapshot) return result;
    if (!daw_sound_spec_valid(sound)) { result.status=DAW_ACTION_INVALID; result.message="Captured sound settings are invalid."; return result; }
    if (engine_transport_requested_playing(state->engine) || state->midi_editor_ui.qwerty_test_enabled ||
        state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording)) {
        result.status = DAW_ACTION_BUSY; result.message = "Stop playback and keyboard Test before auditioning."; return result;
    }
    int track = -1;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; t < engine_get_track_count(state->engine); ++t) if (tracks[t].runtime_id == target.track_id) track = t;
    engine_midi_audition_all_notes_off(state->engine);
    const uint8_t notes[] = {48, 55, 60, 64};
    int rate = state->runtime_cfg.sample_rate > 0 ? state->runtime_cfg.sample_rate : 48000;
    for (int i = 0; i < (chord ? 4 : 1); ++i) {
        if (!engine_midi_audition_note_preview(state->engine, track, sound.base,
            sound.params, chord ? notes[i] : 60, .5f, (uint64_t)rate * 3 / 2)) {
            engine_midi_audition_all_notes_off(state->engine);
            result.status = DAW_ACTION_ENGINE_REJECTED; result.message = "Audition queue rejected the preview."; return result;
        }
    }
    result.status = DAW_ACTION_UNCHANGED; result.message = chord ? "Auditioning C-major chord." : "Auditioning held C4.";
    return result;
}

// Auditions the current effective sound through the same finite comparison owner.
DawActionResult daw_action_audition_instrument(AppState* state, DawActionTarget target, bool chord) {
    DawActionResult result=daw_action_get_instrument(state,target);if(!result.has_snapshot)return result;
    return daw_action_audition_sound(state,target,(DawSoundSpec){result.snapshot.effective_preset,result.snapshot.effective_params,chord});
}
