#include "engine/instrument.h"
#include "engine/engine_internal.h"
#include "engine/instrument_samples.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,m) daw_test_expect("sampled_violin_test",(x),(m))
// Measures finite sample energy for gate, pitch and timbre assertions.
static double energy(EngineInstrumentParams p,int note,float v,int rate,uint64_t start,uint64_t gate) {
    double sum=0;
    for(int i=0;i<rate/10;++i){float x=engine_instrument_samples_note(note,v,p,rate,start+i,gate);CHECK(isfinite(x),"finite sample");sum+=x*x;}
    return sum/(rate/10);
}
// Verifies the fixed bank, nearby pitch coverage, held gates, release and recorded timbre differences.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_TIMER),"SDL timer");
    CHECK(engine_instrument_samples_init(),"all 30 assets loaded");
    EngineInstrumentPresetId id=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN;
    EngineInstrumentParams p=engine_instrument_default_params(id);
    CHECK(!strcmp(engine_instrument_preset_id_string(id),"sampled_violin"),"stable voice identity");
    CHECK(engine_instrument_preset_param_count(id)==5,"only supported controls");
    CHECK(engine_instrument_param_automation_target(ENGINE_INSTRUMENT_PARAM_DYNAMICS)==26,"stable appended dynamics automation");
    CHECK(engine_instrument_param_automation_target(ENGINE_INSTRUMENT_PARAM_MONO)==ENGINE_AUTOMATION_TARGET_COUNT,"mono remains discrete");
    for(int rate=44100;rate<=96000;rate+=(rate==44100?3900:48000)) {
        for(int note=55;note<=96;++note) CHECK(energy(p,note,.7f,rate,rate,rate*12)>1e-5,"every supported pitch audible");
        CHECK(energy(p,69,.7f,rate,rate*10,rate*12)>1e-5,"ten-second hold sustains");
        CHECK(energy(p,69,.7f,rate,rate*2,rate)==0,"note off completes release");
        CHECK(energy(p,54,.7f,rate,rate,rate*12)==0 && energy(p,97,.7f,rate,rate,rate*12)==0,"range is explicit, no extreme stretching");
    }
    p.velocity_depth=0;double aa=0,bb=0,ab=0;
    for(int i=48000;i<96000;++i){p.dynamics=0;float a=engine_instrument_samples_note(69,.7f,p,48000,i,200000);p.dynamics=1;float b=engine_instrument_samples_note(69,.7f,p,48000,i,200000);aa+=a*a;bb+=b*b;ab+=a*b;}
    CHECK(fabs(ab/sqrt(aa*bb))<.95,"soft and strong differ beyond loudness");
    // Zero-time UI envelopes still get bounded click protection, including an early note-off.
    p.attack_ms=0; p.release_ms=0;
    CHECK(engine_instrument_samples_note(69,.7f,p,48000,0,24)==0,"protected onset starts at zero");
    CHECK(energy(p,69,.7f,48000,1000,24)==0,"protected short note reaches silence");
    float just_before=engine_instrument_samples_note(69,.7f,p,48000,23,24);
    float just_after=engine_instrument_samples_note(69,.7f,p,48000,24,24);
    CHECK(fabsf(just_after-just_before)<.005f,"early-off boundary has no amplitude step");
    p=engine_instrument_default_params(id);
    EngineInstrumentSource* source=engine_instrument_source_create();CHECK(source!=NULL,"source created");
    EngineMidiNote note={0,480000,69,.7f};
    EngineAutomationLane lane;engine_automation_lane_init(&lane,26);lane.before=lane.after=ENGINE_AUTOMATION_BOUNDARY_HOLD;
    EngineAutomationPoint points[]={{0,-1,ENGINE_AUTOMATION_CURVE_SMOOTH},{480000,1,ENGINE_AUTOMATION_CURVE_LINEAR}};
    CHECK(engine_automation_lane_set_points(&lane,points,2),"expression points");
    CHECK(engine_instrument_source_set_midi_clip(source,0,480000,id,p,&note,1,NULL,0,&lane,1),"published source");
    engine_instrument_source_reset(source,48000,2);
    float* whole=calloc(960000,sizeof(float));float* split=calloc(960000,sizeof(float));CHECK(whole&&split,"test buffers");
    engine_instrument_source_render(source,whole,480000,0);
    engine_instrument_source_reset(source,48000,2);
    for(int f=0;f<480000;f+=128){int n=480000-f<128?480000-f:128;engine_instrument_source_render(source,split+f*2,n,f);}
    CHECK(!memcmp(whole,split,960000*sizeof(float)),"expression independent of block partition");
    double peak=0;for(int i=0;i<960000;++i){CHECK(isfinite(whole[i]),"finite expression");if(fabs(whole[i])>peak)peak=fabs(whole[i]);}
    CHECK(peak>.01&&peak<1,"original level, unclipped");
    float tail[256]={0};engine_instrument_source_render(source,tail,128,480000);double tail_energy=0;for(int i=0;i<256;++i)tail_energy+=tail[i]*tail[i];CHECK(tail_energy>0,"release continues after region end");
    // Live audition retirement must preserve the zero-release click-protection tail.
    EngineRuntimeConfig config; config_set_defaults(&config); Engine* engine=engine_create(&config);
    CHECK(engine!=NULL,"live fixture engine"); CHECK(engine_add_track(engine)==1,"live fixture track"); p.release_ms=0;
    engine_midi_audition_apply_note_on(engine,0,id,p,69,.7f);
    engine->midi_audition_idle_frame=1000; engine_midi_audition_apply_note_off(engine,69);
    engine_midi_audition_retire(engine); CHECK(engine->midi_audition_notes.note_count==1,"live zero release retained for protection");
    engine->midi_audition_idle_frame=1192;engine_midi_audition_retire(engine);
    CHECK(engine->midi_audition_notes.note_count==0,"live protection tail retires at four ms");
    engine_destroy(engine);
    free(whole);free(split);engine_automation_lane_free(&lane);engine_instrument_source_destroy(source);SDL_Quit();
    printf("Sampled violin: all pitches/rates, timbre, long gate, release, expression and block determinism passed (peak %.6f)\n",peak);return 0;
}
