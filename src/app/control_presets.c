#include "control_bridge_internal.h"
#include "daw/preset_actions.h"
#include "app_state.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <string.h>
// Produces a bounded preset-specific domain reply for the common receipt envelope.
static cJSON* reply(bool ok, const char* code) {
    cJSON* j = cJSON_CreateObject(); cJSON_AddBoolToObject(j, "ok", ok); cJSON_AddStringToObject(j, "code", code); return j;
}
// Reads only string values after the outer dispatcher has checked allowed fields.
static const char* text(const cJSON* j, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(j, key); return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Exposes library failures without changing the project's accepted sound.
static cJSON* failure(DawPresetStatus status) {
    const char* codes[] = {"preset_ok", "invalid_preset", "preset_not_found", "preset_conflict", "preset_storage_failed", "busy", "unsupported_preset"};
    cJSON* j = reply(false, codes[status]); cJSON_AddStringToObject(j, "message", daw_preset_status_message(status)); return j;
}
// Serializes portable sound values without returning filesystem handles or pointers.
static cJSON* preset_json(const DawUserPreset* preset) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "id", preset->id); cJSON_AddStringToObject(j, "revision", preset->revision);
    cJSON_AddStringToObject(j, "name", preset->name); cJSON_AddNumberToObject(j, "synthesis_version", 1);
    cJSON_AddStringToObject(j, "base_voice", engine_instrument_preset_id_string(preset->base));
    cJSON_AddStringToObject(j, "category", engine_instrument_preset_category_display_name(engine_instrument_preset_category(preset->base)));
    cJSON* params = cJSON_AddObjectToObject(j, "parameters");
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec; engine_instrument_param_spec(i, &spec);
        cJSON_AddNumberToObject(params, spec.id, engine_instrument_params_get(preset->params, i));
    }
    return j;
}
// Matches bounded name/category searches consistently with the native library browser.
static bool matches(const char* value, const char* query) {
    if (!query || !query[0]) return true;
    size_t n = strlen(query);
    for (; *value; ++value) {
        size_t i = 0;
        while (i < n && value[i] && tolower((unsigned char)value[i]) == tolower((unsigned char)query[i])) ++i;
        if (i == n) return true;
    }
    return false;
}
// Executes preset actions exclusively on the owner thread after the outer write/state/receipt guards.
cJSON* daw_control_presets(DawControlBridge* bridge, AppState* state, const cJSON* request,
    const DawInstrumentSnapshot* captured, bool* changed) {
    const char* op = text(request, "op");
    const char *id = text(request, "preset_id"), *revision = text(request, "preset_revision");
    if (!strcmp(op, "list_presets")) {
        const char* search = text(request, "search");
        const cJSON* search_value = cJSON_GetObjectItemCaseSensitive(request, "search");
        const cJSON* offset_value = cJSON_GetObjectItemCaseSensitive(request, "offset");
        if ((search_value && (!search || strlen(search) > 95)) || (offset_value &&
            (!cJSON_IsNumber(offset_value) || offset_value->valuedouble < 0 || offset_value->valuedouble > 1000000 || floor(offset_value->valuedouble) != offset_value->valuedouble))) return reply(false, "invalid_request");
        int offset = offset_value ? offset_value->valueint : 0;
        DawPresetList list; DawPresetStatus status = daw_preset_library_list(&list);
        if (status != DAW_PRESET_OK) return failure(status);
        cJSON* result = reply(true, "preset_list"); cJSON* entries = cJSON_AddArrayToObject(result, "presets");
        int total = 0, added = 0;
        for (int i = 0; i < list.count; ++i) {
            DawUserPreset* p = &list.items[i];
            if (!matches(p->name, search) && !matches(engine_instrument_preset_category_display_name(engine_instrument_preset_category(p->base)), search)) continue;
            if (total++ >= offset && added < 32) { cJSON_AddItemToArray(entries, preset_json(p)); ++added; }
        }
        cJSON_AddNumberToObject(result, "total", total); cJSON_AddNumberToObject(result, "next_offset", offset + added < total ? offset + added : -1);
        cJSON_AddNumberToObject(result, "invalid_files", list.invalid_count); cJSON_AddBoolToObject(result, "truncated", list.truncated);
        cJSON_AddStringToObject(result, "library_path", list.root); return result;
    }
    if (!strcmp(op, "save_preset")) {
        const cJSON *id_value = cJSON_GetObjectItemCaseSensitive(request, "preset_id"), *rev_value = cJSON_GetObjectItemCaseSensitive(request, "preset_revision");
        if (!captured || !text(request, "name") || (id_value && !cJSON_IsNull(id_value) && !id) ||
            (rev_value && !cJSON_IsNull(rev_value) && !revision)) return reply(false, "invalid_request");
        DawPresetSaveResult saved = daw_action_save_preset(state, captured, text(request, "name"), id, revision);
        if (saved.status != DAW_PRESET_OK) return failure(saved.status);
        *changed = true; cJSON* result = reply(true, "preset_saved");
        cJSON_AddItemToObject(result, "preset", preset_json(&saved.preset));
        cJSON_AddBoolToObject(result, "durable", saved.durable); cJSON_AddBoolToObject(result, "project_unchanged", true); return result;
    }
    const cJSON* revision_value = cJSON_GetObjectItemCaseSensitive(request, "preset_revision");
    if ((revision_value && !cJSON_IsString(revision_value)) || !id || (!strcmp(op, "get_preset") ? false : !revision)) return reply(false, "invalid_request");
    DawUserPreset preset; DawPresetStatus status = daw_preset_library_get(id, revision, &preset);
    if (status != DAW_PRESET_OK) return failure(status);
    if (!strcmp(op, "get_preset")) { cJSON* j = reply(true, "preset_read"); cJSON_AddItemToObject(j, "preset", preset_json(&preset)); return j; }
    if (!strcmp(op, "apply_preset")) {
        if (!captured) return reply(false, "invalid_target");
        DawActionResult action = daw_action_apply_preset(state, captured->target, id, revision);
        *changed = action.status == DAW_ACTION_APPLIED;
        return daw_control_action_json(bridge, action);
    }
    if (!strcmp(op, "preview_preset")) {
        const char* mode = text(request, "mode");
        if (!mode || (strcmp(mode, "note") && strcmp(mode, "chord"))) return reply(false, "invalid_request");
        DawPresetPreview preview;
        status = daw_action_preview_preset(state, id, revision, !strcmp(mode, "chord"), &preview);
        if (status != DAW_PRESET_OK) return failure(status);
        cJSON* j = reply(true, "preset_preview"); cJSON_AddStringToObject(j, "path", preview.path);
        cJSON_AddStringToObject(j, "preset_revision", revision); cJSON_AddStringToObject(j, "mode", mode);
        cJSON_AddNumberToObject(j, "sample_rate", 48000); cJSON_AddNumberToObject(j, "channels", 2); cJSON_AddNumberToObject(j, "frames", 144000);
        cJSON_AddNumberToObject(j, "peak", preview.peak); cJSON_AddNumberToObject(j, "rms", preview.rms);
        cJSON_AddBoolToObject(j, "normalized", false); cJSON_AddBoolToObject(j, "durable", preview.durable); return j;
    }
    return reply(false, "unknown_operation");
}
