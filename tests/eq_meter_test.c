#include "app_state.h"
#include "daw/eq_actions.h"
#include "daw/composition.h"
#include "../src/session/session_io_read_internal.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("eq_meter_test",(v),(m))
// Proves known-amplitude calibration, asymmetric channels, silence and exact threshold distinctions.
static void calibration(void){
    float* data=calloc(48000*2,sizeof(float));CHECK(data!=NULL,"samples");for(int f=0;f<48000;++f){data[f*2]=.5f*sinf(2*M_PI*1000*f/48000);data[f*2+1]=.25f*sinf(2*M_PI*1000*f/48000);}
    EngineBounceBuffer b={.data=data,.frame_count=48000,.sample_rate=48000,.channels=2};DawCompositionAnalysis a;
    CHECK(daw_composition_measure(&b,0,&a),"measurement");CHECK(a.channel_count==2 && fabs(a.channel_meters[0].peak_dbfs+6.020599913)<1e-5 && fabs(a.channel_meters[1].rms_dbfs+15.05149978)<1e-4,"known sine dBFS");
    CHECK(fabs(a.channel_meters[0].rms-.5/sqrt(2))<1e-7 && !a.clipped_samples && !a.overload_samples,"unweighted RMS");
    memset(data,0,48000*2*sizeof(float));CHECK(daw_composition_measure(&b,0,&a) && a.channel_meters[0].peak==0 && isinf(a.channel_meters[0].peak_dbfs) && a.channel_meters[0].peak_dbfs<0,"silence");
    float thresholds[]={0,0,-1,.5f,1,-.5f,1.25f,0};b.data=thresholds;b.frame_count=4;CHECK(daw_composition_measure(&b,0,&a),"thresholds");
    CHECK(a.clipped_samples==3 && a.overload_samples==1 && a.channel_meters[0].full_scale_samples==3 && a.channel_meters[0].overload_samples==1 && a.channel_meters[1].full_scale_samples==0,"full scale versus overload");
    b.channels=1;b.frame_count=8;CHECK(daw_composition_measure(&b,0,&a) && a.channel_count==1 && a.clipped_samples==3,"mono");thresholds[2]=NAN;CHECK(!daw_composition_measure(&b,0,&a),"nonfinite refusal");free(data);
}
// Proves raw and escaped Unicode agree and malformed/overflow strings refuse without lossy truncation.
static void unicode(void){
    const char* values[]={"\"Violin — café 🎻\"","\"Violin \\u2014 caf\\u00e9 \\ud83c\\udfbb\""};char text[128];
    for(int i=0;i<2;++i){JsonReader r;json_reader_init(&r,values[i],strlen(values[i]));CHECK(json_parse_string(&r,text,sizeof(text)) && !strcmp(text,"Violin — café 🎻"),"raw/escaped Unicode parity");}
    const char* bad[]={"\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0061\"","\"\\u0000\"","\"unfinished","\"\xc0\xaf\""};
    for(int i=0;i<6;++i){JsonReader r;json_reader_init(&r,bad[i],strlen(bad[i]));CHECK(!json_parse_string(&r,text,sizeof(text)),"malformed Unicode refusal");}
    JsonReader r;json_reader_init(&r,values[0],strlen(values[0]));CHECK(!json_parse_string(&r,text,8),"whole-character capacity refusal");
}
// Verifies common EQ DSP parity, stale history, unrelated scalar preservation and engine lifetime refusal.
int main(void){
    calibration();unicode();CHECK(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER)==0,"SDL");AppState* s=calloc(1,sizeof(*s));EngineRuntimeConfig cfg;config_set_defaults(&cfg);s->engine=engine_create(&cfg);CHECK(s->engine!=NULL,"engine");undo_manager_init(&s->undo);
    s->tempo=tempo_state_default(cfg.sample_rate);tempo_map_init(&s->tempo_map,cfg.sample_rate);int clip;
    CHECK(engine_add_midi_clip_to_track(s->engine,0,0,48000,&clip) && engine_clip_midi_add_note(s->engine,0,clip,(EngineMidiNote){0,24000,60,.5f},NULL),"source");
    uint64_t generation=engine_instance_id(s->engine),id=engine_get_tracks(s->engine)[0].runtime_id;DawEqSnapshot before,after;
    CHECK(daw_eq_get(s,generation,id,&before)==DAW_ACTION_UNCHANGED,"capture");DawCompositionRequest req={.start=0,.end=48000};DawCompositionRender base={0},got={0};CHECK(daw_action_render_composition(s,&req,&base)==DAW_ACTION_UNCHANGED,"base");
    EngineEqCurve curve=before.curve;curve.bands[1]=(EngineEqBand){true,1000,6,1};CHECK(daw_eq_set(s,&before,&curve)==DAW_ACTION_APPLIED,"publish");CHECK(daw_action_render_composition(s,&req,&got)==DAW_ACTION_UNCHANGED,"processed");
    EngineEqState reference={0};engine_eq_init(&reference,cfg.sample_rate,2);engine_eq_set_curve(&reference,&curve);engine_eq_process(&reference,base.audio.data,base.audio.frame_count,2);
    for(size_t i=0;i<got.audio.frame_count*2;++i)CHECK(fabsf(base.audio.data[i]-got.audio.data[i])<1e-6f,"independent EQ DSP parity");engine_eq_free(&reference);
    CHECK(daw_eq_get(s,generation,id,&after)==DAW_ACTION_UNCHANGED && engine_track_set_pan(s->engine,0,.25f),"unrelated pan");CHECK(daw_eq_history(s,&after,false)==DAW_ACTION_APPLIED && engine_get_tracks(s->engine)[0].pan==.25f,"scoped undo");
    CHECK(daw_eq_get(s,generation,id,&before)==DAW_ACTION_UNCHANGED && daw_eq_history(s,&before,true)==DAW_ACTION_APPLIED,"redo");
    CHECK(daw_eq_get(s,generation,id,&after)==DAW_ACTION_UNCHANGED,"current");curve.bands[1].gain_db=-3;CHECK(engine_set_track_eq_curve(s->engine,0,&curve),"native changed curve");CHECK(daw_eq_set(s,&after,&after.curve)==DAW_ACTION_STALE_TARGET,"stale write");
    CHECK(daw_eq_get(s,generation,id,&before)==DAW_ACTION_UNCHANGED && daw_eq_history(s,&before,false)==DAW_ACTION_STALE_TARGET,"stale history");
    UndoEqCurveEdit old=s->undo.undo_stack[s->undo.undo_count-1].data.eq_curve_edit;engine_destroy(s->engine);s->engine=engine_create(&cfg);CHECK(!daw_eq_restore(s,&old,false),"replacement lifetime guard");
    daw_composition_render_free(&base);daw_composition_render_free(&got);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);engine_destroy(s->engine);free(s);SDL_Quit();puts("eq_meter_test: passed (known sine/silence/threshold calibration, Unicode decoding, EQ DSP parity and exact scoped lifetime/history guards)");return 0;
}
