#include "control_bridge_internal.h"
#include "app_state.h"
#include "cJSON.h"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
// Creates a domain response without confusing transport success with accepted edits.
static cJSON* reply(bool ok,const char* code){cJSON* out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"ok",ok);cJSON_AddStringToObject(out,"code",code);return out;}
// Reads a strict string without numeric coercion.
static const char* text(const cJSON* o,const char* key){const cJSON* v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v) ? v->valuestring : NULL;}
// Rejects unknown, duplicated or missing nested curve fields.
static bool fields(const cJSON* o,const char* const* names){if(!cJSON_IsObject(o))return false;int count=0;for(const cJSON* p=o->child;p;p=p->next){bool found=false;for(int i=0;names[i];++i)if(!strcmp(p->string,names[i]))found=true;if(!found)return false;for(const cJSON* q=p->next;q;q=q->next)if(!strcmp(p->string,q->string))return false;++count;}int n=0;while(names[n])++n;return count==n;}
// Accepts only finite physical numbers in their advertised domain.
static bool number(const cJSON* o,const char* key,double low,double high,float* out){const cJSON* v=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<low || v->valuedouble>high)return false;*out=(float)v->valuedouble;return true;}
// Parses the complete bounded curve with four bands and two fixed-slope cuts.
static bool curve(const cJSON* o,EngineEqCurve* out){
    static const char* const top[]={"low_cut","high_cut","bands",NULL},*const cut[]={"enabled","frequency_hz",NULL},*const band[]={"enabled","frequency_hz","gain_db","width_octaves",NULL};
    if(!fields(o,top))return false;EngineEqCut* cuts[]={&out->low_cut,&out->high_cut};const char* keys[]={"low_cut","high_cut"};
    for(int i=0;i<2;++i){const cJSON* v=cJSON_GetObjectItemCaseSensitive(o,keys[i]),*enabled=cJSON_GetObjectItemCaseSensitive(v,"enabled");if(!fields(v,cut) || !cJSON_IsBool(enabled) || !number(v,"frequency_hz",20,20000,&cuts[i]->freq_hz))return false;cuts[i]->enabled=cJSON_IsTrue(enabled);}
    const cJSON* bands=cJSON_GetObjectItemCaseSensitive(o,"bands");if(!cJSON_IsArray(bands) || cJSON_GetArraySize(bands)!=ENGINE_EQ_BANDS)return false;
    for(int i=0;i<ENGINE_EQ_BANDS;++i){const cJSON* v=cJSON_GetArrayItem(bands,i),*enabled=cJSON_GetObjectItemCaseSensitive(v,"enabled");EngineEqBand* b=&out->bands[i];if(!fields(v,band) || !cJSON_IsBool(enabled) || !number(v,"frequency_hz",20,20000,&b->freq_hz) || !number(v,"gain_db",-20,20,&b->gain_db) || !number(v,"width_octaves",.1,4,&b->q_width))return false;b->enabled=cJSON_IsTrue(enabled);}return true;
}
// Copies exact identities as decimal strings rather than floating-point numbers.
static void id(cJSON* o,const char* key,uint64_t value){char b[32];snprintf(b,sizeof(b),"%" PRIu64,value);cJSON_AddStringToObject(o,key,b);}
// Encodes stored, meaningful EQ controls without unsupported cut-slope metadata.
static void values(DawControlBridge* b,AppState* s,cJSON* out,const DawEqSnapshot* current){
    DawControlEqState* entry=NULL;for(int i=0;i<DAW_CONTROL_EQ_STATES;++i)if(b->eq_states[i].token[0] && daw_eq_equal(current,&b->eq_states[i].snapshot))entry=&b->eq_states[i];
    if(!entry && b->eq_sequence!=UINT64_MAX){entry=&b->eq_states[b->eq_cursor++ % DAW_CONTROL_EQ_STATES];entry->snapshot=*current;snprintf(entry->token,sizeof(entry->token),"%s:eq:%" PRIu64,b->instance,++b->eq_sequence);}
    if(entry)cJSON_AddStringToObject(out,"expected_state",entry->token);else cJSON_AddBoolToObject(out,"readback_required",true);
    id(out,"generation",current->generation);id(out,"track_id",current->track_id);cJSON_AddStringToObject(out,"scope",current->track_id ? "track" : "master");
    cJSON_AddNumberToObject(out,"enabled_frequency_max_hz",fminf(20000,engine_get_config(s->engine)->sample_rate*.45f));
    cJSON* eq=cJSON_AddObjectToObject(out,"curve");const EngineEqCut* cuts[]={&current->curve.low_cut,&current->curve.high_cut};const char* keys[]={"low_cut","high_cut"};
    for(int i=0;i<2;++i){cJSON* cut=cJSON_AddObjectToObject(eq,keys[i]);cJSON_AddBoolToObject(cut,"enabled",cuts[i]->enabled);cJSON_AddNumberToObject(cut,"frequency_hz",cuts[i]->freq_hz);}
    cJSON* bands=cJSON_AddArrayToObject(eq,"bands");for(int i=0;i<ENGINE_EQ_BANDS;++i){const EngineEqBand* b=&current->curve.bands[i];cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(bands,item);cJSON_AddBoolToObject(item,"enabled",b->enabled);cJSON_AddNumberToObject(item,"frequency_hz",b->freq_hz);cJSON_AddNumberToObject(item,"gain_db",b->gain_db);cJSON_AddNumberToObject(item,"width_octaves",b->q_width);}
}
// Resolves master or stable child/group/return and applies copied exact-curve guards after receipt admission.
cJSON* daw_control_eq(DawControlBridge* b,AppState* s,const cJSON* request,bool* changed){
    uint64_t id=0;const cJSON* target=cJSON_GetObjectItemCaseSensitive(request,"target");
    if(target){DawActionTarget handle;int t,c;if(!daw_control_parse_target(target,&handle) || handle.kind!=DAW_ACTION_TARGET_TRACK)return reply(false,"invalid_target");if(daw_action_target_resolve(s,handle,&t,&c)!=DAW_ACTION_UNCHANGED)return reply(false,"stale_target");id=handle.track_id;}
    DawEqSnapshot current;DawActionStatus status=daw_eq_get(s,engine_instance_id(s->engine),id,&current);if(status!=DAW_ACTION_UNCHANGED)return reply(false,"capture_unavailable");
    const char* op=text(request,"op");if(strcmp(op,"get_eq")){
        const char* token=text(request,"expected_state");DawControlEqState* expected=NULL;if(token)for(int i=0;i<DAW_CONTROL_EQ_STATES;++i)if(!strcmp(token,b->eq_states[i].token))expected=&b->eq_states[i];
        if(!expected || !daw_eq_equal(&expected->snapshot,&current))return reply(false,"state_conflict");
        if(!strcmp(op,"edit_eq")){EngineEqCurve next={0};if(!curve(cJSON_GetObjectItemCaseSensitive(request,"curve"),&next))return reply(false,"invalid_request");status=daw_eq_set(s,&expected->snapshot,&next);}else status=daw_eq_history(s,&expected->snapshot,!strcmp(op,"redo_eq"));
    }
    static const char* const codes[]={"applied","unchanged","invalid_request","wrong_thread","state_conflict","busy","history_unavailable","engine_rejected"};
    cJSON* out=reply(status<=DAW_ACTION_UNCHANGED,!strcmp(op,"get_eq") ? "eq" : codes[status]);*changed=status==DAW_ACTION_APPLIED;
    if(status<=DAW_ACTION_UNCHANGED && daw_eq_get(s,current.generation,id,&current)==DAW_ACTION_UNCHANGED)values(b,s,out,&current);return out;
}
