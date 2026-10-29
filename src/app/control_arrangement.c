#include "control_bridge_internal.h"
#include "app_state.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Creates explicit application outcomes independently of transport success.
static cJSON* reply(bool ok, const char* code) {
    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", ok); cJSON_AddStringToObject(out, "code", code); return out;
}
// Reads strings without coercing nulls or numbers.
static const char* text(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Preserves all identity and frame bits in wire readback.
static void id(cJSON* out, const char* key, uint64_t value) {
    char buffer[32]; snprintf(buffer, sizeof(buffer), "%" PRIu64, value); cJSON_AddStringToObject(out, key, buffer);
}
// Refuses unknown and repeated fields in each batch operation.
static bool fields(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item=object->child; item; item=item->next) {
        bool found=false;
        for (int i=0; allowed[i]; ++i) if (!strcmp(item->string, allowed[i])) found=true;
        if (!found) return false;
        for (const cJSON* other=item->next; other; other=other->next) if (!strcmp(item->string, other->string)) return false;
    }
    return true;
}
// Parses canonical frame strings with overflow refusal.
static bool frame(const cJSON* object, const char* key, uint64_t* out) {
    const char* value=text(object,key);
    if (!value || !*value || strlen(value)>20 || (value[0]=='0' && value[1])) return false;
    for (const char* p=value; *p; ++p) if (*p<'0' || *p>'9') return false;
    errno=0; char* end; unsigned long long number=strtoull(value,&end,10);
    if (errno || *end) return false;
    *out=number; return true;
}
// Serializes a stable authored region or track handle.
static cJSON* target(DawActionTarget value) {
    cJSON* out=cJSON_CreateObject(); id(out,"generation",value.project_generation);
    id(out,"track_id",value.track_id); id(out,"clip_id",value.clip_id);
    cJSON_AddStringToObject(out,"kind",value.kind==DAW_ACTION_TARGET_TRACK?"track":"midi_region"); return out;
}
// Publishes bounded placements without leaking private content ownership into the wire format.
static void page(cJSON* out, const DawControlArrangementState* entry) {
    const DawMidiLayout* layout=&entry->snapshot->layout;
    cJSON_AddStringToObject(out,"expected_arrangement",entry->token); id(out,"generation",layout->generation);
    cJSON_AddNumberToObject(out,"sample_rate",layout->sample_rate);
    cJSON_AddNumberToObject(out,"track_count",layout->track_count); cJSON_AddNumberToObject(out,"clip_count",layout->clip_count);
    cJSON* tracks=cJSON_AddArrayToObject(out,"tracks");
    for (int i=0; i<layout->track_count; ++i) cJSON_AddItemToArray(tracks,target((DawActionTarget){layout->generation,layout->track_ids[i],0,DAW_ACTION_TARGET_TRACK}));
    cJSON* clips=cJSON_AddArrayToObject(out,"placements");
    for (int i=0; i<layout->clip_count; ++i) {
        const DawMidiPlacement* clip=&layout->clips[i];
        cJSON* item=cJSON_CreateObject(); id(item,"track_id",clip->track_id); id(item,"clip_id",clip->clip_id);
        id(item,"start_frame",clip->start); id(item,"duration_frames",clip->duration); id(item,"offset_frames",clip->offset);
        cJSON_AddStringToObject(item,"kind",clip->kind==ENGINE_CLIP_KIND_MIDI?"midi":"audio"); cJSON_AddItemToArray(clips,item);
    }
    cJSON_AddNumberToObject(out,"batch_capacity",DAW_ARRANGEMENT_BATCH_CAP);
    cJSON_AddNumberToObject(out,"clip_capacity",DAW_ARRANGEMENT_CLIP_CAP);
    cJSON_AddNumberToObject(out,"note_capacity",DAW_ARRANGEMENT_NOTE_CAP);
    cJSON_AddNumberToObject(out,"automation_point_capacity",DAW_ARRANGEMENT_POINT_CAP);
}
// Resolves only retained tokens from this endpoint instance.
static DawControlArrangementState* find(DawControlBridge* bridge, const char* token) {
    if (token) for (int i=0; i<DAW_CONTROL_ARRANGEMENT_STATES; ++i)
        if (bridge->arrangement_states[i].snapshot && !strcmp(token,bridge->arrangement_states[i].token)) return &bridge->arrangement_states[i];
    return NULL;
}
// Reuses exact current captures and bounds independently owned content to four states.
DawControlArrangementState* daw_control_arrangement_capture(DawControlBridge* bridge, AppState* state) {
    for (int i=0; i<DAW_CONTROL_ARRANGEMENT_STATES; ++i)
        if (daw_arrangement_snapshot_current(state,bridge->arrangement_states[i].snapshot)) return &bridge->arrangement_states[i];
    if (bridge->arrangement_sequence==UINT64_MAX) return NULL;
    DawArrangementSnapshot* snapshot=calloc(1,sizeof(*snapshot));
    if (!snapshot) return NULL;
    if (daw_action_get_arrangement(state,snapshot).status!=DAW_MIDI_UNCHANGED) { daw_arrangement_snapshot_free(snapshot); free(snapshot); return NULL; }
    DawControlArrangementState* entry=&bridge->arrangement_states[bridge->arrangement_cursor++%DAW_CONTROL_ARRANGEMENT_STATES];
    daw_arrangement_snapshot_free(entry->snapshot); free(entry->snapshot); entry->snapshot=snapshot;
    snprintf(entry->token,sizeof(entry->token),"%s:arrangement:%" PRIu64,bridge->instance,++bridge->arrangement_sequence); return entry;
}
// Validates operation-specific fields and existing destination lifetimes before reserving project history.
static bool parse_edit(AppState* state, const cJSON* object, DawArrangementEdit* out) {
    static const char* const move[]={"operation","target","destination","start_frame",NULL};
    static const char* const trim[]={"operation","target","duration_frames",NULL};
    static const char* const remove[]={"operation","target",NULL};
    const char* operation=text(object,"operation");
    if (!operation || !daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(object,"target"),&out->target) || out->target.kind!=DAW_ACTION_TARGET_MIDI_REGION) return false;
    if (!strcmp(operation,"delete")) { out->operation=ENGINE_ARRANGE_DELETE; return fields(object,remove); }
    if (!strcmp(operation,"trim")) { out->operation=ENGINE_ARRANGE_TRIM; return fields(object,trim) && frame(object,"duration_frames",&out->duration_frames); }
    if (!strcmp(operation,"duplicate")) out->operation=ENGINE_ARRANGE_DUPLICATE;
    else if (!strcmp(operation,"move")) out->operation=ENGINE_ARRANGE_MOVE;
    else return false;
    if (!fields(object,move) || !frame(object,"start_frame",&out->start_frame)) return false;
    const cJSON* destination=cJSON_GetObjectItemCaseSensitive(object,"destination");
    if (destination) {
        DawActionTarget handle={0}; int track,clip;
        if (!daw_control_parse_target(destination,&handle) || handle.kind!=DAW_ACTION_TARGET_TRACK || handle.clip_id || daw_action_target_resolve(state,handle,&track,&clip)!=DAW_ACTION_UNCHANGED) return false;
        out->destination_track_id=handle.track_id;
    }
    return true;
}
// Adopts atomic arrangement and history through common endpoint admission and immutable receipts.
cJSON* daw_control_arrangement(DawControlBridge* bridge, AppState* state, const cJSON* request, bool* changed) {
    const char* operation=text(request,"op"); bool read=!strcmp(operation,"get_arrangement");
    const char* expected=text(request,"expected_arrangement"); DawControlArrangementState* entry=find(bridge,expected);
    if ((!read || cJSON_GetObjectItemCaseSensitive(request,"expected_arrangement")) && (!entry || !daw_arrangement_snapshot_current(state,entry->snapshot))) return reply(false,"state_conflict");
    if (read) { if (!entry) entry=daw_control_arrangement_capture(bridge,state); if (!entry) return reply(false,"capture_unavailable"); cJSON* out=reply(true,"arrangement"); page(out,entry); return out; }
    cJSON* out=NULL;
    if (!strcmp(operation,"arrange_regions")) {
        const cJSON* array=cJSON_GetObjectItemCaseSensitive(request,"edits"); int count=cJSON_GetArraySize(array);
        if (!cJSON_IsArray(array) || count<1 || count>DAW_ARRANGEMENT_BATCH_CAP) return reply(false,"invalid_request");
        DawArrangementEdit edits[DAW_ARRANGEMENT_BATCH_CAP]={0}; DawActionTarget outputs[DAW_ARRANGEMENT_BATCH_CAP]={0};
        for (int i=0; i<count; ++i) if (!parse_edit(state,cJSON_GetArrayItem(array,i),&edits[i])) return reply(false,"invalid_request");
        DawMidiResult result=daw_action_arrange(state,entry->snapshot,edits,count,outputs);
        static const char* const codes[]={"applied","unchanged","invalid_request","wrong_thread","stale_target","state_conflict","busy","history_unavailable","engine_rejected"};
        out=reply(result.status<=DAW_MIDI_UNCHANGED,codes[result.status]); cJSON_AddStringToObject(out,"message",result.message);
        *changed=result.status==DAW_MIDI_APPLIED;
        if (result.status<=DAW_MIDI_UNCHANGED) { cJSON* values=cJSON_AddArrayToObject(out,"output_targets"); for (int i=0; i<count; ++i) cJSON_AddItemToArray(values,outputs[i].clip_id?target(outputs[i]):cJSON_CreateNull()); }
    } else {
        bool redo=!strcmp(operation,"redo_arrangement"); int count=redo?state->undo.redo_count:state->undo.undo_count;
        UndoCommand* commands=redo?state->undo.redo_stack:state->undo.undo_stack;
        if (daw_midi_actions_busy(state)) return reply(false,"busy");
        if (!count || commands[count-1].type!=UNDO_CMD_ARRANGEMENT) return reply(false,"history_conflict");
        *changed=redo?undo_manager_redo(&state->undo,state):undo_manager_undo(&state->undo,state);
        out=reply(*changed,*changed?"applied":"history_conflict");
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out,"ok"))) {
        entry=daw_control_arrangement_capture(bridge,state); if (entry) page(out,entry); else cJSON_AddBoolToObject(out,"readback_required",true);
    }
    return out;
}
