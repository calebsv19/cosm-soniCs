#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/fx_actions.h"
#include "daw/fx_library.h"
#include <stdlib.h>
#include "effects/param_utils.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Returns a domain outcome separately from MCP transport success.
static cJSON* reply(bool ok, const char* code) {
    cJSON* r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", ok); cJSON_AddStringToObject(r, "code", code); return r;
}
// Reads a string without coercing other JSON types.
static const char* text(const cJSON* r, const char* k) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(r, k); return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Reads bounded unsigned numbers without accepting Boolean or fractional values.
static bool number(const cJSON* r, const char* k, uint32_t* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(r, k);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 || v->valuedouble > UINT32_MAX || floor(v->valuedouble) != v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble; return true;
}
// Copies exact chain state into a bounded endpoint token cache.
static DawControlFxState* remember(DawControlBridge* b, const DawFxSnapshot* snapshot) {
    for (int i = 0; i < DAW_CONTROL_FX_STATES; ++i) if (b->fx_states[i].token[0] && daw_fx_snapshot_equal(&b->fx_states[i].snapshot, snapshot)) return &b->fx_states[i];
    if (b->fx_sequence == UINT64_MAX) return NULL;
    DawControlFxState* s = &b->fx_states[b->fx_cursor++ % DAW_CONTROL_FX_STATES]; s->snapshot = *snapshot;
    snprintf(s->token, sizeof(s->token), "%s:fx:%" PRIu64, b->instance, ++b->fx_sequence); return s;
}
// Serializes complete active insert settings for chain reads and captured render provenance.
void daw_control_fx_values_json(cJSON* r, const FxMasterSnapshot* chain) {
    cJSON* effects = cJSON_AddArrayToObject(r, "effects");
    for (int i = 0; i < chain->count; ++i) {
        const FxMasterInstanceInfo* item = &chain->items[i]; cJSON* e = cJSON_CreateObject(); cJSON_AddItemToArray(effects, e);
        cJSON_AddNumberToObject(e, "effect_id", item->id); cJSON_AddNumberToObject(e, "type_id", item->type); cJSON_AddBoolToObject(e, "enabled", item->enabled);
        cJSON* parameters = cJSON_AddArrayToObject(e, "parameters");
        for (uint32_t p = 0; p < item->param_count; ++p) {
            cJSON* v = cJSON_CreateObject(); cJSON_AddItemToArray(parameters, v);
            cJSON_AddNumberToObject(v, "index", p); cJSON_AddNumberToObject(v, "value", item->params[p]);
            cJSON_AddStringToObject(v, "mode", item->param_mode[p] == FX_PARAM_MODE_NATIVE ? "native" : item->param_mode[p] == FX_PARAM_MODE_BEATS ? "beats" : "beat_rate");
            cJSON_AddNumberToObject(v, "beats", item->param_beats[p]);
        }
    }
}
// Serializes every active effect setting and parameter mode in chain order.
static void chain_json(cJSON* r, const DawControlFxState* s) {
    cJSON_AddStringToObject(r, "expected_state", s->token);
    char id[32]; snprintf(id, sizeof(id), "%" PRIu64, s->snapshot.generation); cJSON_AddStringToObject(r, "generation", id);
    snprintf(id, sizeof(id), "%" PRIu64, s->snapshot.track_id); cJSON_AddStringToObject(r, "track_id", id);
    daw_control_fx_values_json(r, &s->snapshot.chain);
}
// Pages copied registry metadata without exposing borrowed pointers to the transport thread.
static cJSON* catalog(AppState* state, const cJSON* request) {
    uint32_t offset = 0; int count = 0; const FxRegistryEntry* entries = NULL;
    if (cJSON_GetObjectItemCaseSensitive(request, "offset") && !number(request, "offset", &offset)) return reply(false, "invalid_offset");
    if (!engine_fx_get_registry(state->engine, &entries, &count) || offset > (uint32_t)count) return reply(false, "invalid_offset");
    cJSON* r = reply(true, "effect_catalog"); cJSON* effects = cJSON_AddArrayToObject(r, "effects");
    uint32_t end = offset + 8 < (uint32_t)count ? offset + 8 : (uint32_t)count;
    cJSON_AddNumberToObject(r, "count", count); cJSON_AddNumberToObject(r, "next_offset", end < (uint32_t)count ? (int)end : -1);
    for (uint32_t i = offset; i < end; ++i) {
        cJSON* e = cJSON_CreateObject(); cJSON_AddItemToArray(effects, e);
        cJSON_AddNumberToObject(e, "type_id", entries[i].id); cJSON_AddStringToObject(e, "name", entries[i].name);
        cJSON* parameters = cJSON_AddArrayToObject(e, "parameters");
        for (uint32_t p = 0; p < entries[i].param_spec_count; ++p) {
            const EffectParamSpec* spec = &entries[i].param_specs[p]; cJSON* v = cJSON_CreateObject(); cJSON_AddItemToArray(parameters, v);
            cJSON_AddNumberToObject(v, "index", p); cJSON_AddStringToObject(v, "id", spec->id); cJSON_AddStringToObject(v, "name", spec->display_name);
            static const char* units[] = {"generic", "dB", "Hz", "ms", "seconds", "percent", "ratio"};
            static const char* types[] = {"float", "integer", "boolean", "enum"};
            cJSON_AddStringToObject(v, "unit", units[spec->unit]); cJSON_AddStringToObject(v, "type", types[spec->type]);
            cJSON_AddNumberToObject(v, "minimum", spec->min_value); cJSON_AddNumberToObject(v, "maximum", spec->max_value); cJSON_AddNumberToObject(v, "default", spec->default_value);
            cJSON_AddBoolToObject(v, "tempo_sync", (spec->flags & FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE) != 0);
            cJSON_AddBoolToObject(v, "read_only", (spec->flags & FX_PARAM_FLAG_READ_ONLY_METER) != 0);
            cJSON* labels = cJSON_AddArrayToObject(v, "enum_labels");
            for (uint32_t l = 0; l < spec->enum_count && l < 8; ++l) cJSON_AddItemToArray(labels, cJSON_CreateString(spec->enum_labels[l]));
        }
    }
    return r;
}
// Encodes portable library settings separately from runtime chain identities.
static void preset_json(cJSON* r,const DawFxPreset* preset) {
    cJSON_AddStringToObject(r,"preset_id",preset->id);cJSON_AddStringToObject(r,"preset_revision",preset->revision);cJSON_AddStringToObject(r,"name",preset->name);cJSON_AddNumberToObject(r,"effects_version",1);
    daw_control_fx_values_json(r,&preset->chain);
    cJSON* effects=cJSON_GetObjectItemCaseSensitive(r,"effects");for(cJSON* effect=effects->child;effect;effect=effect->next)cJSON_DeleteItemFromObjectCaseSensitive(effect,"effect_id");
}
// Maps typed storage refusals to explicit protocol outcomes without claiming project mutation.
static cJSON* library_error(DawPresetStatus status) {
    const char* codes[]={"preset","invalid_request","preset_not_found","preset_revision_conflict","io_error","library_busy","unsupported_preset"};return reply(false,codes[status]);
}
// Returns bounded sorted metadata pages or one complete compatible portable document.
static cJSON* library_read(AppState* state,const cJSON* request) {
    const char* op=text(request,"op");
    if(!strcmp(op,"get_effect_preset")){
        const char* id=text(request,"preset_id"),*revision=text(request,"preset_revision");
        if(cJSON_GetObjectItemCaseSensitive(request,"preset_revision") && !revision)return reply(false,"invalid_request");
        DawFxPreset preset;DawPresetStatus status=daw_fx_library_get(state->engine,id,revision,&preset);if(status!=DAW_PRESET_OK)return library_error(status);
        cJSON* r=reply(true,"effect_preset");preset_json(r,&preset);return r;
    }
    uint32_t offset=0;const char* search=text(request,"search");if((cJSON_GetObjectItemCaseSensitive(request,"offset") && !number(request,"offset",&offset)) || (cJSON_GetObjectItemCaseSensitive(request,"search") && (!search || strlen(search)>95)))return reply(false,"invalid_request");
    DawFxPresetList* list=calloc(1,sizeof(*list));if(!list)return reply(false,"allocation_failed");DawPresetStatus status=daw_fx_library_list(state->engine,list);if(status!=DAW_PRESET_OK){free(list);return library_error(status);}
    int total=0;for(int i=0;i<list->count;++i)if(!search || !*search || strstr(list->items[i].name,search))++total;
    if(offset>(uint32_t)total){free(list);return reply(false,"invalid_offset");}cJSON* r=reply(true,"effect_presets"),*presets=cJSON_AddArrayToObject(r,"presets");int matched=0,shown=0;
    for(int i=0;i<list->count;++i){const DawFxPreset* preset=&list->items[i];if(search && *search && !strstr(preset->name,search))continue;if(matched++<(int)offset || shown>=8)continue;
        cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(presets,item);cJSON_AddStringToObject(item,"preset_id",preset->id);cJSON_AddStringToObject(item,"preset_revision",preset->revision);cJSON_AddStringToObject(item,"name",preset->name);cJSON_AddNumberToObject(item,"effect_count",preset->chain.count);++shown;
    }
    cJSON_AddNumberToObject(r,"count",total);cJSON_AddNumberToObject(r,"next_offset",offset+shown<(uint32_t)total?(int)offset+shown:-1);cJSON_AddNumberToObject(r,"invalid_count",list->invalid_count);cJSON_AddBoolToObject(r,"truncated",list->truncated);free(list);return r;
}
// Adapts receipt-admitted chain edits and guarded history to common owner-thread actions.
cJSON* daw_control_fx(DawControlBridge* bridge, AppState* state, const cJSON* request, const DawActionTarget* target, bool* changed) {
    const char* op = text(request, "op");
    if (!strcmp(op, "list_effects")) return catalog(state, request);
    if(!strcmp(op,"list_effect_presets") || !strcmp(op,"get_effect_preset"))return library_read(state,request);
    if (target && target->kind != DAW_ACTION_TARGET_TRACK) return reply(false, "invalid_target");
    DawFxSnapshot current;
    DawActionStatus got = daw_action_get_fx(state, target ? target->project_generation : engine_instance_id(state->engine), target ? target->track_id : 0, &current);
    if (got != DAW_ACTION_UNCHANGED) return reply(false, got == DAW_ACTION_STALE_TARGET ? "stale_target" : "engine_rejected");
    if (!strcmp(op, "get_effect_chain")) {
        DawControlFxState* entry = remember(bridge, &current); cJSON* r = reply(entry != NULL, entry ? "effect_chain" : "state_capacity");
        if (entry) chain_json(r, entry); return r;
    }
    const char* token = text(request, "expected_state"); DawControlFxState* entry = NULL;
    for (int i = 0; token && i < DAW_CONTROL_FX_STATES; ++i) if (!strcmp(token, bridge->fx_states[i].token)) entry = &bridge->fx_states[i];
    if (!entry || !daw_fx_snapshot_equal(&entry->snapshot, &current)) return reply(false, "state_conflict");
    if(!strcmp(op,"save_effect_preset") || !strcmp(op,"apply_effect_preset")){
        const char* id=text(request,"preset_id"),*revision=text(request,"preset_revision");
        if((cJSON_GetObjectItemCaseSensitive(request,"preset_id") && !id) || (cJSON_GetObjectItemCaseSensitive(request,"preset_revision") && !revision))return reply(false,"invalid_request");
        bool save=!strcmp(op,"save_effect_preset");DawFxPresetResult outcome=save?daw_action_save_fx_preset(state,&current,text(request,"name"),id,revision):daw_action_apply_fx_preset(state,&current,id,revision);
        if(outcome.action>DAW_ACTION_UNCHANGED){const char* codes[]={"applied","unchanged","invalid_request","wrong_thread","state_conflict","busy","history_unavailable","engine_rejected"};return reply(false,codes[outcome.action]);}
        if(outcome.library!=DAW_PRESET_OK)return library_error(outcome.library);
        *changed=outcome.action==DAW_ACTION_APPLIED;
        cJSON* r=reply(true,save?(outcome.durable?"effect_preset_saved":"effect_preset_saved_sync_uncertain"):*changed?"applied":"unchanged");
        cJSON* preset=cJSON_AddObjectToObject(r,"preset");preset_json(preset,&outcome.preset);if(save)cJSON_AddBoolToObject(r,"durable",outcome.durable);
        entry=remember(bridge,&outcome.snapshot);if(entry)chain_json(r,entry);else cJSON_AddBoolToObject(r,"readback_required",true);return r;
    }
    if (strcmp(op, "edit_effect_chain")) {
        bool redo = !strcmp(op, "redo_effect_chain"); int count = redo ? state->undo.redo_count : state->undo.undo_count;
        UndoCommand* commands = redo ? state->undo.redo_stack : state->undo.undo_stack;
        if (!count || commands[count - 1].type != UNDO_CMD_FX_CHAIN) return reply(false, "history_conflict");
        const DawFxSnapshot* expected = redo ? &commands[count - 1].data.fx_chain->before : &commands[count - 1].data.fx_chain->after;
        if (!daw_fx_snapshot_equal(expected, &current)) return reply(false, "history_conflict");
        if (daw_midi_actions_busy(state)) return reply(false, "busy");
        *changed = redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state);
        if (!*changed) return reply(false, "history_conflict");
        daw_action_get_fx(state, current.generation, current.track_id, &current);
    } else {
        const char* action = text(request, "action"); EngineFxChainRequest edit = {0};
        if (!action) return reply(false, "invalid_request");
        for (const cJSON* field = request->child; field; field = field->next) {
            const char* k = field->string;
            bool common = !strcmp(k, "op") || !strcmp(k, "instance") || !strcmp(k, "request_id") || !strcmp(k, "target") || !strcmp(k, "expected_state") || !strcmp(k, "action");
            bool allowed = (!strcmp(action, "add") && !strcmp(k, "type_id")) ||
                           (strcmp(action, "add") && !strcmp(k, "effect_id")) ||
                           (!strcmp(action, "reorder") && !strcmp(k, "position")) ||
                           (!strcmp(action, "enable") && !strcmp(k, "enabled")) ||
                           (!strcmp(action, "parameter") && (!strcmp(k, "parameter") || !strcmp(k, "value") || !strcmp(k, "mode") || !strcmp(k, "beats")));
            if (!common && !allowed) return reply(false, "invalid_request");
        }
        if (!strcmp(action, "add")) { edit.op = ENGINE_FX_CHAIN_ADD; if (!number(request, "type_id", &edit.type)) return reply(false, "invalid_request"); }
        else {
            if (!number(request, "effect_id", &edit.id)) return reply(false, "invalid_request");
            if (!strcmp(action, "remove")) edit.op = ENGINE_FX_CHAIN_REMOVE;
            else if (!strcmp(action, "reorder")) { uint32_t p; edit.op = ENGINE_FX_CHAIN_REORDER; if (!number(request, "position", &p) || p >= FX_MASTER_MAX) return reply(false, "invalid_request"); edit.position = (int)p; }
            else if (!strcmp(action, "enable")) { edit.op = ENGINE_FX_CHAIN_ENABLE; const cJSON* v = cJSON_GetObjectItemCaseSensitive(request, "enabled"); if (!cJSON_IsBool(v)) return reply(false, "invalid_request"); edit.enabled = cJSON_IsTrue(v); }
            else if (!strcmp(action, "parameter")) {
                edit.op = ENGINE_FX_CHAIN_PARAM; const cJSON* v = cJSON_GetObjectItemCaseSensitive(request, "value");
                if (!number(request, "parameter", &edit.parameter) || (v && (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)))) return reply(false, "invalid_request");
                edit.value = v ? (float)v->valuedouble : 0; const char* mode = text(request, "mode");
                if (cJSON_GetObjectItemCaseSensitive(request, "mode") && !mode) return reply(false, "invalid_request");
                if ((!mode || !strcmp(mode, "native")) && (!v || cJSON_GetObjectItemCaseSensitive(request, "beats"))) return reply(false, "invalid_request");
                if (mode && strcmp(mode, "native")) {
                    edit.mode = !strcmp(mode, "beats") ? FX_PARAM_MODE_BEATS : !strcmp(mode, "beat_rate") ? FX_PARAM_MODE_BEAT_RATE : -1;
                    const cJSON* beats = cJSON_GetObjectItemCaseSensitive(request, "beats");
                    if (!cJSON_IsNumber(beats) || !isfinite(beats->valuedouble)) return reply(false, "invalid_request"); edit.beats = (float)beats->valuedouble;
                }
            } else return reply(false, "invalid_request");
        }
        got = daw_action_edit_fx(state, &current, &edit, &current);
        *changed = got == DAW_ACTION_APPLIED;
        if (got != DAW_ACTION_APPLIED && got != DAW_ACTION_UNCHANGED) {
            static const char* codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target", "busy", "history_unavailable", "engine_rejected"};
            return reply(false, codes[got]);
        }
    }
    entry = remember(bridge, &current); cJSON* r = reply(true, *changed ? "applied" : "unchanged");
    if (entry) chain_json(r, entry); else cJSON_AddBoolToObject(r, "readback_required", true); return r;
}
