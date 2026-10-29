#include "daw/mix_capture.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Hashes only explicitly supplied semantic values as a noncryptographic settings identifier.
static void hash(uint64_t* h,const void* data,size_t count) {
    const unsigned char* p=data;while(count--){*h^=*p++;*h*=UINT64_C(1099511628211);}
}
#define HASH(h,v) hash((h),&(v),sizeof(v))
// Includes every authored mixer curve without hashing unused capacity or struct padding.
static void hash_mixer(uint64_t* h,const EngineMixerAutomation* a) {HASH(h,a->count);HASH(h,a->before);HASH(h,a->after);for(int i=0;i<a->count;++i){HASH(h,a->points[i].frame);HASH(h,a->points[i].value);HASH(h,a->points[i].curve);}}
// Includes supported and unsupported base parameters so the settings identifier describes exact storage.
static void hash_params(uint64_t* h,EngineInstrumentParams params) {
    for(int p=0;p<ENGINE_INSTRUMENT_PARAM_COUNT;++p){float value=engine_instrument_params_get(params,p);HASH(h,value);}
}
// Includes automation semantics and every point without hashing padding, capacities or pointers.
static void hash_lanes(uint64_t* h,const EngineAutomationLane* lanes,int count) {
    HASH(h,count);for(int i=0;i<count;++i){const EngineAutomationLane* l=&lanes[i];HASH(h,l->target);HASH(h,l->domain);HASH(h,l->before);HASH(h,l->after);HASH(h,l->point_count);
        for(int j=0;j<l->point_count;++j){HASH(h,l->points[j].frame);HASH(h,l->points[j].value);HASH(h,l->points[j].curve);}}
}
// Includes complete EQ configuration independently of transient filter histories.
static void hash_eq(uint64_t* h,const EngineEqCurve* e) {
    HASH(h,e->low_cut.enabled);HASH(h,e->low_cut.freq_hz);HASH(h,e->high_cut.enabled);HASH(h,e->high_cut.freq_hz);
    for(int b=0;b<ENGINE_EQ_BANDS;++b){HASH(h,e->bands[b].enabled);HASH(h,e->bands[b].freq_hz);HASH(h,e->bands[b].gain_db);HASH(h,e->bands[b].q_width);}
}
// Copies complete bounded source/configuration state through the existing common arrangement guard.
DawActionStatus daw_action_get_mix_snapshot(const AppState* state,DawMixSnapshot** out) {
    if(!state || !state->engine || !out || *out)return DAW_ACTION_INVALID;
    if(!engine_is_control_thread(state->engine))return DAW_ACTION_WRONG_THREAD;
    int tracks=engine_get_track_count(state->engine);if(tracks<1 || tracks>DAW_MIX_TRACKS)return DAW_ACTION_INVALID;
    int points=0,clips=0;const EngineTrack* bounded=engine_get_tracks(state->engine);
    for(int t=0;t<tracks;++t){if(bounded[t].clip_count<0 || bounded[t].clip_count>DAW_ARRANGEMENT_CLIP_CAP-clips)return DAW_ACTION_INVALID;clips+=bounded[t].clip_count;
        int count=bounded[t].midi_instrument_automation_lane_count;
        if(count<0 || count>ENGINE_AUTOMATION_TARGET_COUNT)return DAW_ACTION_INVALID;
        for(int i=0;i<count;++i){int n=bounded[t].midi_instrument_automation_lanes[i].point_count;if(n<0 || n>DAW_ARRANGEMENT_POINT_CAP-points)return DAW_ACTION_INVALID;points+=n;}
        for(int c=0;c<bounded[t].clip_count;++c){const EngineClip* clip=&bounded[t].clips[c];if(clip->automation_lane_count<0 || clip->automation_lane_count>ENGINE_AUTOMATION_TARGET_COUNT)return DAW_ACTION_INVALID;
            for(int i=0;i<clip->automation_lane_count;++i){int n=clip->automation_lanes[i].point_count;if(n<0 || n>DAW_ARRANGEMENT_POINT_CAP-points)return DAW_ACTION_INVALID;points+=n;}}}
    DawMixSnapshot* s=calloc(1,sizeof(*s));if(!s)return DAW_ACTION_ENGINE_REJECTED;
    s->tracks=tracks;s->generation=engine_instance_id(state->engine);s->settings_fingerprint=UINT64_C(14695981039346656037);
    if(daw_action_get_arrangement(state,&s->arrangement).status!=DAW_MIDI_UNCHANGED)goto fail;
    const EngineTrack* rows=engine_get_tracks(state->engine);uint64_t* h=&s->settings_fingerprint;HASH(h,s->generation);HASH(h,tracks);
    for(int i=0;i<=tracks;++i){DawMixNode* n=&s->nodes[i];n->track_id=i<tracks ? rows[i].runtime_id : 0;
        if(i<tracks){const EngineTrack* t=&rows[i];n->parent_id=t->parent_id;n->group=t->is_group;n->returns=t->is_return;n->active=t->active;n->muted=t->muted;n->solo=t->solo;n->gain=t->gain;n->pan=t->pan;
            n->volume_automation=t->volume_automation;
            memcpy(n->sends,t->sends,sizeof(n->sends));snprintf(n->name,sizeof(n->name),"%s",t->name);}
        else {n->active=true;n->gain=1;snprintf(n->name,sizeof(n->name),"Master");}
        if(daw_action_get_fx(state,s->generation,n->track_id,&n->fx)!=DAW_ACTION_UNCHANGED || daw_eq_get(state,s->generation,n->track_id,&n->eq)!=DAW_ACTION_UNCHANGED)goto fail;
        HASH(h,n->track_id);HASH(h,n->parent_id);HASH(h,n->group);HASH(h,n->returns);HASH(h,n->active);HASH(h,n->muted);HASH(h,n->solo);HASH(h,n->gain);HASH(h,n->pan);hash(h,n->name,strlen(n->name));
        for(int j=0;j<ENGINE_SEND_CAP;++j){HASH(h,n->sends[j].return_id);HASH(h,n->sends[j].gain);}
        hash_mixer(h,&n->volume_automation);for(int j=0;j<ENGINE_SEND_CAP;++j)hash_mixer(h,&n->sends[j].automation);
        HASH(h,n->fx.chain.count);for(int j=0;j<n->fx.chain.count;++j){const FxMasterInstanceInfo* f=&n->fx.chain.items[j];HASH(h,f->id);HASH(h,f->type);HASH(h,f->enabled);HASH(h,f->param_count);HASH(h,f->automation.count);HASH(h,f->automation.before);HASH(h,f->automation.after);for(int a=0;a<f->automation.count;++a){HASH(h,f->automation.points[a].frame);HASH(h,f->automation.points[a].value);HASH(h,f->automation.points[a].curve);}
            for(unsigned p=0;p<f->param_count;++p){HASH(h,f->params[p]);HASH(h,f->param_mode[p]);HASH(h,f->param_beats[p]);}}
        hash_eq(h,&n->eq.curve);
        if(i==tracks)continue;
        const EngineTrack* t=&rows[i];HASH(h,t->midi_instrument_enabled);HASH(h,t->midi_instrument_preset);hash_params(h,t->midi_instrument_params);hash_lanes(h,t->midi_instrument_automation_lanes,t->midi_instrument_automation_lane_count);
        for(int c=0;c<t->clip_count;++c){const EngineClip* clip=&t->clips[c];if(!clip->active)continue;if(s->sources>=DAW_ARRANGEMENT_CLIP_CAP)goto fail;
            DawMixSource* source=&s->regions[s->sources++];source->track_id=t->runtime_id;source->clip_id=clip->creation_index;source->start=clip->timeline_start_frames;source->duration=clip->duration_frames;source->gain=clip->gain;
            source->midi=clip->kind==ENGINE_CLIP_KIND_MIDI;source->inherited=source->midi && engine_clip_midi_inherits_track_instrument(clip);source->region_automation_lanes=clip->automation_lane_count;
            source->track_automation_lanes=source->inherited ? t->midi_instrument_automation_lane_count : 0;snprintf(source->name,sizeof(source->name),"%s",clip->name);
            HASH(h,source->clip_id);HASH(h,clip->kind);HASH(h,clip->timeline_start_frames);HASH(h,clip->duration_frames);HASH(h,clip->offset_frames);HASH(h,clip->gain);HASH(h,clip->fade_in_frames);HASH(h,clip->fade_out_frames);HASH(h,clip->fade_in_curve);HASH(h,clip->fade_out_curve);
            hash_lanes(h,clip->automation_lanes,clip->automation_lane_count);
            if(source->midi){source->preset=engine_clip_midi_effective_instrument_preset(state->engine,i,c);source->params=engine_clip_midi_effective_instrument_params(state->engine,i,c);source->notes=clip->midi_notes.note_count;
                HASH(h,source->preset);hash_params(h,source->params);HASH(h,source->inherited);HASH(h,source->notes);
                for(int j=0;j<source->notes;++j){const EngineMidiNote* note=&clip->midi_notes.notes[j];HASH(h,note->start_frame);HASH(h,note->duration_frames);HASH(h,note->note);HASH(h,note->velocity);
                    uint64_t gate=note->duration_frames;if(note->start_frame>=clip->duration_frames)gate=0;else if(gate>clip->duration_frames-note->start_frame)gate=clip->duration_frames-note->start_frame;
                    if(!j || gate<source->gate_min)source->gate_min=gate;if(gate>source->gate_max)source->gate_max=gate;
                    if(note->start_frame+gate>source->last_note_off)source->last_note_off=note->start_frame+gate;}}
        }
    }
    hash(h,&s->arrangement.layout,sizeof(s->arrangement.layout));
    *out=s;return DAW_ACTION_UNCHANGED;
fail: daw_mix_snapshot_free(s);return DAW_ACTION_ENGINE_REJECTED;
}
// Checks exact common content and every node scalar/routing/processing field without a hash guard.
bool daw_mix_snapshot_current(const AppState* state,const DawMixSnapshot* s) {
    if(!s || !state || !state->engine || s->generation!=engine_instance_id(state->engine) || s->tracks!=engine_get_track_count(state->engine) || !daw_arrangement_snapshot_current(state,&s->arrangement))return false;
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    for(int i=0;i<=s->tracks;++i){const DawMixNode* n=&s->nodes[i];DawFxSnapshot fx;DawEqSnapshot eq;
        if(daw_action_get_fx(state,s->generation,n->track_id,&fx)!=DAW_ACTION_UNCHANGED || !daw_fx_snapshot_equal(&fx,&n->fx) || daw_eq_get(state,s->generation,n->track_id,&eq)!=DAW_ACTION_UNCHANGED || !daw_eq_equal(&eq,&n->eq))return false;
        if(i<s->tracks){const EngineTrack* t=&tracks[i];if(t->runtime_id!=n->track_id || t->parent_id!=n->parent_id || t->is_group!=n->group || t->is_return!=n->returns || t->active!=n->active || t->muted!=n->muted || t->solo!=n->solo || t->gain!=n->gain || t->pan!=n->pan || strcmp(t->name,n->name))return false;
            if(!engine_mixer_automation_equal(&t->volume_automation,&n->volume_automation))return false;
            for(int j=0;j<ENGINE_SEND_CAP;++j)if(!engine_mixer_automation_equal(&t->sends[j].automation,&n->sends[j].automation) || t->sends[j].return_id!=n->sends[j].return_id || t->sends[j].gain!=n->sends[j].gain)return false;}
    }
    return true;
}
// Releases only independently captured content and metadata.
void daw_mix_snapshot_free(DawMixSnapshot* snapshot) {if(!snapshot)return;daw_arrangement_snapshot_free(&snapshot->arrangement);free(snapshot);}
// Assigns each tap sample to its own latency-corrected authored frame, rejecting nonfinite input.
static bool observe(void* user,int track,EngineMixProbeStage stage,uint64_t block,uint64_t latency,const float* samples,int frames,int channels) {
    DawMixCapture* capture=user;int node=track<0 ? capture->snapshot->tracks : track;if(node<0 || node>capture->snapshot->tracks || channels!=capture->channels)return false;
    DawMixStage* out=&capture->stages[node][stage];if(out->available && out->latency!=latency)return false;out->available=true;out->latency=latency;
    if(capture->whole_song){if((track>=0 && stage==ENGINE_MIX_OUTPUT) || (track<0 && stage==ENGINE_MIX_SANITIZED))return daw_mix_loudness_observe(capture,node,block,latency,samples,frames,channels);return true;}
    uint64_t length=capture->end-capture->start;
    for(int f=0;f<frames;++f){uint64_t rendered=block+(uint64_t)f;if(rendered<latency)continue;uint64_t nominal=rendered-latency;if(nominal<capture->start || nominal>=capture->end)continue;
        int bin=(int)((nominal-capture->start)*DAW_MIX_BINS/length);
        for(int ch=0;ch<channels;++ch){double v=samples[f*channels+ch];if(!isfinite(v))return false;double a=fabs(v);DawMixMeter* m=&out->channel[ch];if(a>m->peak)m->peak=a;m->power+=v*v;++m->samples;if(a>=DAW_TAIL_THRESHOLD){if(!m->active_samples)m->first_active_frame=nominal;m->last_active_frame=nominal;++m->active_samples;}if(a>=1)++m->full_scale;if(a>1)++m->overload;
            if(a>m->bin_peak[bin])m->bin_peak[bin]=a;m->bin_power[bin]+=v*v;++m->bin_samples[bin];}}
    if((track>=0 && stage==ENGINE_MIX_OUTPUT) || (track<0 && stage==ENGINE_MIX_SANITIZED))return (capture->whole_song || daw_mix_frequency_observe(capture,node,block,latency,samples,frames,channels)) && daw_mix_loudness_observe(capture,node,block,latency,samples,frames,channels);
    return true;
}
// Captures full preceding context cold from frame zero, leaving live transport/mixer/DSP untouched.
static DawActionStatus capture_mix(AppState* state,const DawMixSnapshot* expected,uint64_t start,uint64_t end,bool whole_song,uint64_t authored_end,DawMixCapture** out) {
    if(!state || !state->engine || !expected || !out || *out)return DAW_ACTION_INVALID;if(!engine_is_control_thread(state->engine))return DAW_ACTION_WRONG_THREAD;
    const EngineRuntimeConfig* config=engine_get_config(state->engine);int rate=config->sample_rate,channels=engine_graph_get_channels(state->engine->graph),block=config->block_size;
    if(rate<1 || channels<1 || channels>DAW_MIX_CHANNELS || block<1 || end<=start || (whole_song ? (start!=0 || authored_end>(uint64_t)rate*DAW_LOUDNESS_PROGRAM_SECONDS || end-authored_end>(uint64_t)rate*DAW_LOUDNESS_TAIL_SECONDS || (channels!=1 && channels!=2) || (rate!=44100 && rate!=48000 && rate!=96000)) : (end>(uint64_t)rate*DAW_MIX_CONTEXT_SECONDS || end-start>(uint64_t)rate*DAW_MIX_RANGE_SECONDS || end>UINT64_C(57600000))))return DAW_ACTION_INVALID;
    if(state->bounce_active || state->undo.active_drag_valid || daw_audio_recording_is_active(&state->audio_recording))return DAW_ACTION_BUSY;
    if(!daw_mix_snapshot_current(state,expected))return DAW_ACTION_STALE_TARGET;
    DawMixCapture* capture=calloc(1,sizeof(*capture));if(!capture)return DAW_ACTION_ENGINE_REJECTED;
    if(daw_action_get_mix_snapshot(state,&capture->snapshot)!=DAW_ACTION_UNCHANGED){daw_mix_capture_free(capture);return DAW_ACTION_ENGINE_REJECTED;}
    capture->whole_song=whole_song;capture->authored_end=authored_end;capture->tail_frames=whole_song?end-authored_end:0;
    capture->start=start;capture->end=end;capture->channels=channels;capture->sample_rate=rate;capture->block_frames=block;
    EngineSourcePlan* plan=engine_source_plan_prepare_export(state->engine,end+(uint64_t)rate*60+(uint64_t)block,0);float* samples=NULL;float* scratch=NULL;bool ok=false;
    if(!plan || (!whole_song && !daw_mix_frequency_prepare(capture)) || !daw_mix_loudness_prepare(capture))goto done;capture->master_latency=engine_source_plan_export_latency(plan);
    if(capture->master_latency==UINT64_MAX || capture->master_latency>(uint64_t)rate*60)goto done;
    samples=calloc((size_t)block*channels,sizeof(float));scratch=calloc((size_t)block*channels,sizeof(float));if(!samples || !scratch)goto done;
    for(int n=0;n<=capture->snapshot->tracks;++n) {
        int count=capture->snapshot->nodes[n].fx.chain.count;
        if(count && !whole_song){capture->effects[n]=calloc((size_t)count,sizeof(DawMixEffect));if(!capture->effects[n])goto done;}
    }
    if(!whole_song && !daw_note_tails_prepare(state,capture))goto done;
    EngineMixProbe probe={.user=capture,.observe=observe,.effect=whole_song?NULL:daw_mix_observe_effect,.prepare_source=whole_song?NULL:daw_note_tails_source,.source=whole_song?NULL:daw_note_tails_observe};if(!engine_source_plan_set_probe(plan,&probe))goto done;
    uint64_t total=((end+capture->master_latency+(uint64_t)block-1)/(uint64_t)block)*(uint64_t)block;
    for(uint64_t position=0;position<total;){int count=block;
        if(!engine_source_plan_render_export(plan,position,count,samples,scratch,channels))goto done;position+=count;capture->rendered_frames=position;}
    for(int n=0;!whole_song && n<=capture->snapshot->tracks;++n)for(int st=0;st<ENGINE_MIX_STAGE_COUNT;++st){DawMixStage* stage=&capture->stages[n][st];if(n<capture->snapshot->tracks && st==ENGINE_MIX_SANITIZED)continue;
        if(!stage->available)goto done;for(int ch=0;ch<channels;++ch)if(stage->channel[ch].samples!=end-start)goto done;}
    for(int n=0;!whole_song && n<=capture->snapshot->tracks;++n)for(int e=0;e<capture->snapshot->nodes[n].fx.chain.count;++e) {
        DawMixEffect* effect=&capture->effects[n][e];
        for(int side=0;side<2;++side)for(int ch=0;ch<channels;++ch)if(!effect->boundary[side].available || effect->boundary[side].channel[ch].samples!=end-start)goto done;
        if(effect->reduction_available)for(int ch=0;ch<channels;++ch)if(effect->reduction[ch].samples!=end-start)goto done;
    }
    ok=!probe.failed && (whole_song || daw_mix_frequency_finish(capture)) && daw_mix_loudness_finish(capture) && daw_mix_snapshot_current(state,expected);
done: free(samples);free(scratch);engine_source_plan_discard(state->engine,plan);
    if(!ok){daw_mix_capture_free(capture);return DAW_ACTION_ENGINE_REJECTED;}*out=capture;return DAW_ACTION_UNCHANGED;
}
// Frees immutable captured measurements after endpoint or test consumers finish.
void daw_mix_capture_free(DawMixCapture* capture) {if(!capture)return;if(capture->loudness){for(int n=0;n<=capture->snapshot->tracks;++n)daw_loudness_free(&capture->loudness[n]);free(capture->loudness);}if(capture->tails){free(capture->tails->notes);free(capture->tails);}daw_mix_frequency_free(&capture->frequency);for(int n=0;n<=DAW_MIX_TRACKS;++n)free(capture->effects[n]);daw_mix_snapshot_free(capture->snapshot);free(capture);}

// Retains the original detailed selected-passage diagnostic contract.
DawActionStatus daw_action_capture_mix(AppState* state,const DawMixSnapshot* expected,uint64_t start,uint64_t end,DawMixCapture** out) {return capture_mix(state,expected,start,end,false,0,out);}
// Derives the complete authored endpoint from the exact snapshot and adds only the explicitly requested tail.
DawActionStatus daw_action_capture_song_loudness(AppState* state,const DawMixSnapshot* expected,uint64_t tail,DawMixCapture** out) {
    if(!expected)return DAW_ACTION_INVALID;uint64_t end=0;
    for(int i=0;i<expected->sources;++i){const DawMixSource* s=&expected->regions[i];if(s->duration>UINT64_MAX-s->start)return DAW_ACTION_INVALID;uint64_t stop=s->start+s->duration;if(stop>end)end=stop;}
    if(!end || tail>UINT64_MAX-end)return DAW_ACTION_INVALID;
    return capture_mix(state,expected,0,end+tail,true,end,out);
}
