#include "engine/instrument.h"
#include "engine/piano_expression.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("piano_expression_test",(v),(m))
// Measures an audible window from the complete prepared piano source.
static double energy(const float* x,int first,int count) { double e=0;for(int i=first;i<first+count;++i)e+=x[i]*x[i];return e/count; }
// Qualifies independent key/damper release, live sustain, mechanical coverage and bounded resonance.
int main(void) {
    CHECK(engine_piano_expression_init(),"mechanical bank complete");
    EngineInstrumentPresetId preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO;
    EngineInstrumentParams p=engine_instrument_default_params(preset);p.string_resonance=0;p.release_noise=0;
    EngineInstrumentSource* s=engine_instrument_source_create();CHECK(s!=NULL,"source");
    EngineMidiNote note={0,12000,60,.8f};EngineAutomationLane pedal;engine_automation_lane_init(&pedal,ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN_PEDAL);
    CHECK(engine_automation_lane_insert_point(&pedal,0,1,NULL) && engine_automation_lane_insert_point(&pedal,72000,0,NULL),"pedal phrase");
    pedal.points[0].curve=ENGINE_AUTOMATION_CURVE_HOLD;pedal.before=pedal.after=ENGINE_AUTOMATION_BOUNDARY_HOLD;
    CHECK(engine_instrument_source_set_midi_clip(s,0,144000,preset,p,&note,1,NULL,0,&pedal,1),"publish pedal");
    CHECK(engine_instrument_source_piano_gate(s,0,60)==72000,"key-off captured until exact pedal-up");
    float* wet=calloc(144000,sizeof(float));float* dry=calloc(144000,sizeof(float));CHECK(wet&&dry,"buffers");
    engine_instrument_source_reset(s,48000,1);engine_instrument_source_render(s,wet,144000,0);
    CHECK(energy(wet,48000,2400)>1e-7 && energy(wet,96000,2400)==0,"pedal holds then damps");
    CHECK(engine_instrument_source_set_midi_clip(s,0,144000,preset,p,&note,1,NULL,0,NULL,0),"dry publish");
    engine_instrument_source_reset(s,48000,1);engine_instrument_source_render(s,dry,144000,0);
    CHECK(energy(dry,48000,2400)==0,"ordinary note-off independent of pedal");
    pedal.points[0].curve=ENGINE_AUTOMATION_CURVE_LINEAR;
    CHECK(engine_instrument_source_set_midi_clip(s,0,144000,preset,p,&note,1,NULL,0,&pedal,1),"ramp pedal");
    CHECK(engine_instrument_source_piano_gate(s,0,60)==12000,"release below binary threshold cannot resurrect");
    pedal.points[0].value=2; // Additive offset is clamped to one before mapping; use a base half-down state for threshold timing.
    p.sustain_pedal=.25f;pedal.points[0].value=1;pedal.points[1].value=-1;
    CHECK(engine_instrument_source_set_midi_clip(s,0,144000,preset,p,&note,1,NULL,0,&pedal,1),"linear threshold publish");
    uint64_t gate=engine_instrument_source_piano_gate(s,0,60);CHECK(gate>=17999 && gate<=18001,"sample-exact interpolated pedal threshold");
    p.sustain_pedal=0;
    // A live pedal keeps the original key gate and survives voice-list updates and rearticulation.
    note.duration_frames=UINT64_MAX;
    CHECK(engine_instrument_source_set_midi_clip(s,0,UINT64_MAX,preset,p,&note,1,NULL,0,NULL,0),"live publish");
    engine_instrument_source_piano_pedal(s,true,0);note.duration_frames=12000;
    CHECK(engine_instrument_source_set_midi_clip(s,0,UINT64_MAX,preset,p,&note,1,NULL,0,NULL,0),"live key-off");
    CHECK(engine_instrument_source_piano_gate(s,0,60)==UINT64_MAX,"live pedal retains released voice");
    engine_instrument_source_piano_pedal(s,false,72000);
    CHECK(engine_instrument_source_piano_gate(s,0,60)==72000,"live pedal-up uses render clock");
    CHECK(engine_instrument_source_set_midi_clip(s,0,UINT64_MAX,preset,p,&note,1,NULL,0,NULL,0) && engine_instrument_source_piano_gate(s,0,60)==72000,"release survives later updates");
    for(int n=21;n<=108;++n) {
        double e=0;for(int i=0;i<48000;++i){float x=engine_piano_key_release(n,.8f,1,1,48000,12000+i,12000);CHECK(isfinite(x)&&fabsf(x)<.1f,"quiet finite key mechanics");e+=x*x;}
        CHECK(e>1e-10,"chromatic release recording audible");CHECK(engine_piano_key_release(n,.8f,1,1,48000,60000,12000)==0,"mechanical endpoint silence");
    }
    EnginePianoExpression modes;bool held[88]={false};p.release_noise=0;p.string_resonance=1;p.sustain_pedal=1;
    engine_piano_expression_reset(&modes,48000);double ring=0;
    for(int i=0;i<24000;++i){float x=engine_piano_expression_process(&modes,p,i==0?.5f:0,held,i);ring+=x*x;CHECK(isfinite(x)&&fabsf(x)<1,"stable resonators");}
    CHECK(ring>1e-9,"open strings resonate after excitation");p.sustain_pedal=0;
    float last=1;for(int i=24000;i<144000;++i)last=engine_piano_expression_process(&modes,p,0,held,i);
    CHECK(last==0,"pedal-up resonance reaches exact silence");
    p.sustain_pedal=1;engine_piano_expression_process(&modes,p,.5f,held,144000);p.sustain_pedal=0;
    CHECK(engine_piano_expression_process(&modes,p,0,held,900000)==0,"transport jump clears ringing");
    // Export cuts lift resonant dampers even when authored automation remains down.
    p=engine_instrument_default_params(preset);p.sustain_pedal=1;p.string_resonance=1;
    note=(EngineMidiNote){0,12000,60,.8f};
    CHECK(engine_instrument_source_set_midi_clip(s,0,480000,preset,p,&note,1,NULL,0,NULL,0),"held export publish");
    engine_instrument_source_set_export_end(s,48000,144000);engine_instrument_source_reset(s,48000,1);
    memset(wet,0,144000*sizeof(float));engine_instrument_source_render(s,wet,144000,0);
    CHECK(energy(wet,132000,12000)==0,"export cut damps all piano layers to silence");
    // Exercise the real stopped audition owner, including a pedal pressed before the first key.
    EngineRuntimeConfig cfg;config_set_defaults(&cfg);cfg.sample_rate=48000;cfg.block_size=128;
    Engine* engine=engine_create(&cfg);CHECK(engine!=NULL,"audition engine");CHECK(engine_add_track(engine)==1,"audition track");
    p=engine_instrument_default_params(preset);
    CHECK(engine_midi_audition_pedal(engine,true),"pedal before key");
    CHECK(engine_midi_audition_note_on(engine,0,preset,p,60,.8f),"live key-on owner");
    engine->midi_audition_idle_frame=12000;CHECK(engine_midi_audition_note_off(engine,60),"live key-off owner");
    engine->midi_audition_idle_frame=48000;engine_midi_audition_retire(engine);
    CHECK(engine->midi_audition_notes.note_count==1 && engine->midi_audition_notes.notes[0].duration_frames==12000,"real owner retains key-off under sustain");
    CHECK(engine_midi_audition_pedal(engine,false),"real owner pedal-up");
    CHECK(engine_instrument_source_piano_gate(engine->midi_audition_source,0,60)==48000,"real owner uses current idle clock");
    engine->midi_audition_idle_frame=144000;engine_midi_audition_retire(engine);
    CHECK(engine->midi_audition_notes.note_count==0,"released sustain retires");
    CHECK(engine_midi_audition_pedal(engine,true),"pending pedal");engine_midi_audition_all_notes_off(engine);
    CHECK(!engine->midi_audition_pedal_down,"panic clears pending pedal");engine_destroy(engine);
    free(wet);free(dry);engine_automation_lane_free(&pedal);engine_instrument_source_destroy(s);
    puts("piano_expression_test: timeline/live pedal, threshold, independent releases, all-key mechanics and resonance cleanup passed");return 0;
}
