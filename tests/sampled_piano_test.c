#include "engine/instrument_samples.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("sampled_piano_test",(v),(m))
// Measures a short production piano window without effects or output normalization.
static double energy(EngineInstrumentParams p,int note,float velocity,int rate,uint64_t age,uint64_t gate) {
    double e=0;for(int i=0;i<rate/20;++i){float x=engine_instrument_sample_bank_note(ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO,note,velocity,p,rate,age+i,gate);CHECK(isfinite(x) && fabsf(x)<1,"finite unclipped strike");e+=x*x;}return e/(rate/20);
}
// Qualifies full-keyboard strikes, genuine velocity zones, natural decay, dampers and exact source partitions.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_TIMER) && engine_instrument_samples_init(),"preload");EngineInstrumentPresetId voice=ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO;CHECK(engine_instrument_sample_bank_ready(voice),"piano ready");EngineSampleBankInfo info;CHECK(engine_instrument_sample_bank_info(voice,&info) && info.layers==3 && info.minimum_note==21 && info.maximum_note==108,"real bank coverage");EngineInstrumentParams p=engine_instrument_default_params(voice);
    const int rates[]={44100,48000,96000};
    for(int r=0;r<3;++r)for(int n=21;n<=108;++n){int rate=rates[r];CHECK(energy(p,n,.9f,rate,rate/10,rate*40)>1e-8,"all-key audible attack");CHECK(energy(p,n,.9f,rate,rate*2,rate)==0,"damper reaches silence");CHECK(engine_instrument_sample_bank_note(voice,n,.9f,p,rate,0,24)==0,"silent retrigger onset");}
    CHECK(energy(p,20,.9f,48000,4800,100000)==0 && energy(p,109,.9f,48000,4800,100000)==0,"bounded keyboard");
    CHECK(energy(p,60,.9f,48000,48000,3000000)>energy(p,60,.9f,48000,480000,3000000)*2,"held strike decays naturally");CHECK(energy(p,108,.9f,48000,4800000,5000000)==0,"sample endpoint does not loop or hold DC");
    CHECK(energy(p,60,.9f,48000,4800,1000000)>energy(p,60,.2f,48000,4800,1000000)*2,"velocity changes intensity");double aa=0,bb=0,ab=0;
    for(int i=4800;i<12000;++i){float a=engine_instrument_sample_bank_note(voice,60,.2f,p,48000,i,1000000),b=engine_instrument_sample_bank_note(voice,60,.9f,p,48000,i,1000000);aa+=a*a;bb+=b*b;ab+=a*b;}
    CHECK(fabs(ab/sqrt(aa*bb))<.95,"recorded velocity changes timbre beyond gain");
    EngineInstrumentParams changed=p;changed.dynamics=1;changed.velocity_depth=0;for(int i=4800;i<4900;++i)CHECK(engine_instrument_sample_bank_note(voice,60,.55f,p,48000,i,100000)==engine_instrument_sample_bank_note(voice,60,.55f,changed,48000,i,100000),"held wind controls cannot morph a piano strike");
    EngineInstrumentSource* source=engine_instrument_source_create();CHECK(source!=NULL,"source");const uint32_t frames=240000;EngineMidiNote notes[]={{0,48000,60,.2f},{12000,60000,64,.55f},{24000,48000,67,.9f},{96000,48000,60,.9f}};CHECK(engine_instrument_source_set_midi_clip(source,0,frames,voice,p,notes,4,NULL,0,NULL,0),"piano chord/retrigger publish");float* a=calloc(frames*2,sizeof(float));float* b=calloc(frames*2,sizeof(float));CHECK(a&&b,"buffers");engine_instrument_source_reset(source,48000,2);engine_instrument_source_render(source,a,frames,0);engine_instrument_source_reset(source,48000,2);for(uint32_t f=0;f<frames;f+=128)engine_instrument_source_render(source,b+f*2,frames-f<128?frames-f:128,f);CHECK(!memcmp(a,b,frames*2*sizeof(float)),"callback partitions exact");for(uint32_t i=0;i<frames*2;++i)CHECK(isfinite(a[i]) && fabsf(a[i])<1,"unclipped chord/retrigger");for(uint32_t i=(frames-24000)*2;i<frames*2;++i)CHECK(a[i]==0,"released chord silence");free(a);free(b);engine_instrument_source_destroy(source);SDL_Quit();puts("sampled_piano_test: full keyboard/rates, natural decay, velocity timbre, dampers, retriggers and partitions passed");return 0;
}
