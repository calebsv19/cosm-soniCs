#include "daw/app_actions.h"
#include "app_state.h"
#include <math.h>

// Compares target identities without depending on padding or mutable array positions.
static bool same_target(DawActionTarget a, DawActionTarget b) {
    return a.project_generation == b.project_generation && a.track_id == b.track_id &&
           a.clip_id == b.clip_id && a.kind == b.kind;
}

// Compares exact authored scalar settings, including inactive overrides.
static bool same_instrument(DawInstrumentState a, DawInstrumentState b) {
    if (a.preset != b.preset || a.enabled != b.enabled || a.inherits_track != b.inherits_track) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        if (engine_instrument_params_get(a.params, (EngineInstrumentParamId)i) !=
            engine_instrument_params_get(b.params, (EngineInstrumentParamId)i)) return false;
    }
    return true;
}

// Constructs a bounded result with a static, human-readable reason.
static DawActionResult action_result(DawActionStatus status) {
    const char* messages[] = {
        "Instrument edit committed.", "Instrument settings unchanged.", "Invalid instrument request.",
        "Action requires the DAW control thread.", "Target no longer belongs to this project.",
        "Finish the current edit first.", "Edit not applied: undo history could not be reserved.",
        "Engine rejected instrument publication."
    };
    return (DawActionResult){.status = status, .message = messages[status]};
}

// Resolves generation-bound handles only after verifying control-thread ownership.
DawActionStatus daw_action_target_resolve(const AppState* state, DawActionTarget target, int* track, int* clip) {
    if (!track || !clip) return DAW_ACTION_INVALID;
    if (!state || !state->engine) return DAW_ACTION_STALE_TARGET;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    if (target.kind != DAW_ACTION_TARGET_TRACK && target.kind != DAW_ACTION_TARGET_MIDI_REGION) return DAW_ACTION_INVALID;
    if (!target.project_generation || target.project_generation != engine_instance_id(state->engine)) return DAW_ACTION_STALE_TARGET;
    if (!target.track_id || (target.kind == DAW_ACTION_TARGET_TRACK && target.clip_id != 0)) return DAW_ACTION_INVALID;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; t < engine_get_track_count(state->engine); ++t) {
        if (tracks[t].runtime_id != target.track_id) continue;
        *track = t;
        *clip = -1;
        if (target.kind == DAW_ACTION_TARGET_TRACK) return DAW_ACTION_UNCHANGED;
        for (int c = 0; c < tracks[t].clip_count; ++c) {
            if (tracks[t].clips[c].creation_index != target.clip_id) continue;
            if (tracks[t].clips[c].kind != ENGINE_CLIP_KIND_MIDI) return DAW_ACTION_INVALID;
            *clip = c;
            return DAW_ACTION_UNCHANGED;
        }
        return DAW_ACTION_STALE_TARGET;
    }
    return DAW_ACTION_STALE_TARGET;
}

// Captures handles for callers that already own an index-based UI selection.
DawActionStatus daw_instrument_target_capture(const AppState* state, int track_index,
                                             int clip_index, DawActionTarget* out) {
    if (!state || !state->engine || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    if (track_index < 0 || track_index >= engine_get_track_count(state->engine) || clip_index < -1) return DAW_ACTION_INVALID;
    const EngineTrack* track = &engine_get_tracks(state->engine)[track_index];
    if (clip_index >= track->clip_count || (clip_index >= 0 && track->clips[clip_index].kind != ENGINE_CLIP_KIND_MIDI)) return DAW_ACTION_INVALID;
    *out = (DawActionTarget){.project_generation = engine_instance_id(state->engine),
        .track_id = track->runtime_id, .clip_id = clip_index < 0 ? 0 : track->clips[clip_index].creation_index,
        .kind = clip_index < 0 ? DAW_ACTION_TARGET_TRACK : DAW_ACTION_TARGET_MIDI_REGION};
    return DAW_ACTION_UNCHANGED;
}

// Reads stored and effective instrument settings without exposing borrowed engine storage.
DawActionResult daw_action_get_instrument(const AppState* state, DawActionTarget target) {
    int t, c;
    DawActionStatus status = daw_action_target_resolve(state, target, &t, &c);
    DawActionResult result = action_result(status);
    if (status != DAW_ACTION_UNCHANGED) return result;
    const EngineTrack* track = &engine_get_tracks(state->engine)[t];
    result.has_snapshot = true;
    result.snapshot.target = target;
    if (c < 0) {
        result.snapshot.stored = (DawInstrumentState){.preset = track->midi_instrument_preset,
            .params = track->midi_instrument_params, .enabled = track->midi_instrument_enabled};
        result.snapshot.effective_preset = engine_track_midi_instrument_preset(state->engine, t);
        result.snapshot.effective_params = engine_track_midi_instrument_params(state->engine, t);
    } else {
        const EngineClip* clip = &track->clips[c];
        result.snapshot.stored = (DawInstrumentState){.preset = clip->instrument_preset,
            .params = clip->instrument_params, .inherits_track = clip->instrument_inherits_track};
        result.snapshot.effective_preset = engine_clip_midi_effective_instrument_preset(state->engine, t, c);
        result.snapshot.effective_params = engine_clip_midi_effective_instrument_params(state->engine, t, c);
    }
    return result;
}

// Restores exact instrument-only history while leaving notes, selection and other content untouched.
bool daw_instrument_restore(AppState* state, DawActionTarget target, const DawInstrumentState* value) {
    int t, c;
    if (!value || daw_action_target_resolve(state, target, &t, &c) != DAW_ACTION_UNCHANGED) return false;
    return c < 0 ? engine_track_midi_set_instrument_state(state->engine, t, value->preset, value->params, value->enabled)
                 : engine_clip_midi_set_instrument_state(state->engine, t, c, value->preset, value->params, value->inherits_track);
}

// Validates a partial patch and prepares all authored scalar fields before publication.
static bool prepare_instrument(const DawInstrumentSnapshot* snapshot, const DawInstrumentRequest* request,
                               DawInstrumentState* desired) {
    if (!request || (request->parameter_mask >> ENGINE_INSTRUMENT_PARAM_COUNT) != 0 ||
        (request->set_preset && (request->preset < 0 || request->preset >= ENGINE_INSTRUMENT_PRESET_COUNT))) return false;
    *desired = snapshot->stored;
    if (!request->set_preset && !request->parameter_mask) return true;
    if (request->set_preset) {
        if (desired->preset != request->preset || desired->inherits_track) {
            desired->preset = request->preset;
            desired->params = engine_instrument_default_params(request->preset);
        }
    } else if (desired->inherits_track) {
        desired->preset = snapshot->effective_preset;
        desired->params = snapshot->effective_params;
    }
    desired->inherits_track = false;
    desired->enabled = request->target.kind == DAW_ACTION_TARGET_TRACK;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        if (!(request->parameter_mask & (UINT32_C(1) << i))) continue;
        EngineInstrumentParamSpec spec;
        engine_instrument_param_spec((EngineInstrumentParamId)i, &spec);
        float value = engine_instrument_params_get(request->params, (EngineInstrumentParamId)i);
        if (!isfinite(value) || value < spec.min_value || value > spec.max_value) return false;
        desired->params = engine_instrument_params_set(desired->preset, desired->params, (EngineInstrumentParamId)i, value);
    }
    return true;
}

// Reserves scalar-only history before accepting a continuous edit.
DawActionResult daw_action_begin_instrument(AppState* state, DawActionTarget target, DawInstrumentGesture* gesture) {
    DawActionResult result = daw_action_get_instrument(state, target);
    if (!result.has_snapshot) return result;
    if (!gesture) return action_result(DAW_ACTION_INVALID);
    if (gesture->active || state->undo.active_drag_valid) return action_result(DAW_ACTION_BUSY);
    UndoCommand command = {.type = UNDO_CMD_INSTRUMENT_SETTINGS};
    command.data.instrument_settings.target = target;
    command.data.instrument_settings.before = result.snapshot.stored;
    command.data.instrument_settings.after = result.snapshot.stored;
    if (!undo_manager_begin_drag(&state->undo, &command)) return action_result(DAW_ACTION_HISTORY_UNAVAILABLE);
    *gesture = (DawInstrumentGesture){.target = target, .history_serial = state->undo.drag_serial, .active = true};
    return result;
}

// Checks serial and target ownership before touching another caller's reservation.
static bool owns_gesture(const AppState* state, const DawInstrumentGesture* gesture) {
    return gesture && gesture->active && state->undo.active_drag_valid &&
        gesture->history_serial == state->undo.drag_serial &&
        state->undo.active_drag.type == UNDO_CMD_INSTRUMENT_SETTINGS &&
        same_target(gesture->target, state->undo.active_drag.data.instrument_settings.target);
}

// Publishes one complete patch and updates only already-reserved scalar history.
DawActionResult daw_action_update_instrument(AppState* state, DawInstrumentGesture* gesture,
                                            const DawInstrumentRequest* request) {
    if (!gesture || !request || !same_target(gesture->target, request->target)) return action_result(DAW_ACTION_INVALID);
    DawActionResult result = daw_action_get_instrument(state, gesture->target);
    if (!result.has_snapshot) return result;
    if (!owns_gesture(state, gesture)) return action_result(DAW_ACTION_BUSY);
    DawInstrumentState desired;
    if (!prepare_instrument(&result.snapshot, request, &desired)) return action_result(DAW_ACTION_INVALID);
    if (same_instrument(desired, result.snapshot.stored)) return result;
    if (!daw_instrument_restore(state, gesture->target, &desired)) return action_result(DAW_ACTION_ENGINE_REJECTED);
    state->undo.active_drag.data.instrument_settings.after = desired;
    result = daw_action_get_instrument(state, gesture->target);
    result.status = DAW_ACTION_APPLIED;
    result.message = "Instrument preview accepted; history remains reserved.";
    return result;
}

// Transfers reserved history without allocation, or transactionally restores the original preview baseline.
DawActionResult daw_action_finish_instrument(AppState* state, DawInstrumentGesture* gesture, bool commit) {
    if (!gesture) return action_result(DAW_ACTION_INVALID);
    DawActionResult result = daw_action_get_instrument(state, gesture->target);
    if (result.status == DAW_ACTION_WRONG_THREAD || !state) return result;
    if (!owns_gesture(state, gesture)) {
        gesture->active = false;
        return action_result(DAW_ACTION_BUSY);
    }
    if (!result.has_snapshot) {
        undo_manager_cancel_drag(&state->undo);
        gesture->active = false;
        return result;
    }
    UndoCommand* command = &state->undo.active_drag;
    bool changed = !same_instrument(command->data.instrument_settings.before, command->data.instrument_settings.after);
    if (!commit && changed && !daw_instrument_restore(state, gesture->target, &command->data.instrument_settings.before))
        return action_result(DAW_ACTION_ENGINE_REJECTED);
    if (commit && changed) {
        if (!undo_manager_commit_drag(&state->undo, command)) return action_result(DAW_ACTION_HISTORY_UNAVAILABLE);
    } else {
        undo_manager_cancel_drag(&state->undo);
    }
    gesture->active = false;
    result = daw_action_get_instrument(state, gesture->target);
    result.status = changed ? DAW_ACTION_APPLIED : DAW_ACTION_UNCHANGED;
    result.message = commit ? (changed ? "Instrument edit committed." : "Instrument settings unchanged.") : "Instrument preview cancelled.";
    return result;
}

// Applies one discrete patch using the same reserved transaction as UI gestures.
DawActionResult daw_action_set_instrument(AppState* state, const DawInstrumentRequest* request) {
    if (!request) return action_result(DAW_ACTION_INVALID);
    DawActionResult result = daw_action_get_instrument(state, request->target);
    if (!result.has_snapshot) return result;
    if (state->undo.active_drag_valid) return action_result(DAW_ACTION_BUSY);
    DawInstrumentState desired;
    if (!prepare_instrument(&result.snapshot, request, &desired)) return action_result(DAW_ACTION_INVALID);
    if (same_instrument(desired, result.snapshot.stored)) return result;
    DawInstrumentGesture gesture = {0};
    result = daw_action_begin_instrument(state, request->target, &gesture);
    if (!gesture.active) return result;
    result = daw_action_update_instrument(state, &gesture, request);
    if (result.status != DAW_ACTION_APPLIED && result.status != DAW_ACTION_UNCHANGED) {
        undo_manager_cancel_drag(&state->undo);
        return result;
    }
    return daw_action_finish_instrument(state, &gesture, true);
}
