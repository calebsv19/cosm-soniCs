#include "control_bridge_internal.h"
#include "app_state.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
// Encodes a domain outcome independently from transport success.
static cJSON* reply(bool ok,const char* code) {
    cJSON* out=cJSON_CreateObject(); cJSON_AddBoolToObject(out,"ok",ok); cJSON_AddStringToObject(out,"code",code); return out;
}
// Reads a strict string without scalar coercion.
static const char* text(const cJSON* request,const char* key) {
    const cJSON* value=cJSON_GetObjectItemCaseSensitive(request,key); return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Emits uint64 identities without floating point loss.
static void id(cJSON* out,const char* key,uint64_t value) {
    char buffer[32]; snprintf(buffer,sizeof(buffer),"%" PRIu64,value); cJSON_AddStringToObject(out,key,buffer);
}
// Reserves/reuses a copied state token, including truthful inspection of mute and solo.
static void values(DawControlBridge* b,cJSON* out,const DawTrackControls* current) {
    DawControlTrackState* entry=NULL;
    for(int i=0;i<DAW_CONTROL_TRACK_STATES;++i) {
        DawControlTrackState* row=&b->track_states[i];
        if(row->token[0] && daw_track_controls_equal(&row->snapshot,current,DAW_TRACK_NAME) &&
            daw_track_controls_equal(&row->snapshot,current,DAW_TRACK_GAIN) && daw_track_controls_equal(&row->snapshot,current,DAW_TRACK_PAN) &&
            row->snapshot.muted==current->muted && row->snapshot.solo==current->solo &&
            row->snapshot.is_group==current->is_group && row->snapshot.is_return==current->is_return) {entry=row;break;}
    }
    if(!entry && b->track_sequence!=UINT64_MAX) {
        entry=&b->track_states[b->track_cursor++ % DAW_CONTROL_TRACK_STATES]; entry->snapshot=*current;
        snprintf(entry->token,sizeof(entry->token),"%s:track:%" PRIu64,b->instance,++b->track_sequence);
    }
    if(entry)cJSON_AddStringToObject(out,"expected_state",entry->token);else cJSON_AddBoolToObject(out,"readback_required",true);
    cJSON* target=cJSON_AddObjectToObject(out,"target");id(target,"generation",current->target.project_generation);
    id(target,"track_id",current->target.track_id);id(target,"clip_id",0);cJSON_AddStringToObject(target,"kind","track");
    id(out,"persistent_id",current->persistent_id);cJSON_AddStringToObject(out,"name",current->name);
    cJSON_AddNumberToObject(out,"gain",current->gain);cJSON_AddNumberToObject(out,"pan",current->pan);
    cJSON_AddBoolToObject(out,"muted",current->muted);cJSON_AddBoolToObject(out,"solo",current->solo);
    cJSON_AddBoolToObject(out,"is_group",current->is_group);cJSON_AddBoolToObject(out,"is_return",current->is_return);
}
// Rejects decoded NUL escapes before cJSON can silently shorten a control value or identity string.
static bool nul_escape(const char* raw) {
    for(const char* p=raw; p && *p; ++p) {
        if(*p!='\\')continue;
        if(!strncmp(p,"\\u0000",6))return true;
        if(p[1])++p;
    }
    return false;
}
// Routes one independently guarded label or scalar after common receipt admission.
cJSON* daw_control_track_controls(DawControlBridge* b,AppState* s,const cJSON* request,const char* raw,bool* changed) {
    if(nul_escape(raw))return reply(false,"invalid_request");
    DawActionTarget target;
    if(!daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request,"target"),&target) || target.kind!=DAW_ACTION_TARGET_TRACK)return reply(false,"invalid_target");
    DawTrackControls current;DawActionStatus status=daw_track_controls_get(s,target,&current);
    if(status!=DAW_ACTION_UNCHANGED)return reply(false,status==DAW_ACTION_WRONG_THREAD ? "wrong_thread" : "stale_target");
    const char* op=text(request,"op");
    if(strcmp(op,"get_track_controls")) {
        const char* field=text(request,"control");DawTrackControl control;
        if(!field)return reply(false,"invalid_request");
        if(!strcmp(field,"name"))control=DAW_TRACK_NAME;else if(!strcmp(field,"gain"))control=DAW_TRACK_GAIN;
        else if(!strcmp(field,"pan"))control=DAW_TRACK_PAN;else return reply(false,"invalid_request");
        const char* token=text(request,"expected_state");DawControlTrackState* expected=NULL;
        if(token)for(int i=0;i<DAW_CONTROL_TRACK_STATES;++i)if(!strcmp(token,b->track_states[i].token))expected=&b->track_states[i];
        if(!expected || !daw_track_controls_equal(&expected->snapshot,&current,control))return reply(false,"state_conflict");
        if(!strcmp(op,"set_track_control")) {
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(request,"value");
            if(control==DAW_TRACK_NAME ? !cJSON_IsString(value) : !cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
                (control==DAW_TRACK_GAIN ? value->valuedouble<0 || value->valuedouble>10 : value->valuedouble<-1 || value->valuedouble>1))return reply(false,"invalid_request");
            status=daw_track_controls_set(s,&expected->snapshot,control,control==DAW_TRACK_NAME ? value->valuestring : NULL,
                control==DAW_TRACK_NAME ? 0 : (float)value->valuedouble);
        } else status=daw_track_controls_history(s,&expected->snapshot,control,!strcmp(op,"redo_track_control"));
    }
    static const char* const codes[]={"applied","unchanged","invalid_request","wrong_thread","state_conflict","busy","history_unavailable","engine_rejected"};
    cJSON* out=reply(status<=DAW_ACTION_UNCHANGED,!strcmp(op,"get_track_controls") ? "track_controls" : codes[status]);
    *changed=status==DAW_ACTION_APPLIED;
    if(status<=DAW_ACTION_UNCHANGED && daw_track_controls_get(s,target,&current)==DAW_ACTION_UNCHANGED)values(b,out,&current);
    return out;
}
