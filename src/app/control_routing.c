#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/mixer_automation_wire.h"
#include "engine/groups.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Separates domain acceptance from MCP transport success.
static cJSON* reply(bool ok, const char* code) {
    cJSON* out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "ok", ok); cJSON_AddStringToObject(out, "code", code); return out;
}
// Reads strings without accepting numeric or null coercion.
static const char* text(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key); return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Rejects unknown and duplicate operation fields.
static bool fields(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next) {
        bool found = false;
        for (int i = 0; allowed[i]; ++i) if (!strcmp(item->string, allowed[i])) found = true;
        if (!found) return false;
        for (const cJSON* other = item->next; other; other = other->next) if (!strcmp(item->string, other->string)) return false;
    }
    return true;
}
// Preserves all generation and stable identity bits in wire readback.
static void id(cJSON* out, const char* key, uint64_t value) {
    char buffer[32]; snprintf(buffer, sizeof(buffer), "%" PRIu64, value); cJSON_AddStringToObject(out, key, buffer);
}
// Copies a generation-bound track handle without exposing an editable array index.
static cJSON* target(uint64_t generation, uint64_t track_id) {
    cJSON* out = cJSON_CreateObject(); id(out, "generation", generation); id(out, "track_id", track_id); id(out, "clip_id", 0);
    cJSON_AddStringToObject(out, "kind", "track"); return out;
}
// Retains four complete copied routing states with exact-value reuse and endpoint-local tokens.
static DawControlRoutingState* remember(DawControlBridge* bridge, const EngineRoutingSnapshot* snapshot) {
    for (int i = 0; i < DAW_CONTROL_ROUTING_STATES; ++i)
        if (bridge->routing_states[i].token[0] && engine_routing_snapshot_equal(snapshot, &bridge->routing_states[i].snapshot)) return &bridge->routing_states[i];
    if (bridge->routing_sequence == UINT64_MAX) return NULL;
    DawControlRoutingState* entry = &bridge->routing_states[bridge->routing_cursor++ % DAW_CONTROL_ROUTING_STATES];
    entry->snapshot = *snapshot;
    snprintf(entry->token, sizeof(entry->token), "%s:routing:%" PRIu64, bridge->instance, ++bridge->routing_sequence); return entry;
}
// Returns complete bounded topology and linear post-fader send values.
static void values(cJSON* out, const DawControlRoutingState* entry, const AppState* state) {
    const EngineRoutingSnapshot* snapshot = &entry->snapshot;
    cJSON_AddStringToObject(out, "expected_routing", entry->token); id(out, "generation", snapshot->generation);
    cJSON_AddNumberToObject(out, "track_count", snapshot->track_count);
    cJSON* tracks = cJSON_AddArrayToObject(out, "tracks");
    const EngineTrack* authored = engine_get_tracks(state->engine);
    for (int t = 0; t < snapshot->track_count; ++t) {
        const EngineRoutingTrack* row = &snapshot->tracks[t]; cJSON* item = cJSON_CreateObject(); cJSON_AddItemToArray(tracks, item);
        cJSON_AddItemToObject(item, "target", target(snapshot->generation, row->track_id));
        cJSON_AddStringToObject(item, "name", authored[t].name); cJSON_AddBoolToObject(item, "is_return", row->is_return);
        id(item,"persistent_id",authored[t].persistent_id);
        cJSON_AddNumberToObject(item,"visible_row",engine_group_visible_row(authored,snapshot->track_count,t));
        cJSON_AddNumberToObject(item,"depth",engine_group_depth(authored,snapshot->track_count,t));
        cJSON_AddBoolToObject(item,"is_group",row->is_group); cJSON_AddBoolToObject(item,"folded",row->folded);
        cJSON_AddNumberToObject(item,"sibling_order",row->sibling_order);
        cJSON_AddItemToObject(item,"parent_target",row->parent_id ? target(snapshot->generation,row->parent_id) : cJSON_CreateNull());
        if(row->volume_automation.count)daw_mixer_automation_json(cJSON_AddObjectToObject(item,"volume_automation"),&row->volume_automation);else cJSON_AddNullToObject(item,"volume_automation");
        cJSON* sends = cJSON_AddArrayToObject(item, "sends");
        for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
            cJSON* slot = cJSON_CreateObject(); cJSON_AddItemToArray(sends, slot); cJSON_AddNumberToObject(slot, "slot", send);
            cJSON_AddItemToObject(slot, "return_target", row->sends[send].return_id ? target(snapshot->generation, row->sends[send].return_id) : cJSON_CreateNull());
            cJSON_AddNumberToObject(slot, "gain", row->sends[send].gain);
            if(row->sends[send].automation.count)daw_mixer_automation_json(cJSON_AddObjectToObject(slot,"automation"),&row->sends[send].automation);else cJSON_AddNullToObject(slot,"automation");
        }
    }
}
// Resolves one existing track lifetime from a strict nested handle.
static int track_index(AppState* state, const cJSON* value) {
    DawActionTarget handle; int track, clip;
    if (!daw_control_parse_target(value, &handle) || handle.kind != DAW_ACTION_TARGET_TRACK || handle.clip_id ||
        daw_action_target_resolve(state, handle, &track, &clip) != DAW_ACTION_UNCHANGED) return -1;
    return track;
}
// Stages a unique role or send change while validating the final graph as one atomic batch.
static bool edits(AppState* state, const cJSON* array, EngineRoutingSnapshot* next) {
    static const char* const auto_fields[]={"operation","target","slot","automation",NULL};
    static const char* const volume_fields[]={"operation","target","automation",NULL};
    static const char* const role_fields[] = {"operation", "target", "is_return", NULL};
    static const char* const send_fields[] = {"operation", "target", "slot", "return_target", "gain", NULL};
    static const char* const group_fields[]={"operation","target","is_group",NULL};
    static const char* const parent_fields[]={"operation","target","parent_target",NULL};
    static const char* const fold_fields[]={"operation","target","folded",NULL};
    static const char* const order_fields[]={"operation","target","sibling_order",NULL};
    int count = cJSON_GetArraySize(array); if (!cJSON_IsArray(array) || count < 1 || count > 16) return false;
    int changed_tracks[16], changed_slots[16];
    for (int i = 0; i < count; ++i) {
        const cJSON* edit = cJSON_GetArrayItem(array, i); const char* operation = text(edit, "operation");
        int track = track_index(state, cJSON_GetObjectItemCaseSensitive(edit, "target")); if (!operation || track < 0) return false;
        int slot = -1;
        if(!strcmp(operation,"volume_automation")){if(!fields(edit,volume_fields) || !next->tracks[track].is_group || !daw_mixer_automation_parse(cJSON_GetObjectItemCaseSensitive(edit,"automation"),&next->tracks[track].volume_automation,10))return false;slot=-6;
        }else if(!strcmp(operation,"send_automation")){const cJSON* index=cJSON_GetObjectItemCaseSensitive(edit,"slot");if(!fields(edit,auto_fields) || !cJSON_IsNumber(index) || index->valuedouble<0 || index->valuedouble>=ENGINE_SEND_CAP || floor(index->valuedouble)!=index->valuedouble)return false;int send=index->valueint;if(!next->tracks[track].sends[send].return_id || !daw_mixer_automation_parse(cJSON_GetObjectItemCaseSensitive(edit,"automation"),&next->tracks[track].sends[send].automation,1))return false;slot=send;
        }else if (!strcmp(operation, "role")) {
            const cJSON* value = cJSON_GetObjectItemCaseSensitive(edit, "is_return");
            if (!fields(edit, role_fields) || !cJSON_IsBool(value)) return false;
            next->tracks[track].is_return = cJSON_IsTrue(value);
        } else if (!strcmp(operation,"group")) {
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(edit,"is_group");
            if(!fields(edit,group_fields) || !cJSON_IsBool(value))return false;
            next->tracks[track].is_group=cJSON_IsTrue(value); slot=-2;
        } else if (!strcmp(operation,"parent")) {
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(edit,"parent_target");
            if(!fields(edit,parent_fields) || !value)return false;
            int parent=cJSON_IsNull(value) ? -1 : track_index(state,value);if(!cJSON_IsNull(value) && parent<0)return false;
            next->tracks[track].parent_id=parent<0 ? 0 : next->tracks[parent].track_id;slot=-3;
        } else if (!strcmp(operation,"fold")) {
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(edit,"folded");
            if(!fields(edit,fold_fields) || !cJSON_IsBool(value))return false;
            next->tracks[track].folded=cJSON_IsTrue(value);slot=-4;
        } else if (!strcmp(operation,"order")) {
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(edit,"sibling_order");
            if(!fields(edit,order_fields) || !cJSON_IsNumber(value) || value->valuedouble<0 || value->valuedouble>1000000 || floor(value->valuedouble)!=value->valuedouble)return false;
            next->tracks[track].sibling_order=value->valueint;slot=-5;
        } else if (!strcmp(operation, "send")) {
            const cJSON* index = cJSON_GetObjectItemCaseSensitive(edit, "slot");
            const cJSON* gain = cJSON_GetObjectItemCaseSensitive(edit, "gain");
            const cJSON* destination = cJSON_GetObjectItemCaseSensitive(edit, "return_target");
            if (!fields(edit, send_fields) || !cJSON_IsNumber(index) || index->valuedouble < 0 || index->valuedouble >= ENGINE_SEND_CAP || floor(index->valuedouble) != index->valuedouble ||
                !cJSON_IsNumber(gain) || !isfinite(gain->valuedouble) || gain->valuedouble < 0 || gain->valuedouble > 1 || !destination) return false;
            slot = index->valueint; int return_track = cJSON_IsNull(destination) ? -1 : track_index(state, destination);
            if (!cJSON_IsNull(destination) && return_track < 0) return false;
            EngineSend* send=&next->tracks[track].sends[slot];uint64_t id=return_track<0?0:next->tracks[return_track].track_id;
            if(send->return_id!=id && send->automation.count)return false;send->return_id=id;send->gain=(float)gain->valuedouble;
        } else return false;
        for (int previous = 0; previous < i; ++previous) if (changed_tracks[previous] == track && changed_slots[previous] == slot) return false;
        changed_tracks[i] = track; changed_slots[i] = slot;
    }
    return true;
}
// Adopts copied reads, guarded creation/batches/history and endpoint-admitted immutable receipts.
cJSON* daw_control_routing(DawControlBridge* bridge, AppState* state, const cJSON* request, bool* changed) {
    EngineRoutingSnapshot current, output;
    DawActionStatus status = daw_action_get_routing(state, &current);
    if (status != DAW_ACTION_UNCHANGED) return reply(false, "capture_unavailable");
    const char* operation = text(request, "op"); bool read = !strcmp(operation, "get_routing");
    if (!read) {
        const char* token = text(request, "expected_routing"); DawControlRoutingState* expected = NULL;
        if (token) for (int i = 0; i < DAW_CONTROL_ROUTING_STATES; ++i)
            if (!strcmp(token, bridge->routing_states[i].token)) expected = &bridge->routing_states[i];
        if (!expected || !engine_routing_snapshot_equal(&current, &expected->snapshot)) return reply(false, "state_conflict");
        if (!strcmp(operation,"create_group")) status=daw_action_create_group(state,&current,text(request,"name"),&output);
        else if (!strcmp(operation, "create_return")) status = daw_action_create_return(state, &current, &output);
        else if (!strcmp(operation, "edit_routing")) {
            EngineRoutingSnapshot next = current;
            if (!edits(state, cJSON_GetObjectItemCaseSensitive(request, "edits"), &next)) return reply(false, "invalid_request");
            status = daw_action_set_routing(state, &current, &next, &output);
        } else status = daw_action_routing_history(state, &current, !strcmp(operation, "redo_routing"), &output);
    } else output = current;
    static const char* const codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target", "busy", "history_unavailable", "engine_rejected"};
    cJSON* out = reply(status <= DAW_ACTION_UNCHANGED, read ? "routing" : codes[status]);
    *changed = status == DAW_ACTION_APPLIED;
    if (status <= DAW_ACTION_UNCHANGED) {
        DawControlRoutingState* entry = remember(bridge, &output);
        if (entry) values(out, entry, state); else cJSON_AddBoolToObject(out, "readback_required", true);
        if (*changed && (!strcmp(operation, "create_return") || !strcmp(operation,"create_group"))) cJSON_AddItemToObject(out, "created_target", target(output.generation, output.tracks[output.track_count - 1].track_id));
    }
    return out;
}
