#include "engine/instrument.h"
#include "engine/instrument_percussion.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x,m) daw_test_expect("percussion_render_test",(x),(m))

// Checks real renderer/diagnostic parity across short triggers, seeks and arbitrary block boundaries.
static void parity(void) {
    const int rates[]={44100,48000,96000};
    for(int r=0;r<3;++r) for(int id=ENGINE_INSTRUMENT_PRESET_KICK_DRUM;id<=ENGINE_INSTRUMENT_PRESET_HAT_KIT;++id) {
        int n=rates[r]; float* whole=calloc(n,sizeof(float)); float* split=calloc(n,sizeof(float));
        EngineInstrumentParams p=engine_instrument_default_params(id);
        EngineInstrumentSource* s=engine_instrument_source_create();
        EngineMidiNote note={.note=id==ENGINE_INSTRUMENT_PRESET_SNARE_DRUM?50:36,.velocity=.8f,.duration_frames=48};
        CHECK(s && whole && split && engine_instrument_source_set_midi_clip(s,0,n,id,p,&note,1,NULL,0,NULL,0),"prepared hit");
        engine_instrument_source_reset(s,rates[r],1);
        engine_instrument_source_render(s,whole,n,0);
        engine_instrument_source_reset(s,rates[r],1);
        for(int at=0;at<n;at+=127) engine_instrument_source_render(s,split+at,n-at<127?n-at:127,at);
        CHECK(!memcmp(whole,split,n*sizeof(float)),"block-size independent noise and phase");
        float energy=0;
        for(int i=0;i<n;++i) {
            CHECK(isfinite(whole[i]) && fabsf(whole[i])<1,"bounded finite signal");
            CHECK(whole[i]==engine_instrument_note_sample(id,p,note.note,.8f,rates[r],i,48),"exact diagnostic/source parity");
            energy+=whole[i]*whole[i];
        }
        CHECK(energy>0,"audible energy");
        if(engine_instrument_percussion_one_shot(id)) {
            float later[128]={0}; int at=rates[r]/20;
            engine_instrument_source_render(s,later,128,at);
            CHECK(!memcmp(later,whole+at,sizeof(later)),"seek into natural decay reproduces stream");
            CHECK(fabsf(whole[at])>1e-7,"short MIDI trigger does not choke natural hit");
        } else {
            CHECK(whole[rates[r]/4]==0,"808 gate closes with bounded release");
        }
        engine_instrument_source_destroy(s);free(whole);free(split);
    }
}

// Measures pitch analytically against a separately integrated frequency trajectory.
static void pitch_integral(void) {
    EngineInstrumentParams p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_KICK_DRUM);
    p.tone=0;p.drive=0;p.click_mix=0;p.attack_ms=0;p.level=1;p.body_decay_ms=1000;
    const int rate=96000; const double hz=440, tau=.025, ratio=pow(2,28.0/12);
    double phase=0, previous=hz*ratio;
    for(int f=1;f<12000;++f) {
        double t=(double)f/rate, freq=hz*(1+(ratio-1)*exp(-t/tau));
        phase+=6.28318530717958647692*(previous+freq)*.5/rate;previous=freq;
        double expected=.32*sin(phase)*pow(.001,t);
        double actual=engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_KICK_DRUM,p,69,1,rate,f,48);
        CHECK(fabs(expected-actual)<.00002,"phase follows integrated pitch, not instantaneous f(t)*t");
    }
}

// Checks that every newly advertised control changes its sound and every voice finishes exactly.
static void controls(void) {
    for(int id=ENGINE_INSTRUMENT_PRESET_KICK_DRUM;id<=ENGINE_INSTRUMENT_PRESET_HAT_KIT;++id) {
        EngineInstrumentParams p=engine_instrument_default_params(id);
        for(int k=0;k<engine_instrument_preset_param_count(id);++k) {
            EngineInstrumentParamId param;EngineInstrumentParamSpec spec;
            CHECK(engine_instrument_preset_param_id_at(id,k,&param)&&engine_instrument_param_spec(param,&spec),"active control");
            if (param >= ENGINE_INSTRUMENT_PARAM_MONO) continue; // Multi-note articulation is qualified separately.
            EngineInstrumentParams q=engine_instrument_params_set(id,p,param,
                engine_instrument_params_get(p,param)==spec.max_value ? spec.min_value : spec.max_value);
            uint8_t pitch = id == ENGINE_INSTRUMENT_PRESET_HAT_KIT ? 46 : 36;
            double change=0;
            for(int f=0;f<96000;f+=23) {
                double a=engine_instrument_note_sample(id,p,pitch,.8f,48000,f,12000);
                double b=engine_instrument_note_sample(id,q,pitch,.8f,48000,f,12000);change+=fabs(a-b);
            }
            CHECK(change>.00001,"advertised percussion control changes rendered audio");
        }
        CHECK(engine_instrument_note_sample(id,p,36,1,48000,200000,480000)==0,"natural tail has exact silence");
    }
}

// Runs meaningful production rendering, phase, gate and control checks.
int main(void) { parity();pitch_integral();controls();puts("percussion_render_test: success");return 0; }
