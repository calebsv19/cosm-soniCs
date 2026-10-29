#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/mixer_automation_wire.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Creates an explicit domain envelope independently of MCP transport success.
static cJSON* reply(bool ok,const char* code) {cJSON* j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"ok",ok);cJSON_AddStringToObject(j,"code",code);return j;}
// Reads strings without coercion or borrowing a pointer after dispatch ends.
static const char* text(const cJSON* j,const char* key) {const cJSON* v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
// Preserves full-width frame/identity values on the JSON wire.
static void id(cJSON* j,const char* key,uint64_t v) {char s[32];snprintf(s,sizeof(s),"%" PRIu64,v);cJSON_AddStringToObject(j,key,s);}
// Refuses noncanonical, signed and overflowing frame strings.
static bool frame(const cJSON* j,const char* key,uint64_t* out) {
    const char* s=text(j,key);if(!s || !*s || strlen(s)>20 || (s[0]=='0' && s[1]))return false;
    for(const char* p=s;*p;++p)if(*p<'0' || *p>'9')return false;errno=0;char* end;uint64_t v=strtoull(s,&end,10);if(errno || *end)return false;*out=v;return true;
}
// Reads a bounded page index without accepting floating point or negative values.
static bool offset(const cJSON* j,const char* key,int maximum,int* out) {
    const cJSON* v=cJSON_GetObjectItemCaseSensitive(j,key);*out=0;if(!v)return true;if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>maximum || floor(v->valuedouble)!=v->valuedouble)return false;*out=(int)v->valuedouble;return true;
}
// Represents calibrated silence as null dBFS without inventing a finite floor.
static void db(cJSON* j,const char* key,double amplitude) {cJSON_AddItemToObject(j,key,amplitude>0 ? cJSON_CreateNumber(20*log10(amplitude)) : cJSON_CreateNull());}
// Names observable graph stages with the actual source/bus/master processing order.
static const char* stage_name(const DawMixNode* n,int stage) {
    if(!n->track_id){const char* names[]={"master_sum_pre_eq_inserts","master_post_eq_pre_inserts","master_post_inserts_pre_sanitize","master_output_post_sanitize"};return names[stage];}
    const char* names[]={"input_post_source_gain_or_bus_sum","post_inserts_eq_pre_bus_gain_pan","output_post_bus_gain_pan_pre_edge_compensation","unavailable"};return names[stage];
}
// Describes copied dedicated EQ settings, including disabled authored bands.
static void eq_json(cJSON* j,const EngineEqCurve* curve) {
    const EngineEqCut* cuts[]={&curve->low_cut,&curve->high_cut};const char* names[]={"low_cut","high_cut"};
    for(int i=0;i<2;++i){cJSON* c=cJSON_AddObjectToObject(j,names[i]);cJSON_AddBoolToObject(c,"enabled",cuts[i]->enabled);cJSON_AddNumberToObject(c,"frequency_hz",cuts[i]->freq_hz);}
    cJSON* bands=cJSON_AddArrayToObject(j,"bands");for(int b=0;b<ENGINE_EQ_BANDS;++b){const EngineEqBand* band=&curve->bands[b];cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(bands,item);cJSON_AddNumberToObject(item,"index",b);cJSON_AddBoolToObject(item,"enabled",band->enabled);cJSON_AddNumberToObject(item,"frequency_hz",band->freq_hz);cJSON_AddNumberToObject(item,"gain_db",band->gain_db);cJSON_AddNumberToObject(item,"bandwidth_octaves",band->q_width);}
}
// Describes stable topology and authored controls without treating nested nodes as additive stems.
static void node_json(cJSON* j,const DawMixNode* n) {
    id(j,"track_id",n->track_id);id(j,"parent_id",n->parent_id);cJSON_AddStringToObject(j,"kind",!n->track_id ? "master" : n->group ? "group" : n->returns ? "return" : "child");cJSON_AddStringToObject(j,"name",n->name);
    cJSON_AddBoolToObject(j,"source_active_flag",n->active);cJSON_AddBoolToObject(j,"muted",n->muted);cJSON_AddBoolToObject(j,"solo",n->solo);cJSON_AddNumberToObject(j,"authored_gain_linear",n->gain);db(j,"authored_gain_db",n->gain);cJSON_AddNumberToObject(j,"authored_pan_balance",n->pan);
    daw_mixer_automation_json(cJSON_AddObjectToObject(j,"volume_automation"),&n->volume_automation);
    cJSON* sends=cJSON_AddArrayToObject(j,"sends");for(int i=0;i<ENGINE_SEND_CAP;++i)if(n->sends[i].return_id){cJSON* send=cJSON_CreateObject();cJSON_AddItemToArray(sends,send);id(send,"return_id",n->sends[i].return_id);daw_mixer_automation_json(cJSON_AddObjectToObject(send,"automation"),&n->sends[i].automation);cJSON_AddNumberToObject(send,"gain_linear",n->sends[i].gain);db(send,"gain_db",n->sends[i].gain);cJSON_AddStringToObject(send,"tap","post processed lane output; group sums include descendants");}
}
// Declares range, full cold preceding context, identity and limits for every immutable readback.
static void describe(cJSON* j,const char* token,const DawMixCapture* c) {
    if(c->whole_song){cJSON_AddStringToObject(j,"schema","sonics_song_loudness_v1");cJSON_AddStringToObject(j,"capture_id",token);id(j,"generation",c->snapshot->generation);id(j,"settings_fingerprint_fnv1a64",c->snapshot->settings_fingerprint);id(j,"start_frame",0);id(j,"end_frame",c->end);id(j,"authored_end_frame",c->authored_end);id(j,"tail_frames",c->tail_frames);cJSON_AddNumberToObject(j,"sample_rate",c->sample_rate);cJSON_AddNumberToObject(j,"channels",c->channels);cJSON_AddNumberToObject(j,"node_count",c->snapshot->tracks+1);cJSON_AddStringToObject(j,"scope","complete authored arrangement from zero plus explicit tail; no live transport/selection/DSP or music changes; detailed FX/source/spectral pages excluded");return;}
    cJSON_AddStringToObject(j,"schema","sonics_mix_capture_v3");cJSON_AddStringToObject(j,"capture_id",token);id(j,"generation",c->snapshot->generation);id(j,"settings_fingerprint_fnv1a64",c->snapshot->settings_fingerprint);
    id(j,"start_frame",c->start);id(j,"end_frame",c->end);id(j,"frames",c->end-c->start);id(j,"context_start_frame",0);id(j,"preceding_context_frames",c->start);id(j,"rendered_frames",c->rendered_frames);id(j,"following_context_frames",c->rendered_frames-c->end);cJSON_AddNumberToObject(j,"render_block_frames",c->block_frames);id(j,"master_latency_frames",c->master_latency);
    cJSON* edges=cJSON_AddArrayToObject(j,"bin_frame_edges");for(int b=0;b<=DAW_MIX_BINS;++b){uint64_t edge=c->start+((uint64_t)b*(c->end-c->start)+DAW_MIX_BINS-1)/DAW_MIX_BINS;char value[32];snprintf(value,sizeof(value),"%" PRIu64,edge);cJSON_AddItemToArray(edges,cJSON_CreateString(value));}
    cJSON_AddNumberToObject(j,"sample_rate",c->sample_rate);cJSON_AddNumberToObject(j,"channels",c->channels);cJSON_AddNumberToObject(j,"node_count",c->snapshot->tracks+1);cJSON_AddNumberToObject(j,"source_count",c->snapshot->sources);cJSON_AddNumberToObject(j,"time_bins",DAW_MIX_BINS);cJSON_AddBoolToObject(j,"normalized",false);
    cJSON_AddStringToObject(j,"context","private production graph, cold/reset at project frame zero, all preceding authored input rendered; live DSP/audition history excluded; following samples retained for declared lookahead and full production blocks; no extra measured tail; linear timeline, live loop/audition state excluded");
    cJSON_AddStringToObject(j,"accounting","each stage is measured in context and aligned by its own latency; children, parents, returns and master overlap and MUST NOT be summed as independent contributions; pans use stereo balance");
    cJSON_AddStringToObject(j,"calibration","original float sample peak and unweighted RMS; dBFS=20log10(amplitude), silence null; full_scale abs>=1; overload abs>1; per-channel equally spaced frame buckets; loudness/true peak in separate processed-output object");
    cJSON* missing=cJSON_AddObjectToObject(j,"unavailable");cJSON_AddStringToObject(missing,"raw_audio_region_tails","MIDI source output is observed before child/region scalar gain; raw audio-file region tails are outside this slice");
    cJSON_AddStringToObject(missing,"other_effect_gain_reduction","actual per-channel pre-makeup reduction is available only for native Compressor and Limiter; input/output ratios are not substituted");
    cJSON_AddStringToObject(missing,"whole_programme_loudness","integrated loudness covers only the selected range, bounded to 30 seconds; LRA and surround layouts unavailable");cJSON_AddStringToObject(missing,"individual_note_audible_tail","prepared ending fences and selected automated targets are captured; audible source tails are aggregate, not attributed to overlapping individual notes");
    cJSON_AddStringToObject(missing,"media_content_fingerprint","settings hash is noncryptographic and excludes sample-bank/decoded-media bytes; exact guards and private media pins protect this capture, not cross-session asset reproducibility");
}
// Serializes one bounded stage without expanding the captured PCM into JSON.
static void stage_json(cJSON* j,const DawMixNode* node,int stage,const DawMixStage* s,int channels) {
    cJSON_AddStringToObject(j,"stage",stage_name(node,stage));cJSON_AddBoolToObject(j,"available",s->available);if(!s->available){cJSON_AddStringToObject(j,"reason","sanitization occurs only on master output");return;}
    id(j,"latency_frames",s->latency);cJSON* array=cJSON_AddArrayToObject(j,"channels");
    for(int ch=0;ch<channels;++ch){const DawMixMeter* m=&s->channel[ch];cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(array,item);cJSON_AddNumberToObject(item,"index",ch);cJSON_AddStringToObject(item,"label",channels==1 ? "mono" : channels==2 ? ch ? "right" : "left" : "indexed channel");
        double rms=m->samples?sqrt(m->power/m->samples):0;id(item,"sample_count",m->samples);cJSON_AddNumberToObject(item,"activity_threshold_dbfs",-80);id(item,"above_threshold_samples",m->active_samples);if(m->active_samples){id(item,"first_above_threshold_frame",m->first_active_frame);id(item,"last_above_threshold_frame",m->last_active_frame);}else{cJSON_AddNullToObject(item,"first_above_threshold_frame");cJSON_AddNullToObject(item,"last_above_threshold_frame");}cJSON_AddNumberToObject(item,"sample_peak",m->peak);cJSON_AddNumberToObject(item,"rms",rms);db(item,"sample_peak_dbfs",m->peak);db(item,"rms_dbfs",rms);id(item,"full_scale_samples",m->full_scale);id(item,"overload_samples",m->overload);
        cJSON* peak=cJSON_AddArrayToObject(item,"bin_sample_peak");cJSON* power=cJSON_AddArrayToObject(item,"bin_rms");cJSON* counts=cJSON_AddArrayToObject(item,"bin_sample_count");
        for(int b=0;b<DAW_MIX_BINS;++b){cJSON_AddItemToArray(peak,cJSON_CreateNumber(m->bin_peak[b]));cJSON_AddItemToArray(power,cJSON_CreateNumber(m->bin_samples[b]?sqrt(m->bin_power[b]/m->bin_samples[b]):0));cJSON_AddItemToArray(counts,cJSON_CreateNumber((double)m->bin_samples[b]));}
    }
}
// Serializes one bounded insert page and true applied attenuation, excluding makeup and RMS estimates.
static void effect_json(cJSON* j,const DawMixNode* node,const DawMixEffect* effect,int position,int channels) {
    const FxMasterInstanceInfo* f=&node->fx.chain.items[position];id(j,"effect_id",f->id);cJSON_AddNumberToObject(j,"type_id",f->type);cJSON_AddNumberToObject(j,"position",position);cJSON_AddBoolToObject(j,"enabled",f->enabled);
    cJSON* sides=cJSON_AddArrayToObject(j,"boundaries");
    for(int side=0;side<2;++side){cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(sides,item);stage_json(item,node,0,&effect->boundary[side],channels);cJSON_ReplaceItemInObject(item,"stage",cJSON_CreateString(side?"insert_output":"insert_input"));}
    cJSON* gr=cJSON_AddObjectToObject(j,"gain_reduction");cJSON_AddBoolToObject(gr,"available",effect->reduction_available);
    cJSON_AddStringToObject(gr,"semantics","actual applied pre-makeup gain in dB; nonpositive attenuation; output-sample clock minus this insert output latency; compressor independent channels, limiter linked channels; cold captured settings exclude live bypass ramps");
    if(!effect->reduction_available){cJSON_AddStringToObject(gr,"reason","processor does not expose a qualified per-sample applied reduction trace");return;}
    id(gr,"latency_frames",effect->boundary[1].latency);cJSON* array=cJSON_AddArrayToObject(gr,"channels");
    for(int ch=0;ch<channels;++ch){const DawMixReduction* r=&effect->reduction[ch];cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(array,item);cJSON_AddNumberToObject(item,"index",ch);id(item,"sample_count",r->samples);cJSON_AddNumberToObject(item,"minimum_gain_db",r->minimum_db);cJSON_AddNumberToObject(item,"mean_gain_db",r->samples?r->sum_db/r->samples:0);
        cJSON* minima=cJSON_AddArrayToObject(item,"bin_minimum_gain_db");cJSON* means=cJSON_AddArrayToObject(item,"bin_mean_gain_db");cJSON* counts=cJSON_AddArrayToObject(item,"bin_sample_count");
        for(int b=0;b<DAW_MIX_BINS;++b){cJSON_AddItemToArray(minima,cJSON_CreateNumber(r->bin_minimum_db[b]));cJSON_AddItemToArray(means,cJSON_CreateNumber(r->bin_samples[b]?r->bin_sum_db[b]/r->bin_samples[b]:0));cJSON_AddItemToArray(counts,cJSON_CreateNumber((double)r->bin_samples[b]));}
    }
}
// Describes region-local or inherited base sound and actual supported parameter IDs with explicit gate units.
static void source_json(cJSON* j,const DawMixSource* s) {
    id(j,"track_id",s->track_id);id(j,"clip_id",s->clip_id);id(j,"start_frame",s->start);id(j,"duration_frames",s->duration);cJSON_AddStringToObject(j,"name",s->name);cJSON_AddStringToObject(j,"kind",s->midi?"midi":"audio");cJSON_AddNumberToObject(j,"gain_linear",s->gain);
    if(!s->midi)return;cJSON_AddBoolToObject(j,"inherits_track_instrument",s->inherited);cJSON_AddStringToObject(j,"effective_base_preset",engine_instrument_preset_id_string(s->preset));cJSON_AddNumberToObject(j,"note_count",s->notes);id(j,"region_relative_gate_min_frames",s->gate_min);id(j,"region_relative_gate_max_frames",s->gate_max);id(j,"region_relative_last_note_off_frame",s->last_note_off);
    cJSON_AddNumberToObject(j,"region_automation_lanes",s->region_automation_lanes);cJSON_AddNumberToObject(j,"inherited_track_automation_lanes",s->track_automation_lanes);cJSON_AddStringToObject(j,"gate_semantics","authored note duration clipped at region right edge; does not resolve mono/choke/pedal/per-note automation");
    cJSON* params=cJSON_AddObjectToObject(j,"effective_base_parameters");cJSON* supported=cJSON_AddArrayToObject(j,"supported_controls");cJSON* units=cJSON_AddObjectToObject(j,"parameter_units");
    for(int p=0;p<ENGINE_INSTRUMENT_PARAM_COUNT;++p){EngineInstrumentParamSpec spec;if(!engine_instrument_param_spec(p,&spec))continue;cJSON_AddNumberToObject(params,spec.id,engine_instrument_params_get(s->params,p));cJSON_AddStringToObject(units,spec.id,spec.unit ? spec.unit : "");if(engine_instrument_preset_param_active(s->preset,p))cJSON_AddItemToArray(supported,cJSON_CreateString(spec.id));}
}
// Returns immutable bounded pages even after current project state has changed.
static cJSON* read_capture(DawControlBridge* bridge,const cJSON* request) {
    const char* token=text(request,"capture_id");DawMixCapture* capture=NULL;
    for(int i=0;token && i<DAW_CONTROL_MIX_STATES;++i)if(bridge->mix_captures[i].capture && !strcmp(token,bridge->mix_captures[i].token))capture=bridge->mix_captures[i].capture;
    if(!capture)return reply(false,"expired_mix_capture");
    if(capture->whole_song){int node,unused;if(!offset(request,"node_offset",capture->snapshot->tracks,&node))return reply(false,"invalid_request");const char* fields[]={"source_offset","effect_offset","frequency_window","frequency_channel","note_offset"};for(int i=0;i<5;++i)if(!offset(request,fields[i],0,&unused))return reply(false,"invalid_request");cJSON* j=reply(true,"song_loudness_page");describe(j,token,capture);cJSON_AddNumberToObject(j,"node_offset",node);cJSON_AddNumberToObject(j,"next_node_offset",node<capture->snapshot->tracks?node+1:-1);node_json(cJSON_AddObjectToObject(j,"node"),&capture->snapshot->nodes[node]);daw_control_mix_loudness_json(cJSON_AddObjectToObject(j,"loudness"),capture,node);return j;}
    int node,source,effect,window,channel,note;if(!offset(request,"node_offset",capture->snapshot->tracks,&node) || !offset(request,"source_offset",capture->snapshot->sources,&source) || !offset(request,"effect_offset",FX_MASTER_MAX-1,&effect) || !offset(request,"frequency_window",DAW_FREQUENCY_WINDOWS-1,&window) || !offset(request,"frequency_channel",capture->channels-1,&channel) || !offset(request,"note_offset",capture->tails->count,&note))return reply(false,"invalid_request");
    cJSON* j=reply(true,"mix_capture_page");describe(j,token,capture);cJSON_AddNumberToObject(j,"node_offset",node);cJSON_AddNumberToObject(j,"next_node_offset",node<capture->snapshot->tracks?node+1:-1);
    cJSON* row=cJSON_AddObjectToObject(j,"node");const DawMixNode* n=&capture->snapshot->nodes[node];node_json(row,n);daw_control_fx_values_json(cJSON_AddObjectToObject(row,"inserts"),&n->fx.chain);eq_json(cJSON_AddObjectToObject(row,"dedicated_eq"),&n->eq.curve);
    cJSON* stages=cJSON_AddArrayToObject(row,"stages");for(int st=0;st<ENGINE_MIX_STAGE_COUNT;++st){cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(stages,item);stage_json(item,n,st,&capture->stages[node][st],capture->channels);}
    if(effect>=n->fx.chain.count && effect!=0){cJSON_Delete(j);return reply(false,"invalid_request");}
    cJSON_AddNumberToObject(j,"effect_count",n->fx.chain.count);cJSON_AddNumberToObject(j,"effect_offset",effect);cJSON_AddNumberToObject(j,"next_effect_offset",effect+1<n->fx.chain.count?effect+1:-1);
    if(n->fx.chain.count)effect_json(cJSON_AddObjectToObject(j,"effect"),n,&capture->effects[node][effect],effect,capture->channels);else cJSON_AddNullToObject(j,"effect");
    daw_control_mix_loudness_json(cJSON_AddObjectToObject(j,"loudness"),capture,node);
    daw_control_mix_frequency_json(cJSON_AddObjectToObject(j,"frequency"),capture,node,window,channel);
    cJSON* regions=cJSON_AddArrayToObject(j,"sources");int end=source+8;if(end>capture->snapshot->sources)end=capture->snapshot->sources;
    for(int i=source;i<end;++i){cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(regions,item);source_json(item,&capture->snapshot->regions[i]);daw_source_tail_json(cJSON_AddObjectToObject(item,"source_tail"),capture,i);}
    cJSON_AddNumberToObject(j,"note_count",capture->tails->count);cJSON_AddNumberToObject(j,"note_offset",note);int note_end=note+8;if(note_end>capture->tails->count)note_end=capture->tails->count;cJSON_AddNumberToObject(j,"next_note_offset",note_end<capture->tails->count?note_end:-1);cJSON* notes=cJSON_AddArrayToObject(j,"note_endings");for(int i=note;i<note_end;++i){cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(notes,item);daw_note_ending_json(item,capture,i);}
    cJSON_AddNumberToObject(j,"source_offset",source);cJSON_AddNumberToObject(j,"next_source_offset",end<capture->snapshot->sources?end:-1);return j;
}
// Routes read-only exact guards, full-context capture and endpoint-lifetime immutable pages.
cJSON* daw_control_mix_capture(DawControlBridge* bridge,AppState* state,const cJSON* request) {
    const char* op=text(request,"op");if(!strcmp(op,"get_mix_capture"))return read_capture(bridge,request);
    if(bridge->mix_sequence==UINT64_MAX)return reply(false,"capture_capacity_reached");
    if(!strcmp(op,"get_mix_snapshot")){DawMixSnapshot* snapshot=NULL;DawActionStatus status=daw_action_get_mix_snapshot(state,&snapshot);if(status!=DAW_ACTION_UNCHANGED)return reply(false,"mix_snapshot_unavailable");
        DawControlMixGuard* slot=&bridge->mix_guards[bridge->mix_guard_cursor++%DAW_CONTROL_MIX_STATES];daw_mix_snapshot_free(slot->snapshot);slot->snapshot=snapshot;snprintf(slot->token,sizeof(slot->token),"%s:mix-state:%" PRIu64,bridge->instance,++bridge->mix_sequence);
        cJSON* j=reply(true,"mix_snapshot");cJSON_AddStringToObject(j,"expected_mix",slot->token);cJSON_AddNumberToObject(j,"sample_rate",engine_get_config(state->engine)->sample_rate);id(j,"generation",snapshot->generation);id(j,"settings_fingerprint_fnv1a64",snapshot->settings_fingerprint);cJSON_AddNumberToObject(j,"node_count",snapshot->tracks+1);cJSON_AddNumberToObject(j,"source_count",snapshot->sources);
        cJSON* nodes=cJSON_AddArrayToObject(j,"nodes");for(int n=0;n<=snapshot->tracks;++n){cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(nodes,item);node_json(item,&snapshot->nodes[n]);}return j;
    }
    const char* expected=text(request,"expected_mix");DawMixSnapshot* snapshot=NULL;for(int i=0;expected && i<DAW_CONTROL_MIX_STATES;++i)if(bridge->mix_guards[i].snapshot && !strcmp(expected,bridge->mix_guards[i].token))snapshot=bridge->mix_guards[i].snapshot;
    if(!snapshot)return reply(false,"expired_mix_state");uint64_t start,end;if(!frame(request,"start_frame",&start) || !frame(request,"end_frame",&end))return reply(false,"invalid_request");
    const cJSON* mode=cJSON_GetObjectItemCaseSensitive(request,"whole_song");if(mode && !cJSON_IsBool(mode))return reply(false,"invalid_request");bool whole=cJSON_IsTrue(mode);uint64_t tail=0;const cJSON* tail_item=cJSON_GetObjectItemCaseSensitive(request,"tail_frames");if(tail_item && !frame(request,"tail_frames",&tail))return reply(false,"invalid_request");if((whole && (start!=0 || end!=1)) || (!whole && tail))return reply(false,"invalid_request");
    DawMixCapture* capture=NULL;DawActionStatus status=whole?daw_action_capture_song_loudness(state,snapshot,tail,&capture):daw_action_capture_mix(state,snapshot,start,end,&capture);if(status!=DAW_ACTION_UNCHANGED){const char* codes[]={"applied","unchanged","invalid_request","wrong_thread","state_conflict","busy","history_unavailable","engine_rejected"};return reply(false,codes[status]);}
    DawControlMixCapture* slot=&bridge->mix_captures[bridge->mix_capture_cursor++%DAW_CONTROL_MIX_STATES];daw_mix_capture_free(slot->capture);slot->capture=capture;snprintf(slot->token,sizeof(slot->token),"%s:mix-capture:%" PRIu64,bridge->instance,++bridge->mix_sequence);
    cJSON* j=reply(true,"mix_captured");describe(j,slot->token,capture);return j;
}
