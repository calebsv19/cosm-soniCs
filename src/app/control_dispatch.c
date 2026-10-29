#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/app_actions.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Produces a machine-readable rejection without claiming an edit was applied.
static cJSON* reply(bool ok, const char* code) {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ok);
    cJSON_AddStringToObject(result, "code", code);
    return result;
}

// Retrieves a case-sensitive string property without coercion.
static const char* string_at(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

// Rejects unknown and duplicate properties at each typed object boundary.
static bool fields_valid(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next) {
        bool found = false;
        for (int i = 0; allowed[i]; ++i) if (strcmp(item->string, allowed[i]) == 0) found = true;
        if (!found) return false;
        for (const cJSON* other = item->next; other; other = other->next)
            if (strcmp(item->string, other->string) == 0) return false;
    }
    return true;
}

// Encodes identities as decimal strings to preserve all 64 bits in MCP clients.
static void add_id(cJSON* object, const char* key, uint64_t value) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    cJSON_AddStringToObject(object, key, text);
}

// Parses a canonical unsigned decimal identity with explicit overflow checking.
static bool parse_id(const cJSON* object, const char* key, uint64_t* value) {
    const char* text = string_at(object, key);
    if (!text || !text[0] || strlen(text) > 20 || (text[0] == '0' && text[1])) return false;
    for (const char* p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char* end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || *end) return false;
    *value = (uint64_t)parsed;
    return true;
}

// Copies a generation-bound target from a strict wire object.
static bool parse_target(const cJSON* object, DawActionTarget* target) {
    static const char* const fields[] = {"generation", "track_id", "clip_id", "kind", NULL};
    const char* kind = string_at(object, "kind");
    if (!fields_valid(object, fields) || !kind) return false;
    if (strcmp(kind, "track") == 0) target->kind = DAW_ACTION_TARGET_TRACK;
    else if (strcmp(kind, "midi_region") == 0) target->kind = DAW_ACTION_TARGET_MIDI_REGION;
    else return false;
    return parse_id(object, "generation", &target->project_generation) &&
        parse_id(object, "track_id", &target->track_id) && parse_id(object, "clip_id", &target->clip_id);
}

// Serializes an opaque target without publishing mutable array positions as handles.
static cJSON* target_json(DawActionTarget target) {
    cJSON* object = cJSON_CreateObject();
    add_id(object, "generation", target.project_generation);
    add_id(object, "track_id", target.track_id);
    add_id(object, "clip_id", target.clip_id);
    cJSON_AddStringToObject(object, "kind", target.kind == DAW_ACTION_TARGET_TRACK ? "track" : "midi_region");
    return object;
}

// Copies all instrument parameters in stable specification order.
static cJSON* params_json(EngineInstrumentParams params) {
    cJSON* object = cJSON_CreateObject();
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec;
        engine_instrument_param_spec((EngineInstrumentParamId)i, &spec);
        cJSON_AddNumberToObject(object, spec.id, engine_instrument_params_get(params, (EngineInstrumentParamId)i));
    }
    return object;
}

// Copies stored and effective settings for readback and an exact state precondition.
static cJSON* snapshot_json(DawControlBridge* bridge, DawInstrumentSnapshot snapshot) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "endpoint_instance", bridge->instance);
    cJSON_AddItemToObject(object, "target", target_json(snapshot.target));
    cJSON_AddStringToObject(object, "preset", engine_instrument_preset_id_string(snapshot.stored.preset));
    cJSON_AddItemToObject(object, "parameters", params_json(snapshot.stored.params));
    cJSON_AddBoolToObject(object, "enabled", snapshot.stored.enabled);
    cJSON_AddBoolToObject(object, "inherits_track", snapshot.stored.inherits_track);
    cJSON_AddStringToObject(object, "effective_preset", engine_instrument_preset_id_string(snapshot.effective_preset));
    cJSON_AddItemToObject(object, "effective_parameters", params_json(snapshot.effective_params));
    return object;
}

// Adds copied state and a canonical state token; this token is not a project-wide revision.
static void add_snapshot(DawControlBridge* bridge, cJSON* result, DawInstrumentSnapshot snapshot) {
    cJSON* object = snapshot_json(bridge, snapshot);
    char* version = cJSON_PrintUnformatted(object);
    if (version) cJSON_AddStringToObject(result, "expected_state", version);
    cJSON_AddItemToObject(result, "instrument", object);
    free(version);
}

// Maps the common application result to a stable wire outcome.
static cJSON* action_json(DawControlBridge* bridge, DawActionResult action) {
    static const char* const codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target",
        "busy", "history_unavailable", "engine_rejected"};
    cJSON* result = reply(action.status <= DAW_ACTION_UNCHANGED, codes[action.status]);
    cJSON_AddStringToObject(result, "message", action.message);
    if (action.has_snapshot) add_snapshot(bridge, result, action.snapshot);
    return result;
}

// Advertises actual presets, ranges and preset-specific visible controls from the engine registry.
static cJSON* capabilities(DawControlBridge* bridge) {
    cJSON* result = reply(true, "capabilities");
    cJSON_AddBoolToObject(result, "writes_enabled", bridge->allow_write);
    cJSON_AddStringToObject(result, "precondition_scope", "exact target instrument state including effective inheritance; not project revision");
    cJSON_AddNumberToObject(result, "receipt_capacity", DAW_CONTROL_RECEIPTS);
    cJSON_AddNumberToObject(result, "receipts_used", bridge->receipt_count);
    cJSON* presets = cJSON_AddArrayToObject(result, "presets");
    for (int i = 0; i < engine_instrument_preset_count(); ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        cJSON* object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "id", engine_instrument_preset_id_string(preset));
        cJSON_AddStringToObject(object, "name", engine_instrument_preset_display_name(preset));
        cJSON_AddItemToObject(object, "defaults", params_json(engine_instrument_default_params(preset)));
        cJSON* visible = cJSON_AddArrayToObject(object, "visible_parameters");
        for (int j = 0; j < engine_instrument_preset_param_count(preset); ++j) {
            EngineInstrumentParamId id;
            EngineInstrumentParamSpec spec;
            engine_instrument_preset_param_id_at(preset, j, &id);
            engine_instrument_param_spec(id, &spec);
            cJSON_AddItemToArray(visible, cJSON_CreateString(spec.id));
        }
        cJSON_AddItemToArray(presets, object);
    }
    cJSON* parameters = cJSON_AddArrayToObject(result, "parameters");
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec;
        engine_instrument_param_spec((EngineInstrumentParamId)i, &spec);
        cJSON* object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "id", spec.id);
        cJSON_AddStringToObject(object, "name", spec.display_name);
        cJSON_AddStringToObject(object, "unit", spec.unit ? spec.unit : "");
        cJSON_AddNumberToObject(object, "minimum", spec.min_value);
        cJSON_AddNumberToObject(object, "maximum", spec.max_value);
        cJSON_AddStringToObject(object, "control_scale", spec.scale == ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME ? "log1p_ms" : "linear");
        cJSON_AddItemToArray(parameters, object);
    }
    return result;
}

// Lists at most 64 instrument targets per page with copied names and generation-bound handles.
static cJSON* project_json(AppState* state, const cJSON* request) {
    const cJSON* page = cJSON_GetObjectItemCaseSensitive(request, "offset");
    if (page && (!cJSON_IsNumber(page) || !isfinite(page->valuedouble) || page->valuedouble < 0 ||
        page->valuedouble > 1000000 || floor(page->valuedouble) != page->valuedouble)) return reply(false, "invalid_offset");
    int offset = page ? page->valueint : 0;
    cJSON* result = reply(true, "project");
    add_id(result, "generation", engine_instance_id(state->engine));
    cJSON* targets = cJSON_AddArrayToObject(result, "targets");
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int count = 0, emitted = 0;
    for (int t = 0; t < engine_get_track_count(state->engine); ++t) {
        for (int c = -1; c < tracks[t].clip_count; ++c) {
            if (c >= 0 && tracks[t].clips[c].kind != ENGINE_CLIP_KIND_MIDI) continue;
            int index = count++;
            if (index < offset || emitted >= 64) continue;
            DawActionTarget target;
            if (daw_instrument_target_capture(state, t, c, &target) != DAW_ACTION_UNCHANGED) continue;
            cJSON* object = target_json(target);
            cJSON_AddStringToObject(object, "name", c < 0 ? tracks[t].name : tracks[t].clips[c].name);
            cJSON_AddItemToArray(targets, object);
            ++emitted;
        }
    }
    cJSON_AddNumberToObject(result, "total", count);
    cJSON_AddNumberToObject(result, "next_offset", offset + emitted < count ? offset + emitted : -1);
    return result;
}

// Validates raw floating values before the engine's sanitizing helper can clamp them.
static bool parse_patch(const cJSON* request, DawInstrumentRequest* patch) {
    const cJSON* preset = cJSON_GetObjectItemCaseSensitive(request, "preset");
    if (preset && !cJSON_IsNull(preset)) {
        if (!cJSON_IsString(preset) || !engine_instrument_preset_from_id_string(preset->valuestring, &patch->preset)) return false;
        patch->set_preset = true;
    }
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(request, "parameters");
    if (!params || !cJSON_IsObject(params)) return false;
    for (const cJSON* item = params->child; item; item = item->next) {
        EngineInstrumentParamId id;
        EngineInstrumentParamSpec spec;
        if (!cJSON_IsNumber(item) || !engine_instrument_param_from_id_string(item->string, &id)) return false;
        engine_instrument_param_spec(id, &spec);
        if (!isfinite(item->valuedouble) || item->valuedouble < spec.min_value || item->valuedouble > spec.max_value ||
            (patch->parameter_mask & (UINT32_C(1) << id))) return false;
        patch->parameter_mask |= UINT32_C(1) << id;
        patch->params = engine_instrument_params_set(ENGINE_INSTRUMENT_PRESET_SYNTH_LAB, patch->params, id, (float)item->valuedouble);
    }
    return true;
}

// Applies a guarded instrument patch through the existing reserved application action.
static cJSON* set_instrument(DawControlBridge* bridge, AppState* state, const cJSON* request, bool* changed) {
    DawInstrumentRequest patch = {0};
    if (!parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &patch.target) || !parse_patch(request, &patch))
        return reply(false, "invalid_request");
    DawActionResult before = daw_action_get_instrument(state, patch.target);
    if (!before.has_snapshot) return action_json(bridge, before);
    const char* expected = string_at(request, "expected_state");
    cJSON* object = snapshot_json(bridge, before.snapshot);
    char* current = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    bool match = expected && current && strcmp(expected, current) == 0;
    free(current);
    if (!match) return reply(false, "state_conflict");
    if (state->bounce_active || daw_audio_recording_is_active(&state->audio_recording)) return reply(false, "busy");
    DawActionResult result = daw_action_set_instrument(state, &patch);
    *changed = result.status == DAW_ACTION_APPLIED;
    return action_json(bridge, result);
}

// Reverses only a matching instrument command at the history cursor after checking current state.
static cJSON* instrument_history(DawControlBridge* bridge, AppState* state, const cJSON* request, bool redo, bool* changed) {
    DawActionTarget target = {0};
    if (!parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)) return reply(false, "invalid_target");
    DawActionResult before = daw_action_get_instrument(state, target);
    if (!before.has_snapshot) return action_json(bridge, before);
    cJSON* object = snapshot_json(bridge, before.snapshot);
    char* current = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    const char* expected = string_at(request, "expected_state");
    bool match = current && expected && strcmp(current, expected) == 0;
    free(current);
    if (!match) return reply(false, "state_conflict");
    if (state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording))
        return reply(false, "busy");
    int count = redo ? state->undo.redo_count : state->undo.undo_count;
    UndoCommand* commands = redo ? state->undo.redo_stack : state->undo.undo_stack;
    if (!count || commands[count - 1].type != UNDO_CMD_INSTRUMENT_SETTINGS) return reply(false, "history_conflict");
    DawActionTarget last = commands[count - 1].data.instrument_settings.target;
    if (last.project_generation != target.project_generation || last.track_id != target.track_id ||
        last.clip_id != target.clip_id || last.kind != target.kind) return reply(false, "history_conflict");
    bool accepted = redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state);
    if (!accepted) return reply(false, "engine_rejected");
    *changed = true;
    DawActionResult after = daw_action_get_instrument(state, target);
    after.status = DAW_ACTION_APPLIED;
    return action_json(bridge, after);
}

// Routes strict requests and reserves immutable receipt storage before any mutation.
char* daw_control_dispatch(DawControlBridge* bridge, AppState* state, const char* raw, bool* changed) {
    *changed = false;
    static const char* const fields[] = {"op", "offset", "target", "instance", "request_id", "expected_state", "preset", "parameters", NULL};
    const char* end = NULL;
    cJSON* request = cJSON_ParseWithOpts(raw, &end, true);
    cJSON* result = NULL;
    DawControlReceipt* receipt = NULL;
    if (!request || !fields_valid(request, fields)) { result = reply(false, "invalid_request"); goto done; }
    const char* op = string_at(request, "op");
    if (!op) { result = reply(false, "invalid_request"); goto done; }
    static const char* const read_fields[] = {"op", NULL};
    static const char* const project_fields[] = {"op", "offset", NULL};
    static const char* const get_fields[] = {"op", "target", NULL};
    static const char* const status_fields[] = {"op", "instance", "request_id", NULL};
    static const char* const history_fields[] = {"op", "instance", "request_id", "target", "expected_state", NULL};
    const char* const* operation_fields = fields;
    if (strcmp(op, "capabilities") == 0) operation_fields = read_fields;
    else if (strcmp(op, "project") == 0) operation_fields = project_fields;
    else if (strcmp(op, "get_instrument") == 0) operation_fields = get_fields;
    else if (strcmp(op, "request_status") == 0) operation_fields = status_fields;
    else if (strcmp(op, "undo_instrument") == 0 || strcmp(op, "redo_instrument") == 0) operation_fields = history_fields;
    if (!fields_valid(request, operation_fields)) { result = reply(false, "invalid_request"); goto done; }
    if (strcmp(op, "capabilities") == 0) { result = capabilities(bridge); goto done; }
    if (strcmp(op, "project") == 0) { result = project_json(state, request); goto done; }
    if (strcmp(op, "get_instrument") == 0) {
        DawActionTarget target = {0};
        result = parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)
            ? action_json(bridge, daw_action_get_instrument(state, target)) : reply(false, "invalid_target");
        goto done;
    }
    const char* instance = string_at(request, "instance");
    if (!instance || strcmp(instance, bridge->instance) != 0) { result = reply(false, "stale_instance"); goto done; }
    const char* id = string_at(request, "request_id");
    if (!id || !id[0] || strlen(id) > 64) { result = reply(false, "invalid_request_id"); goto done; }
    for (int i = 0; i < bridge->receipt_count; ++i) {
        if (strcmp(id, bridge->receipts[i].id) != 0) continue;
        if (strcmp(op, "request_status") == 0 || strcmp(raw, bridge->receipts[i].request) == 0) {
            char* replay = strdup(bridge->receipts[i].response);
            cJSON_Delete(request);
            return replay;
        }
        result = reply(false, "request_id_conflict");
        goto done;
    }
    if (strcmp(op, "request_status") == 0) { result = reply(false, "unknown_request"); goto done; }
    if (strcmp(op, "set_instrument") != 0 && strcmp(op, "undo_instrument") != 0 && strcmp(op, "redo_instrument") != 0) { result = reply(false, "unknown_operation"); goto done; }
    if (!bridge->allow_write) { result = reply(false, "read_only"); goto done; }
    if (bridge->receipt_count >= DAW_CONTROL_RECEIPTS) { result = reply(false, "receipt_capacity_reached"); goto done; }
    receipt = &bridge->receipts[bridge->receipt_count];
    receipt->request = strdup(raw);
    receipt->response = malloc(DAW_CONTROL_MAX_RESPONSE);
    if (!receipt->request || !receipt->response) {
        free(receipt->request); free(receipt->response);
        memset(receipt, 0, sizeof(*receipt));
        receipt = NULL;
        result = reply(false, "allocation_failed");
        goto done;
    }
    strcpy(receipt->id, id);
    strcpy(receipt->response, "{\"ok\":false,\"code\":\"result_unavailable\",\"may_have_applied\":true}");
    ++bridge->receipt_count;
    result = strcmp(op, "set_instrument") == 0 ? set_instrument(bridge, state, request, changed)
        : instrument_history(bridge, state, request, strcmp(op, "redo_instrument") == 0, changed);
done:
    cJSON_Delete(request);
    if (result) cJSON_AddStringToObject(result, "instance", bridge->instance);
    if (result && receipt) cJSON_AddStringToObject(result, "request_id", receipt->id);
    char* response = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (receipt) {
        if (response && strlen(response) < DAW_CONTROL_MAX_RESPONSE) strcpy(receipt->response, response);
        free(response);
        return strdup(receipt->response);
    }
    return response;
}
