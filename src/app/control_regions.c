#include "control_bridge_internal.h"
#include "app_state.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Creates a stable domain outcome independently of MCP transport success.
static cJSON* reply(bool ok,const char* code){cJSON* r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",ok);cJSON_AddStringToObject(r,"code",code);return r;}
// Reads a typed string without wire coercion.
static const char* text(const cJSON* r,const char* k){const cJSON* v=cJSON_GetObjectItemCaseSensitive(r,k);return cJSON_IsString(v)?v->valuestring:NULL;}
// Serializes complete unsigned identities and frame values as canonical decimal strings.
static void id(cJSON* r,const char* k,uint64_t v){char b[32];snprintf(b,sizeof(b),"%" PRIu64,v);cJSON_AddStringToObject(r,k,b);}
// Refuses noncanonical or overflowing unsigned frame strings.
static bool frame(const cJSON* r,const char* k,uint64_t* out){const char* v=text(r,k);if(!v || !*v || strlen(v)>20 || (v[0]=='0' && v[1]))return false;
    for(const char* p=v;*p;++p)if(*p<'0' || *p>'9')return false;errno=0;char* end;unsigned long long n=strtoull(v,&end,10);if(errno || *end)return false;*out=n;return true;}
// Parses optional integral page offsets with explicit bounds and no Boolean conversion.
static bool offset(const cJSON* r,const char* k,int max,int* out){const cJSON* v=cJSON_GetObjectItemCaseSensitive(r,k);*out=0;if(!v)return true;
    if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>max || floor(v->valuedouble)!=v->valuedouble)return false;*out=v->valueint;return true;}
// Maps common action codes without obscuring accepted edits behind unavailable readback.
static cJSON* action(DawMidiResult a){static const char* const codes[]={"applied","unchanged","invalid_request","wrong_thread","stale_target","state_conflict","busy","history_unavailable","engine_rejected"};
    cJSON* r=reply(a.status<=DAW_MIDI_UNCHANGED,codes[a.status]);cJSON_AddStringToObject(r,"message",a.message);return r;}
// Retains exact independently copied layout state behind a bounded endpoint token cache.
static DawControlLayoutState* remember(DawControlBridge* b,DawMidiLayout** layout){
    for(int i=0;i<DAW_CONTROL_LAYOUT_STATES;++i)if(b->layout_states[i].snapshot && !memcmp(b->layout_states[i].snapshot,*layout,sizeof(**layout)))return &b->layout_states[i];
    if(b->layout_sequence==UINT64_MAX)return NULL;
    DawControlLayoutState* s=&b->layout_states[b->layout_cursor++%DAW_CONTROL_LAYOUT_STATES];free(s->snapshot);s->snapshot=*layout;*layout=NULL;
    snprintf(s->token,sizeof(s->token),"%s:layout:%" PRIu64,b->instance,++b->layout_sequence);return s;
}
// Finds only current endpoint tokens without accepting arbitrary client snapshots.
static DawControlLayoutState* find(DawControlBridge* b,const char* token){if(token)for(int i=0;i<DAW_CONTROL_LAYOUT_STATES;++i)
    if(b->layout_states[i].snapshot && !strcmp(token,b->layout_states[i].token))return &b->layout_states[i];return NULL;}
// Serializes bounded track, clip-placement and tempo streams from one exact retained layout.
static void page(cJSON* r,const DawControlLayoutState* s,int track_offset,int clip_offset,int tempo_offset){
    const DawMidiLayout* l=s->snapshot;cJSON_AddStringToObject(r,"expected_layout",s->token);id(r,"generation",l->generation);
    cJSON_AddNumberToObject(r,"track_count",l->track_count);cJSON_AddNumberToObject(r,"clip_count",l->clip_count);cJSON_AddNumberToObject(r,"tempo_event_count",l->tempo_count);cJSON_AddNumberToObject(r,"sample_rate",l->sample_rate);
    cJSON* tracks=cJSON_AddArrayToObject(r,"tracks");int end=track_offset+64;if(end>l->track_count)end=l->track_count;
    for(int i=track_offset;i<end;++i){cJSON* t=cJSON_CreateObject();id(t,"generation",l->generation);id(t,"track_id",l->track_ids[i]);id(t,"clip_id",0);cJSON_AddStringToObject(t,"kind","track");cJSON_AddItemToArray(tracks,t);}
    cJSON_AddNumberToObject(r,"next_offset",end<l->track_count?end:-1);
    cJSON* clips=cJSON_AddArrayToObject(r,"placements");end=clip_offset+128;if(end>l->clip_count)end=l->clip_count;
    for(int i=clip_offset;i<end;++i){const DawMidiPlacement* p=&l->clips[i];cJSON* c=cJSON_CreateObject();id(c,"track_id",p->track_id);id(c,"clip_id",p->clip_id);id(c,"start_frame",p->start);id(c,"duration_frames",p->duration);id(c,"offset_frames",p->offset);cJSON_AddStringToObject(c,"kind",p->kind==ENGINE_CLIP_KIND_MIDI?"midi":"audio");cJSON_AddItemToArray(clips,c);}
    cJSON_AddNumberToObject(r,"next_clip_offset",end<l->clip_count?end:-1);
    cJSON* tempo=cJSON_AddArrayToObject(r,"tempo_events");end=tempo_offset+64;if(end>l->tempo_count)end=l->tempo_count;
    for(int i=tempo_offset;i<end;++i){cJSON* t=cJSON_CreateObject();cJSON_AddNumberToObject(t,"beat",l->tempo[i].beat);cJSON_AddNumberToObject(t,"bpm",l->tempo[i].bpm);cJSON_AddItemToArray(tempo,t);}
    cJSON_AddNumberToObject(r,"next_tempo_offset",end<l->tempo_count?end:-1);
}
// Routes layout reads, atomic creation and guarded structural history on the application owner.
cJSON* daw_control_regions(DawControlBridge* bridge,AppState* state,const cJSON* request,const DawActionTarget* target,bool* changed){
    const char* op=text(request,"op");bool read=!strcmp(op,"get_midi_layout");
    DawMidiLayout* current=malloc(sizeof(*current));if(!current)return reply(false,"allocation_failed");
    DawMidiResult got=daw_action_get_midi_layout(state,current);cJSON* result=NULL;
    if(got.status!=DAW_MIDI_UNCHANGED){result=action(got);goto done;}
    const char* expected=text(request,"expected_layout");DawControlLayoutState* entry=find(bridge,expected);
    if((!read || cJSON_GetObjectItemCaseSensitive(request,"expected_layout")) && (!entry || memcmp(entry->snapshot,current,sizeof(*current)))){result=reply(false,"state_conflict");goto done;}
    if(read){int t,c,p;if(!offset(request,"offset",DAW_MIDI_LAYOUT_TRACK_CAP,&t) || !offset(request,"clip_offset",DAW_MIDI_LAYOUT_CLIP_CAP,&c) || !offset(request,"tempo_offset",DAW_MIDI_TEMPO_EVENT_CAP,&p) || t>current->track_count || c>current->clip_count || p>current->tempo_count){result=reply(false,"invalid_offset");goto done;}
        if(!entry)entry=remember(bridge,&current);if(!entry){result=reply(false,"allocation_failed");goto done;}
        result=reply(true,"midi_layout");page(result,entry,t,c,p);goto done;
    }
    DawActionTarget created={0};
    if(!strcmp(op,"create_midi_region")){uint64_t start,duration;
        if(!frame(request,"start_frame",&start) || !frame(request,"duration_frames",&duration)){result=reply(false,"invalid_request");goto done;}
        got=daw_action_create_midi_region(state,entry->snapshot,target,start,duration,&created);result=action(got);*changed=got.status==DAW_MIDI_APPLIED;
        if(*changed){cJSON* t=cJSON_AddObjectToObject(result,"created_target");id(t,"generation",created.project_generation);id(t,"track_id",created.track_id);id(t,"clip_id",created.clip_id);cJSON_AddStringToObject(t,"kind","midi_region");}
    } else {
        bool redo=!strcmp(op,"redo_midi_region");int count=redo?state->undo.redo_count:state->undo.undo_count;UndoCommand* commands=redo?state->undo.redo_stack:state->undo.undo_stack;
        if(!target || !count || commands[count-1].type!=UNDO_CMD_MIDI_REGION){result=reply(false,"history_conflict");goto done;}
        DawActionTarget last=commands[count-1].data.midi_region.target;
        if(last.project_generation!=target->project_generation || last.track_id!=target->track_id || last.clip_id!=target->clip_id || last.kind!=target->kind){result=reply(false,"history_conflict");goto done;}
        if(daw_midi_actions_busy(state)){result=reply(false,"busy");goto done;}
        bool accepted=redo?undo_manager_redo(&state->undo,state):undo_manager_undo(&state->undo,state);*changed=accepted;result=reply(accepted,accepted?"applied":"history_conflict");
    }
    if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))){if(!current)current=malloc(sizeof(*current));
        got=current?daw_action_get_midi_layout(state,current):(DawMidiResult){.status=DAW_MIDI_ENGINE_REJECTED};entry=got.status==DAW_MIDI_UNCHANGED?remember(bridge,&current):NULL;
        if(entry)page(result,entry,0,0,0);else cJSON_AddBoolToObject(result,"readback_required",true);
    }
done:
    free(current);return result;
}
