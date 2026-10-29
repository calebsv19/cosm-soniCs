#include "engine/instrument_samples.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,m) daw_test_expect("sampled_winds_test",(x),(m))
// Measures actual sample playback energy over a steady diagnostic window.
static double energy(EngineInstrumentPresetId preset, EngineInstrumentParams p, int note, int rate, uint64_t age, uint64_t gate) {
    double sum=0;
    for (int i=0;i<rate/10;++i) { float x=engine_instrument_sample_bank_note(preset,note,.7f,p,rate,age+i,gate); CHECK(isfinite(x),"finite output"); sum+=x*x; }
    return sum/(rate/10);
}
// Exercises real banks, multiroot coverage, adjacent dynamics, gates and block-independent automation.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_TIMER),"SDL init"); CHECK(engine_instrument_samples_init(),"shared bank preload");
    const EngineInstrumentPresetId voices[]={ENGINE_INSTRUMENT_PRESET_SAMPLED_TRUMPET,ENGINE_INSTRUMENT_PRESET_SAMPLED_CLARINET,ENGINE_INSTRUMENT_PRESET_SAMPLED_SAXOPHONE,ENGINE_INSTRUMENT_PRESET_SAMPLED_FLUTE,ENGINE_INSTRUMENT_PRESET_SAMPLED_CELLO_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_OBOE,ENGINE_INSTRUMENT_PRESET_SAMPLED_FRENCH_HORN,ENGINE_INSTRUMENT_PRESET_SAMPLED_BASSOON,ENGINE_INSTRUMENT_PRESET_SAMPLED_TENOR_TROMBONE,ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLA_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_DOUBLE_BASS};
    const int layers[]={2,3,2,2,2,2,2,2,2,2,2,2};
    for (int v=0;v<12;++v) {
        EngineSampleBankInfo info; CHECK(engine_instrument_sample_bank_info(voices[v],&info),"bank metadata");
        CHECK(engine_instrument_sample_bank_ready(voices[v]) && info.layers==layers[v],"resident dynamics layers");
        int anchor=info.maximum_note<71 ? info.maximum_note-2 : 69;
        EngineInstrumentParams p=engine_instrument_default_params(voices[v]);
        for (int rate=44100;rate<=96000;rate+=(rate==44100?3900:48000)) {
            for (int note=info.minimum_note;note<=info.maximum_note;++note)
                CHECK(energy(voices[v],p,note,rate,rate,rate*12)>1e-6,"all sounding pitches audible");
            CHECK(energy(voices[v],p,anchor,rate,rate*10,rate*12)>1e-6,"long held sustain");
            CHECK(energy(voices[v],p,anchor,rate,rate*2,rate)==0,"note-off reaches silence");
            CHECK(energy(voices[v],p,info.minimum_note-1,rate,rate,rate*12)==0,"bounded low range");
            CHECK(energy(voices[v],p,info.maximum_note+1,rate,rate,rate*12)==0,"bounded high range");
        }
        p.velocity_depth=0;
        double aa=0,bb=0,ab=0;
        for (int i=48000;i<92000;++i) {
            p.dynamics=0;float a=engine_instrument_sample_bank_note(voices[v],anchor,.7f,p,48000,i,200000);
            p.dynamics=1;float b=engine_instrument_sample_bank_note(voices[v],anchor,.7f,p,48000,i,200000);
            aa+=a*a;bb+=b*b;ab+=a*b;
            if (v==1) {
                p.dynamics=.49999f;float lo=engine_instrument_sample_bank_note(voices[v],anchor,.7f,p,48000,i,200000);
                p.dynamics=.50001f;float hi=engine_instrument_sample_bank_note(voices[v],anchor,.7f,p,48000,i,200000);
                CHECK(fabsf(lo-hi)<.001f,"three-layer midpoint is continuous");
            }
        }
        CHECK(fabs(ab/sqrt(aa*bb))<.95,"recorded dynamics change waveform beyond gain");
        // Tiny expression changes around the middle layer must not introduce a sharp timbre corner.
        if (v==1) for (int n=info.minimum_note;n<=info.maximum_note;++n) {
            for (int i=48000;i<49000;++i) {
                p.dynamics=.499f;float lo=engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,i,200000)/(.55f+.45f*p.dynamics);
                p.dynamics=.5f;float mid=engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,i,200000)/(.55f+.45f*p.dynamics);
                p.dynamics=.501f;float hi=engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,i,200000)/(.55f+.45f*p.dynamics);
                CHECK(fabsf(lo-2*mid+hi)<.00001f,"middle-layer timbre slope is smooth");
            }
        }
        if (v>=2) {
            p.velocity_depth=0;
            for (int n=info.minimum_note;n<=info.maximum_note;++n) for (int window=4;window<48;++window) {
                uint64_t age=(uint64_t)window*12000;
                p.dynamics=0;double soft=energy(voices[v],p,n,48000,age,1000000)/(.55*.55);
                p.dynamics=1;double strong=energy(voices[v],p,n,48000,age,1000000);
                p.dynamics=.5f;double middle=energy(voices[v],p,n,48000,age,1000000)/(.775*.775);
                if (!(middle>(soft+strong)*.5*(v>=9?.80*.80:(v>=5?.95*.95:(v>=4?.80*.80:(v==3?.95*.95:.75*.75)))))) fprintf(stderr,"Prepared blend preset=%d note=%d age=%llu ratio=%g\n",voices[v],n,(unsigned long long)age,sqrt(middle/((soft+strong)*.5)));
                CHECK(middle>(soft+strong)*.5*(v>=9?.80*.80:(v>=5?.95*.95:(v>=4?.80*.80:(v==3?.95*.95:.75*.75)))),"prepared dynamics avoids sustained layer cancellation");
                CHECK(middle<(soft+strong)*.5*(v>=9?1.20*1.20:(v>=5?1.10*1.10:(v>=4?1.20*1.20:(v==3?1.10*1.10:1.35*1.35)))),"prepared dynamics avoids correlated-layer level bulges");
            }
        }
        p=engine_instrument_default_params(voices[v]);
        for (int n=info.minimum_note;n<=info.maximum_note;++n) {
            uint64_t gate=2400, end=gate+(uint64_t)ceil(p.release_ms*48);
            CHECK(engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,0,gate)==0,"all-pitch silent onset");
            CHECK(fabsf(engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,end-1,gate))<.000001f,"all-pitch smooth release endpoint");
            CHECK(engine_instrument_sample_bank_note(voices[v],n,.7f,p,48000,end,gate)==0,"all-pitch exact release silence");
        }
        if (v>=5) for (int n=info.minimum_note;n<=info.maximum_note;++n) for(int k=0;k<3;++k) {
            float velocity=k==0?.1f:(k==1?.5f:1.f);uint64_t gate=144000;
            float held=engine_instrument_sample_bank_note(voices[v],n,velocity,p,48000,gate,gate+1);
            float released=engine_instrument_sample_bank_note(voices[v],n,velocity,p,48000,gate,gate);
            CHECK(held==released,"note-off begins without a level discontinuity");
            CHECK(engine_instrument_sample_bank_note(voices[v],n,velocity,p,48000,0,24)==0,"retrigger begins silently");
            uint64_t end=24+(uint64_t)ceil(p.release_ms*48);
            CHECK(engine_instrument_sample_bank_note(voices[v],n,velocity,p,48000,end,24)==0,"short retrigger releases exactly");
        }
        p=engine_instrument_default_params(voices[v]);p.attack_ms=p.release_ms=0;
        CHECK(engine_instrument_sample_bank_note(voices[v],anchor,.7f,p,48000,0,24)==0,"protected onset");
        CHECK(energy(voices[v],p,anchor,48000,1000,24)==0,"protected early-off silence");
        EngineInstrumentSource* source=engine_instrument_source_create();CHECK(source!=NULL,"source");
        uint32_t frames=v>=3 ? 600000 : 120000;
        EngineMidiNote notes[9]={{0,frames,anchor,.7f}};int note_count=1;
        if (v>=3) {
            notes[0].duration_frames=384000;
            for (int i=0;i<8;++i) notes[note_count++]=(EngineMidiNote){288000+(uint32_t)i*12000,18000,anchor+i%3,i%2?.8f:.3f};
        }
        EngineAutomationLane lane;engine_automation_lane_init(&lane,26);lane.before=lane.after=ENGINE_AUTOMATION_BOUNDARY_HOLD;
        EngineAutomationPoint points[]={{0,-1,ENGINE_AUTOMATION_CURVE_SMOOTH},{frames,1,ENGINE_AUTOMATION_CURVE_LINEAR}};
        CHECK(engine_automation_lane_set_points(&lane,points,2),"expression lane");
        p=engine_instrument_default_params(voices[v]);CHECK(engine_instrument_source_set_midi_clip(source,0,frames,voices[v],p,notes,note_count,NULL,0,&lane,1),"source publish");
        float* whole=calloc((size_t)frames*2,sizeof(float));float* split=calloc((size_t)frames*2,sizeof(float));CHECK(whole&&split,"buffers");
        engine_instrument_source_reset(source,48000,2);engine_instrument_source_render(source,whole,frames,0);
        engine_instrument_source_reset(source,48000,2);
        for (int f=0;f<(int)frames;f+=128)engine_instrument_source_render(source,split+f*2,frames-f<128?frames-f:128,f);
        CHECK(!memcmp(whole,split,(size_t)frames*2*sizeof(float)),"block-independent dynamics");
        if (v>=3) {
            for (uint32_t i=0;i<frames*2;++i) CHECK(isfinite(whole[i]) && fabsf(whole[i])<1,"finite unclipped overlapping sample notes");
            for (uint32_t i=(frames-24000)*2;i<frames*2;++i) CHECK(whole[i]==0,"retrigger and overlap tails reach silence");
        }
        free(whole);free(split);engine_automation_lane_free(&lane);engine_instrument_source_destroy(source);
    }
    SDL_Quit();puts("sampled_winds_test: pitch/rate coverage, sustain, dynamics, three-layer continuity, gates and partitions passed");return 0;
}
