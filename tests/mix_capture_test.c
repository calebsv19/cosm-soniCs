#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "daw/mix_capture.h"
#include "daw/routing_actions.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x,m) daw_test_expect("mix_capture_test",!!(x),(m))
// Constructs a private engine with complete common-action timing metadata and dummy output.
static AppState* state(void) {
    AppState* s=calloc(1,sizeof(*s));config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);CHECK(s->engine,"engine");undo_manager_init(&s->undo);s->tempo=tempo_state_default(48000);tempo_map_init(&s->tempo_map,48000);time_signature_map_init(&s->time_signature_map);s->selected_track_index=s->selected_clip_index=-1;return s;
}
// Releases the private fixture without modifying any saved project or runtime configuration.
static void release(AppState* s) {engine_destroy(s->engine);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);}
// Performs one exact guarded range capture and proves it preserves common snapshot/history ownership.
static DawMixCapture* capture(AppState* s,uint64_t start,uint64_t end) {
    DawMixSnapshot* guard=NULL;CHECK(daw_action_get_mix_snapshot(s,&guard)==DAW_ACTION_UNCHANGED,"snapshot");DawMixCapture* c=NULL;int history=s->undo.undo_count;EngineSourcePlan* active=s->engine->active_source_plan;
    CHECK(daw_action_capture_mix(s,guard,start,end,&c)==DAW_ACTION_UNCHANGED && c,"capture");CHECK(daw_mix_snapshot_current(s,guard) && s->undo.undo_count==history && s->engine->active_source_plan==active && s->selected_track_index==-1,"preserved authored/selection/plan/history");daw_mix_snapshot_free(guard);return c;
}
// Compares calibrated channel peak/RMS against a known in-context linear route factor.
static void level(const DawMixCapture* c,int node,int stage,double left,double right) {
    const DawMixStage* s=&c->stages[node][stage];CHECK(s->available,"available stage");double factors[]={left,right};
    for(int ch=0;ch<2;++ch){const DawMixMeter* m=&s->channel[ch];CHECK(m->samples==c->end-c->start,"complete stage samples");CHECK(fabs(m->peak-factors[ch])<2e-6,"peak calibration");CHECK(fabs(sqrt(m->power/m->samples)-factors[ch]/sqrt(2))<2e-6,"RMS calibration");uint64_t count=0;for(int b=0;b<DAW_MIX_BINS;++b)count+=m->bin_samples[b];CHECK(count==m->samples,"bucket accounting");if(stage==ENGINE_MIX_OUTPUT || stage==ENGINE_MIX_SANITIZED){double sum=0;const float* p=c->frequency.powers+((size_t)node*c->channels+ch)*DAW_FREQUENCY_WINDOWS*DAW_FREQUENCY_BINS;for(int k=0;k<DAW_FREQUENCY_BINS;++k)sum+=p[k];CHECK(fabs(sum-factors[ch]*factors[ch]/2)<2e-6,"group/return/master spectral power calibration");}}
}
// Compares master samples against the ordinary production bounce using the same cold full context.
static void master_parity(AppState* s,const DawMixCapture* c) {
    EngineBounceOptions options=engine_bounce_options_default();options.normalize_if_clipping=false;EngineBounceBuffer audio={0};CHECK(engine_bounce_range_to_buffer_with_options(s->engine,0,c->rendered_frames,&options,NULL,NULL,&audio),"ordinary bounce");
    if(!c->whole_song)for(int ch=0;ch<2;++ch){double power=0,peak=0;for(uint64_t f=c->start;f<c->end;++f){double v=audio.data[f*2+ch];power+=v*v;if(fabs(v)>peak)peak=fabs(v);}const DawMixMeter* m=&c->stages[c->snapshot->tracks][ENGINE_MIX_SANITIZED].channel[ch];CHECK(fabs(m->peak-peak)<1e-7 && fabs(m->power-power)<1e-7,"exact production master accounting");}
    if(c->frequency.frames)for(int window=0;window<32;window+=15)for(int ch=0;ch<2;++ch){double power=0,weights=0,spectral=0;
        for(int f=0;f<c->frequency.frames;++f){double w=.5-.5*cos(2*M_PI*f/c->frequency.frames),v=audio.data[(c->frequency.start[window]+f)*2+ch];power+=v*v*w*w;weights+=w*w;}
        const float* bins=c->frequency.powers+((size_t)c->snapshot->tracks*2+ch)*32*DAW_FREQUENCY_BINS+window*DAW_FREQUENCY_BINS;
        for(int k=0;k<DAW_FREQUENCY_BINS;++k)spectral+=bins[k];CHECK(fabs(spectral-power/weights)<1e-7,"spectral master matches independent weighted ordinary bounce through nonlinear inserts and graph delay");
    }
    DawLoudness measured={0};CHECK(daw_loudness_init(&measured,c->sample_rate,2),"ordinary bounce meter init");if(c->whole_song){measured.programme=true;measured.short_terms=calloc(DAW_LOUDNESS_SHORT_TERMS,sizeof(double));CHECK(measured.short_terms,"ordinary programme LRA storage");}CHECK(daw_loudness_add(&measured,audio.data+c->start*2,(int)(c->end-c->start)) && daw_loudness_finish(&measured),"ordinary bounce loudness meter");
    const DawLoudness* actual=&c->loudness[c->snapshot->tracks];
    CHECK((!isfinite(measured.integrated) && !isfinite(actual->integrated)) || fabs(measured.integrated-actual->integrated)<1e-8,"latency-corrected production master LUFS equals ordinary bounce");
    for(int ch=0;ch<2;++ch)CHECK(fabs(measured.true_peak[ch]-actual->true_peak[ch])<1e-8,"latency-corrected production master true peak equals ordinary bounce");
    engine_bounce_buffer_free(&audio);
}
// Qualifies actual per-sample attenuation against a steady analytic compressor and a linked limiter.
static void effect_calibration(const char* path) {
    AppState* s=state();float samples[4096*2];for(int f=0;f<4096;++f){samples[f*2]=.5f;samples[f*2+1]=.125f;}
    EngineBounceBuffer b={samples,4096,2,48000};CHECK(engine_bounce_write_wav(&b,path,ENGINE_BOUNCE_WAV_FLOAT32) && engine_add_clip_to_track(s->engine,0,path,0,NULL),"constant fixture");
    FxInstId comp=engine_fx_track_add(s->engine,0,20);CHECK(comp && engine_fx_track_set_param(s->engine,0,comp,0,-12) && engine_fx_track_set_param(s->engine,0,comp,2,.1) && engine_fx_track_set_param(s->engine,0,comp,4,6) && engine_fx_track_set_param(s->engine,0,comp,5,0) && engine_fx_track_set_param(s->engine,0,comp,6,0),"compressor settings");
    DawMixCapture* c=capture(s,1001,2001);DawMixEffect* e=&c->effects[0][0];double expected=(-12+ (20*log10(.5)+12)/4)-20*log10(.5);
    CHECK(e->reduction_available && e->reduction[0].samples==1000 && fabs(e->reduction[0].sum_db/1000-expected)<1e-4 && e->reduction[1].minimum_db==0,"analytic per-channel attenuation excludes makeup");
    CHECK(e->boundary[1].channel[0].peak>e->boundary[0].channel[0].peak && e->reduction[0].minimum_db<0,"makeup raises output while actual reduction stays negative");
    for(int ch=0;ch<2;++ch){uint64_t count=0;for(int bin=0;bin<DAW_MIX_BINS;++bin)count+=e->reduction[ch].bin_samples[bin];CHECK(count==1000,"nonaligned gain buckets");}
    master_parity(s,c);daw_mix_capture_free(c);CHECK(engine_fx_track_set_enabled(s->engine,0,comp,false),"bypass");c=capture(s,1001,2001);e=&c->effects[0][0];CHECK(e->reduction[0].minimum_db==0 && e->reduction[0].samples==1000 && e->boundary[0].channel[0].power==e->boundary[1].channel[0].power,"disabled trace zero with exact bypass parity");daw_mix_capture_free(c);
    FxInstId lim=engine_fx_track_add(s->engine,0,21);CHECK(lim && engine_fx_track_set_param(s->engine,0,lim,0,-12) && engine_fx_track_set_param(s->engine,0,lim,1,3),"lookahead limiter");
    c=capture(s,1001,2001);e=&c->effects[0][1];double gain=pow(10,-12./20)/.5;CHECK(e->boundary[0].latency==0 && e->boundary[1].latency==144 && fabs(e->reduction[0].minimum_db-20*log10(gain))<1e-5 && e->reduction[0].sum_db==e->reduction[1].sum_db,"linked limiter gain output-aligned across block boundaries");
    CHECK(fabs(e->boundary[1].channel[0].peak-pow(10,-12./20))<1e-6,"known limiter ceiling");master_parity(s,c);daw_mix_capture_free(c);release(s);
    s=state();for(int f=0;f<4096;++f){samples[f*2]=samples[f*2+1]=.125f;}samples[4000]=samples[4001]=.5f;
    CHECK(engine_bounce_write_wav(&b,path,ENGINE_BOUNCE_WAV_FLOAT32) && engine_add_clip_to_track(s->engine,0,path,0,NULL),"future transient source");
    lim=engine_fx_track_add(s->engine,0,21);CHECK(lim && engine_fx_track_set_param(s->engine,0,lim,0,-12) && engine_fx_track_set_param(s->engine,0,lim,1,3),"future transient limiter");
    c=capture(s,1855,1856);CHECK(c->effects[0][0].reduction[0].samples==1 && c->effects[0][0].reduction[0].minimum_db==0,"lookahead reduction starts at exact sample, not whole block summary");daw_mix_capture_free(c);
    c=capture(s,1856,1857);CHECK(fabs(c->effects[0][0].reduction[0].minimum_db-20*log10(gain))<1e-5 && fabs(c->effects[0][0].boundary[1].channel[0].peak-.125*gain)<1e-6,"future input aligns applied reduction to delayed output sample");master_parity(s,c);daw_mix_capture_free(c);release(s);
}
// Checks Parseval calibration independently for tone, silence, DC, Nyquist, noise and anti-phase channels.
static void frequency_calibration(void) {
    const int rates[]={22050,44100,48000,96000,192000};
    for(int rate_index=0;rate_index<5;++rate_index)for(int kind=0;kind<5;++kind) {
        DawMixCapture* c=calloc(1,sizeof(*c));c->snapshot=calloc(1,sizeof(*c->snapshot));c->channels=2;c->sample_rate=rates[rate_index];c->start=7001;c->end=c->start+(kind==4?1024:2048);
        CHECK(daw_mix_frequency_prepare(c),"spectral prepare");int count=(int)(c->end-c->start);float values[2048];double weighted=0,weight=0;unsigned seed=7;
        for(int f=0;f<count;++f){seed=seed*1664525u+1013904223u;float value=kind==0?.5f*sinf(2*M_PI*32*f/2048):kind==1?0:kind==2?.5f:kind==3?(f%2?-.25f:.25f):((float)(seed>>8)/16777216.f-.5f);values[f]=value;double w=.5-.5*cos(2*M_PI*f/count);weighted+=value*value*w*w;weight+=w*w;}
        const uint64_t latency=13;float block[37*2];
        for(uint64_t pos=c->start+latency;pos<c->end+latency;){int frames=c->end+latency-pos<37?(int)(c->end+latency-pos):37;for(int f=0;f<frames;++f){float v=values[pos-c->start-latency+f];block[f*2]=v;block[f*2+1]=-v*.5f;}CHECK(daw_mix_frequency_observe(c,0,pos,latency,block,frames,2),"partitioned delayed tap");pos+=frames;}
        CHECK(daw_mix_frequency_finish(c) && !c->frequency.samples && !c->frequency.counts,"only immutable spectral powers retained");
        for(int ch=0;ch<2;++ch){double sum=0;int peak=0;const float* p=c->frequency.powers+(size_t)ch*DAW_FREQUENCY_WINDOWS*DAW_FREQUENCY_BINS;for(int k=0;k<DAW_FREQUENCY_BINS;++k){sum+=p[k];if(p[k]>p[peak])peak=k;}CHECK(fabs(sum-weighted/weight*(ch?.25:1))<1e-7,"independent weighted Parseval calibration; anti-phase channels do not cancel");if(kind==0)CHECK(peak==32,"bin-centered tone");if(kind==2)CHECK(peak==0,"DC retained");if(kind==3)CHECK(peak==1024,"Nyquist retained");}
        cJSON* j=cJSON_CreateObject();daw_control_mix_frequency_json(j,c,0,31,1);cJSON* bands=cJSON_GetObjectItemCaseSensitive(j,"bands");double band_power=0,high=0;
        for(int b=0;b<cJSON_GetArraySize(bands);++b){cJSON* band=cJSON_GetArrayItem(bands,b);band_power+=cJSON_GetObjectItemCaseSensitive(band,"mean_square_power")->valuedouble;high=cJSON_GetObjectItemCaseSensitive(band,"high_hz")->valuedouble;}
        CHECK(high==c->sample_rate*.5 && fabs(band_power-weighted/weight*.25)<1e-7,"disjoint bands cover DC through exact Nyquist at every sample rate");cJSON_Delete(j);
        daw_mix_capture_free(c);
    }
    DawMixCapture* c=calloc(1,sizeof(*c));c->snapshot=calloc(1,sizeof(*c->snapshot));c->channels=2;c->sample_rate=48000;c->end=2;CHECK(daw_mix_frequency_prepare(c) && daw_mix_frequency_finish(c) && !c->frequency.frames,"short range explicitly unavailable");daw_mix_capture_free(c);
}
// Confirms frequency changes appear in distinct authored windows for production child and master outputs.
static void frequency_over_time(const char* path) {
    float samples[4096*2];for(int f=0;f<4096;++f){float v=.5f*sinf(2*M_PI*(f<2048?750:6000)*f/48000);samples[f*2]=v;samples[f*2+1]=-v*.5f;}
    EngineBounceBuffer b={samples,4096,2,48000};AppState* s=state();CHECK(engine_bounce_write_wav(&b,path,ENGINE_BOUNCE_WAV_FLOAT32) && engine_add_clip_to_track(s->engine,0,path,0,NULL),"changing tone fixture");DawMixCapture* c=capture(s,0,4096);
    for(int node=0;node<2;++node)for(int window=0;window<32;window+=31){const float* power=c->frequency.powers+((size_t)node*2*32+window)*DAW_FREQUENCY_BINS;int peak=0;for(int k=1;k<DAW_FREQUENCY_BINS;++k)if(power[k]>power[peak])peak=k;CHECK(peak==(window?256:32) && c->frequency.start[window]==(uint64_t)(window?2048:0),"changing tone, exact window bounds, child/master parity");}
    master_parity(s,c);daw_mix_capture_free(c);release(s);
}
// Verifies production note fences, region tails, sampled/piano floors and mono/choke ownership.
static void note_tail_diagnostics(void) {
    AppState* s=state();int clip;CHECK(engine_add_midi_clip_to_track(s->engine,0,0,18000,&clip) && engine_clip_midi_add_note(s->engine,0,clip,(EngineMidiNote){0,12000,60,.75f},NULL),"tail region");
    EngineInstrumentParams p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);p.attack_ms=0;p.decay_ms=0;p.sustain=1;p.release_ms=100;
    CHECK(engine_clip_midi_set_instrument_params(s->engine,0,clip,p) && engine_track_set_gain(s->engine,0,.5f),"tail controls");DawMixCapture* c=capture(s,0,24000);
    CHECK(c->tails->count==1 && c->tails->notes[0].prepared && c->tails->notes[0].production.key_off==12000 && c->tails->notes[0].production.source_end==18000,"actual prepared ending fence");
    const DawSourceTailChannel* m=&c->tails->source[0][0];CHECK(m->samples==24000 && m->post_active>0 && m->post_last>=16500 && m->post_last<16800 && fabs(m->peak*.5-c->stages[0][ENGINE_MIX_INPUT].channel[0].peak)<1e-6,"measured source release before child gain");master_parity(s,c);daw_mix_capture_free(c);
    p.release_ms=500;CHECK(engine_clip_midi_set_instrument_params(s->engine,0,clip,p),"long release");c=capture(s,0,24000);CHECK(c->tails->source[0][0].post_last>=17900 && c->tails->source[0][0].post_last<18000,"region fence truncates long synth release");master_parity(s,c);daw_mix_capture_free(c);
    CHECK(engine_track_set_muted(s->engine,0,true),"muted source");c=capture(s,0,24000);CHECK(c->tails->notes[0].prepared && c->tails->source[0][0].samples==0 && c->stages[1][ENGINE_MIX_SANITIZED].channel[0].active_samples==0,"muted source explicitly unobserved, not manufactured silence");daw_mix_capture_free(c);release(s);
    EngineInstrumentSource* source=engine_instrument_source_create();EngineNoteEnding ending;EngineMidiNote note={0,12000,60,.8f};p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO);p.release_ms=0;
    EngineAutomationPoint points[]={{0,1,ENGINE_AUTOMATION_CURVE_HOLD},{24000,0,ENGINE_AUTOMATION_CURVE_HOLD}};EngineAutomationLane lane={.target=ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN_PEDAL,.points=points,.point_count=2};
    CHECK(engine_instrument_source_set_midi_clip(source,0,48000,ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO,p,&note,1,NULL,0,&lane,1) && engine_instrument_source_note_ending(source,0,&ending),"piano ending getter");CHECK(ending.key_off==12000 && ending.damper_off==24000 && ending.sampled && ending.key_off_params.release_ms==0,"piano key/damper distinction and raw release floor provenance");
    p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    EngineAutomationPoint track_points[]={{10000,.2f,ENGINE_AUTOMATION_CURVE_HOLD},{22000,.4f,ENGINE_AUTOMATION_CURVE_HOLD}};EngineAutomationPoint clip_points[]={{0,.1f,ENGINE_AUTOMATION_CURVE_HOLD},{12000,-.05f,ENGINE_AUTOMATION_CURVE_HOLD}};
    EngineAutomationLane track_release={.target=ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_MS,.points=track_points,.point_count=2,.before=ENGINE_AUTOMATION_BOUNDARY_HOLD,.after=ENGINE_AUTOMATION_BOUNDARY_HOLD};EngineAutomationLane clip_release={.target=ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_MS,.points=clip_points,.point_count=2,.before=ENGINE_AUTOMATION_BOUNDARY_HOLD,.after=ENGINE_AUTOMATION_BOUNDARY_HOLD};
    CHECK(engine_instrument_source_set_midi_clip(source,10000,48000,ENGINE_INSTRUMENT_PRESET_PURE_SINE,p,&note,1,&track_release,1,&clip_release,1) && engine_instrument_source_note_ending(source,0,&ending),"automated ending controls");
    EngineInstrumentParams expected=engine_instrument_params_apply_automation_delta(ENGINE_INSTRUMENT_PRESET_PURE_SINE,p,ENGINE_INSTRUMENT_PARAM_RELEASE_MS,.4f);expected=engine_instrument_params_apply_automation_delta(ENGINE_INSTRUMENT_PRESET_PURE_SINE,expected,ENGINE_INSTRUMENT_PARAM_RELEASE_MS,-.05f);
    CHECK(fabs(ending.key_off_params.release_ms-expected.release_ms)<1e-5 && ending.onset_params.release_ms!=ending.key_off_params.release_ms,"global track then region-relative release automation at exact key-off");
    p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_SUB_808);p.mono=1;EngineMidiNote notes[]={{2000,1000,64,.8f},{0,1500,60,.8f},{1000,1500,62,.8f}};
    CHECK(engine_instrument_source_set_midi_clip(source,0,4000,ENGINE_INSTRUMENT_PRESET_SUB_808,p,notes,3,NULL,0,NULL,0),"mono ending");engine_instrument_source_set_export_end(source,1500,0);CHECK(engine_instrument_source_note_ending(source,0,&ending) && ending.original_index==1 && ending.handoff==1000 && ending.next_legato,"original identity and legato handoff survive export compaction");
    p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_OPEN_HAT);p.hat_choke=1;uint64_t fences[]={500};CHECK(engine_instrument_source_set_midi_clip(source,0,4000,ENGINE_INSTRUMENT_PRESET_OPEN_HAT,p,&note,1,NULL,0,NULL,0) && engine_instrument_source_set_hat_chokes(source,fences,1) && engine_instrument_source_note_ending(source,0,&ending) && ending.choke==500,"prepared track choke fence");engine_instrument_source_destroy(source);
}
// Verifies whole-song extent, tails, immutable guards and long production/bounce parity.
static void whole_song_loudness(void) {
    AppState* s=state();int clip;uint64_t duration=48000*40;
    CHECK(engine_add_midi_clip_to_track(s->engine,0,0,duration,&clip) && engine_clip_midi_add_note(s->engine,0,clip,(EngineMidiNote){0,duration,60,.5f},NULL),"40 second private song");
    DawMixSnapshot* guard=NULL;CHECK(daw_action_get_mix_snapshot(s,&guard)==DAW_ACTION_UNCHANGED,"song guard");DawMixCapture* c=NULL;
    CHECK(daw_action_capture_song_loudness(s,guard,48000,&c)==DAW_ACTION_UNCHANGED && c->whole_song && c->start==0 && c->authored_end==duration && c->end==duration+48000 && !c->tails && !c->frequency.samples && !c->effects[0],"complete derived song plus explicit tail, lightweight storage");
    CHECK(c->loudness[0].short_term_count==381 && c->loudness[0].frames==duration+48000 && isfinite(c->loudness[0].lra) && c->loudness[0].count==0 && c->stages[0][0].channel[0].samples==0,"complete 10Hz LRA windows beyond passage limit");master_parity(s,c);daw_mix_capture_free(c);c=NULL;
    CHECK(daw_action_capture_song_loudness(s,guard,48000*30+1,&c)==DAW_ACTION_INVALID && !c,"tail work cap");
    CHECK(daw_action_capture_mix(s,guard,0,duration,&c)==DAW_ACTION_INVALID && !c,"passage cap unchanged");
    DawMixSource saved=guard->regions[0];guard->regions[0].duration=48000*601;
    CHECK(daw_action_capture_song_loudness(s,guard,0,&c)==DAW_ACTION_INVALID && !c,"whole programme over-cap refusal");guard->regions[0]=saved;
    CHECK(engine_track_set_gain(s->engine,0,.75f),"native gain edit");CHECK(daw_action_capture_song_loudness(s,guard,0,&c)==DAW_ACTION_STALE_TARGET && !c,"exact whole-song stale guard");
    daw_mix_snapshot_free(guard);release(s);
}
// Qualifies known amplitudes, nested buses/returns, latency, mute/solo, state guards and real samples.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER|SDL_INIT_AUDIO),"SDL");whole_song_loudness();AppState* s=state();char dir[]="/tmp/sonics-mix-fixture-XXXXXX";CHECK(mkdtemp(dir),"private fixture");char path[1024];snprintf(path,sizeof(path),"%s/sine.wav",dir);
    float* samples=calloc(48000*2,sizeof(float));for(int f=0;f<48000;++f){samples[f*2]=.5f*sinf(2*M_PI*1000*f/48000);samples[f*2+1]=.25f*sinf(2*M_PI*1000*f/48000);}EngineBounceBuffer b={samples,48000,2,48000};CHECK(engine_bounce_write_wav(&b,path,ENGINE_BOUNCE_WAV_FLOAT32),"known sine WAV");free(samples);
    CHECK(engine_add_clip_to_track(s->engine,0,path,0,NULL),"source");DawMixCapture* c=capture(s,0,48000);level(c,0,ENGINE_MIX_INPUT,.5,.25);level(c,1,ENGINE_MIX_SANITIZED,.5,.25);master_parity(s,c);
    CHECK(c->loudness[0].available && c->loudness[0].frames==48000 && fabs(c->loudness[0].integrated-c->loudness[1].integrated)<1e-9 && fabs(c->loudness[0].true_peak[0]-.5)<.01,"production child/master LUFS and true peak parity");
    DawMixSnapshot* guard=NULL;CHECK(daw_action_get_mix_snapshot(s,&guard)==DAW_ACTION_UNCHANGED,"guard");CHECK(engine_track_set_gain(s->engine,0,.5f),"gain change");DawMixCapture* rejected=NULL;CHECK(daw_action_capture_mix(s,guard,0,48000,&rejected)==DAW_ACTION_STALE_TARGET && !rejected,"stale exact guard");level(c,0,ENGINE_MIX_INPUT,.5,.25);daw_mix_snapshot_free(guard);daw_mix_capture_free(c);
    CHECK(engine_track_set_gain(s->engine,0,4),"overload gain");c=capture(s,0,48000);CHECK(c->stages[0][ENGINE_MIX_INPUT].channel[0].overload>0 && c->stages[0][ENGINE_MIX_INPUT].channel[1].full_scale>0 && c->stages[0][ENGINE_MIX_INPUT].channel[1].overload==0,"full-scale versus overload channel thresholds");master_parity(s,c);daw_mix_capture_free(c);CHECK(engine_track_set_gain(s->engine,0,.5f),"restore source gain");
    CHECK(engine_add_track(s->engine)==1,"silent child");EngineRoutingSnapshot before,out;CHECK(daw_action_get_routing(s,&before)==DAW_ACTION_UNCHANGED,"routing");CHECK(daw_action_create_group(s,&before,"Strings",&out)==DAW_ACTION_APPLIED,"inner group");before=out;CHECK(daw_action_create_group(s,&before,"Orchestra",&out)==DAW_ACTION_APPLIED,"outer group");before=out;CHECK(daw_action_create_return(s,&before,&out)==DAW_ACTION_APPLIED,"return");before=out;
    out.tracks[0].parent_id=out.tracks[2].track_id;out.tracks[1].parent_id=out.tracks[3].track_id;out.tracks[2].parent_id=out.tracks[3].track_id;out.tracks[0].sends[0]=(EngineSend){out.tracks[4].track_id,.25f};CHECK(daw_action_set_routing(s,&before,&out,&before)==DAW_ACTION_APPLIED,"nested/send routing");
    CHECK(engine_track_set_gain(s->engine,2,.6f) && engine_track_set_gain(s->engine,3,.7f) && engine_track_set_gain(s->engine,4,.5f),"bus gains");c=capture(s,0,48000);
    level(c,0,ENGINE_MIX_INPUT,.25,.125);level(c,1,ENGINE_MIX_OUTPUT,0,0);level(c,2,ENGINE_MIX_INPUT,.25,.125);level(c,2,ENGINE_MIX_OUTPUT,.15,.075);level(c,3,ENGINE_MIX_OUTPUT,.105,.0525);level(c,4,ENGINE_MIX_INPUT,.0625,.03125);level(c,4,ENGINE_MIX_OUTPUT,.03125,.015625);level(c,5,ENGINE_MIX_SANITIZED,.13625,.068125);master_parity(s,c);daw_mix_capture_free(c);
    FxInstId gain=engine_fx_track_add(s->engine,2,1);CHECK(gain && engine_fx_track_set_param(s->engine,2,gain,0,-6),"insert gain");double half=pow(10,-6./20);c=capture(s,0,48000);level(c,2,ENGINE_MIX_INSERTS_EQ,.25*half,.125*half);level(c,5,ENGINE_MIX_SANITIZED,.105*half+.03125,.0525*half+.015625);master_parity(s,c);daw_mix_capture_free(c);
    FxInstId limiter=engine_fx_track_add(s->engine,2,21),wet=engine_fx_track_add(s->engine,4,21),master=engine_fx_master_add(s->engine,21);CHECK(limiter && wet && master && engine_fx_track_set_param(s->engine,2,limiter,1,1) && engine_fx_track_set_param(s->engine,4,wet,1,2) && engine_fx_master_set_param(s->engine,master,1,1),"independent latency");
    c=capture(s,12001,31001);CHECK(c->rendered_frames%(uint64_t)c->block_frames==0 && c->rendered_frames-c->end>=c->master_latency && c->master_latency>0 && c->stages[2][ENGINE_MIX_INPUT].latency==0 && c->stages[2][ENGINE_MIX_OUTPUT].latency>0 && c->stages[3][ENGINE_MIX_INPUT].latency==c->stages[2][ENGINE_MIX_OUTPUT].latency && c->stages[5][ENGINE_MIX_SANITIZED].latency==c->master_latency,"stage-specific alignment");master_parity(s,c);daw_mix_capture_free(c);
    FxMasterSnapshot chain;CHECK(engine_fx_track_snapshot(s->engine,2,&chain),"limiter settings");float ceiling=chain.items[1].params[0];CHECK(engine_track_set_gain(s->engine,0,4) && engine_fx_track_set_param(s->engine,2,limiter,0,-12),"nonlinear bus stimulus");
    c=capture(s,12001,31001);CHECK(c->stages[2][ENGINE_MIX_INPUT].channel[0].peak>1 && c->stages[2][ENGINE_MIX_INSERTS_EQ].channel[0].peak<.5,"actual nonlinear bus reduction");master_parity(s,c);daw_mix_capture_free(c);CHECK(engine_track_set_gain(s->engine,0,.5f) && engine_fx_track_set_param(s->engine,2,limiter,0,ceiling),"restore private bus");
    CHECK(engine_track_set_muted(s->engine,0,true),"mute");c=capture(s,0,48000);level(c,5,ENGINE_MIX_SANITIZED,0,0);daw_mix_capture_free(c);CHECK(engine_track_set_muted(s->engine,0,false) && engine_track_set_solo(s->engine,4,true),"return solo");c=capture(s,0,48000);level(c,5,ENGINE_MIX_SANITIZED,.03125,.015625);master_parity(s,c);daw_mix_capture_free(c);CHECK(engine_track_set_solo(s->engine,4,false),"restore solo");
    CHECK(engine_start(s->engine) && engine_transport_play(s->engine),"playback");SDL_Delay(20);c=capture(s,0,48000);CHECK(engine_transport_requested_playing(s->engine),"capture leaves playback active");daw_mix_capture_free(c);engine_transport_stop(s->engine);engine_stop(s->engine);release(s);effect_calibration(path);frequency_over_time(path);frequency_calibration();note_tail_diagnostics();CHECK(!unlink(path) && !rmdir(dir),"fixture cleanup");
    s=state();int clip;CHECK(engine_add_midi_clip_to_track(s->engine,0,0,48000,&clip) && engine_clip_midi_add_note(s->engine,0,clip,(EngineMidiNote){0,12000,60,.5f},NULL),"sampled region");
    DawActionTarget target;CHECK(daw_instrument_target_capture(s,0,clip,&target)==DAW_ACTION_UNCHANGED,"instrument target");DawInstrumentRequest patch={.target=target,.set_preset=true,.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN};CHECK(daw_action_set_instrument(s,&patch).status==DAW_ACTION_APPLIED,"real sampled voice");
    c=capture(s,0,48000);CHECK(c->snapshot->sources==1 && c->snapshot->regions[0].preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN && c->snapshot->regions[0].gate_max==12000 && c->stages[0][ENGINE_MIX_INPUT].channel[0].peak>0,"sampled provenance and signal");master_parity(s,c);daw_mix_capture_free(c);release(s);SDL_Quit();puts("mix_capture_test: passed (sine/silence/stereo calibration, nested buses/returns, insert gains, per-stage latency, exact master parity, mute/return solo, live preservation, stale guards and real sampled voice)");return 0;
}
