#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/groups.h"
#include "daw/routing_actions.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/track_group_panel.h"
#include "input/timeline/timeline_geometry.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Instruments only prepared graph allocations and detects any nested render allocation.
static int allocation_fail=-1;static bool rendering;static int render_allocations;
// Supplies a deterministic preparation failure without modifying production DSP.
static void* allocate(size_t count,size_t bytes) { if(rendering)++render_allocations;
    if(allocation_fail==0)return NULL;if(allocation_fail>0)--allocation_fail;return calloc(count,bytes); }
#define calloc allocate
#include "../src/engine/group_mix.c"
#undef calloc
// Captures complete guards through the common application owner.
static EngineRoutingSnapshot capture(AppState* s) {EngineRoutingSnapshot r;assert(daw_action_get_routing(s,&r)==DAW_ACTION_UNCHANGED);return r;}
// Renders original-level production audio independently of live musical state.
static EngineBounceBuffer bounce(AppState* s,int isolated) {EngineBounceOptions o=engine_bounce_options_default();o.normalize_if_clipping=false;
    o.isolate_track=isolated>=0;o.track_index=isolated;EngineBounceBuffer out={0};assert(engine_bounce_range_to_buffer_with_options(s->engine,0,12000,&o,NULL,NULL,&out));return out;}
// Checks an independently derived multiplicative signal factor sample by sample.
static void factor(EngineBounceBuffer* base,EngineBounceBuffer* out,float value) {
    assert(base->frame_count==out->frame_count);for(size_t n=0;n<out->frame_count*out->channels;++n)
        if(fabsf(out->data[n]-base->data[n]*value)>1e-6f){fprintf(stderr,"factor %f sample %zu: %f vs %f\n",value,n,out->data[n],base->data[n]*value);abort();}
}
// Checks independent stereo balance factors for a child inside a panned parent.
static void stereo(EngineBounceBuffer* base,EngineBounceBuffer* out,float left,float right) {
    for(size_t frame=0;frame<out->frame_count;++frame){assert(fabsf(out->data[frame*2]-base->data[frame*2]*left)<1e-6f);
        assert(fabsf(out->data[frame*2+1]-base->data[frame*2+1]*right)<1e-6f);}
}
// Models actual latency independently of the compensation graph.
struct FxHandle {float ring[64];unsigned write,delay;};
// Describes a real three-sample delay.
static int delay3(FxDesc* d){*d=(FxDesc){.name="delay3",.api_version=FX_API_VERSION,.flags=FX_FLAG_INPLACE_OK|FX_FLAG_HAS_LATENCY,.num_inputs=1,.num_outputs=1,.latency_samples=3};return 1;}
// Describes a real five-sample delay.
static int delay5(FxDesc* d){delay3(d);d->latency_samples=5;return 1;}
// Describes a real four-sample delay.
static int delay4(FxDesc* d){delay3(d);d->latency_samples=4;return 1;}
// Delays interleaved PCM using an independent stereo ring index.
static void delay_process(FxHandle* h,const float* in,float* out,int frames,int channels){for(int f=0;f<frames;++f){unsigned read=(h->write+32-h->delay)%32;
    for(int ch=0;ch<channels;++ch){float v=in[f*channels+ch];out[f*channels+ch]=h->ring[read*2+ch];h->ring[h->write*2+ch]=v;}h->write=(h->write+1)%32;}}
// Clears the fixture DSP's own delayed history.
static void delay_reset(FxHandle* h){memset(h->ring,0,sizeof(h->ring));h->write=0;}
// Frees one fixture-owned delay handle.
static void delay_free(FxHandle* h){free(h);}
// Creates the delay effect separately from the graph's latency calculations.
static int delay_create(const FxDesc* d,FxHandle** h,FxVTable* v,uint32_t rate,uint32_t block,uint32_t channels){(void)rate;(void)block;(void)channels;
    *h=calloc(1,sizeof(**h));if(!*h)return 0;(*h)->delay=d->latency_samples;*v=(FxVTable){.process=delay_process,.reset=delay_reset,.destroy=delay_free};return 1;}
// Adds a half-gain processor using the existing child/group/master effects interface.
static void half(AppState* s,int t) {FxInstId id=t<0 ? engine_fx_master_add(s->engine,1) : engine_fx_track_add(s->engine,t,1);assert(id);
    assert(t<0 ? engine_fx_master_set_param(s->engine,id,0,-6.020599913f) : engine_fx_track_set_param(s->engine,t,id,0,-6.020599913f));}
// Creates one guarded group and confirms one undo/redo transition with retained identities.
static int group(AppState* s,const char* name) {EngineRoutingSnapshot before=capture(s),out;int undo=s->undo.undo_count;
    assert(daw_action_create_group(s,&before,name,&out)==DAW_ACTION_APPLIED && s->undo.undo_count==undo+1);
    int index=out.track_count-1;uint64_t id=out.tracks[index].track_id,persistent=engine_get_tracks(s->engine)[index].persistent_id;
    assert(daw_action_routing_history(s,&out,false,&before)==DAW_ACTION_APPLIED);
    assert(daw_action_routing_history(s,&before,true,&out)==DAW_ACTION_APPLIED && out.tracks[index].track_id==id && engine_get_tracks(s->engine)[index].persistent_id==persistent);return index;
}
// Qualifies actual nested processing, serial PDC, folding, history, persistence and refusal boundaries.
int main(int argc,char** argv) {assert(argc==2);assert(!SDL_Init(SDL_INIT_EVENTS) && !TTF_Init());assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    undo_manager_init(&s->undo);input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s,1600,1000);s->timeline_visible_seconds=10;s->timeline_vertical_scale=1;
    assert(engine_add_track(s->engine)==1);for(int t=0;t<2;++t){assert(engine_add_midi_clip_to_track(s->engine,t,0,12000,NULL));assert(engine_clip_midi_add_note(s->engine,t,0,(EngineMidiNote){0,9600,60,.5f},NULL));}
    EngineBounceBuffer base=bounce(s,0);
    int strings=group(s,"Strings"),theme=group(s,"Major Theme"),winds=group(s,"Woodwinds"),brass=group(s,"Brass");assert(strings==2 && theme==3 && winds==4 && brass==5);
    EngineRoutingSnapshot before=capture(s),out;assert(daw_action_create_return(s,&before,&out)==DAW_ACTION_APPLIED);int room=out.track_count-1;
    before=out;EngineRoutingSnapshot next=before;next.tracks[0].parent_id=next.tracks[theme].track_id;next.tracks[1].parent_id=next.tracks[strings].track_id;next.tracks[theme].parent_id=next.tracks[strings].track_id;
    next.tracks[strings].sends[0]=(EngineSend){next.tracks[room].track_id,.25f};assert(daw_action_set_routing(s,&before,&next,&out)==DAW_ACTION_APPLIED);
    EngineRoutingSnapshot bad=out;bad.tracks[strings].parent_id=bad.tracks[theme].track_id;assert(daw_action_set_routing(s,&out,&bad,&before)==DAW_ACTION_INVALID);
    bad=out;bad.tracks[room].parent_id=bad.tracks[strings].track_id;assert(daw_action_set_routing(s,&out,&bad,&before)==DAW_ACTION_INVALID);
    assert(!engine_remove_track(s->engine,strings) && !engine_add_midi_clip_to_track(s->engine,strings,0,100,NULL));
    half(s,0);half(s,theme);half(s,strings);half(s,room);half(s,-1);
    EngineBounceBuffer full=bounce(s,-1);factor(&base,&full,.3515625f);engine_bounce_buffer_free(&full);
    assert(engine_track_set_pan(s->engine,strings,.5f) && engine_track_set_pan(s->engine,0,-.5f));
    full=bounce(s,-1);stereo(&base,&full,.17578125f,.31640625f);engine_bounce_buffer_free(&full);
    assert(engine_track_set_pan(s->engine,strings,0) && engine_track_set_pan(s->engine,0,0) && engine_track_set_gain(s->engine,strings,.8f));
    full=bounce(s,-1);factor(&base,&full,.28125f);engine_bounce_buffer_free(&full);assert(engine_track_set_gain(s->engine,strings,1));
    full=bounce(s,theme);factor(&base,&full,.125f);engine_bounce_buffer_free(&full);
    full=bounce(s,strings);factor(&base,&full,.3125f);engine_bounce_buffer_free(&full);
    assert(engine_track_set_solo(s->engine,theme,true));full=bounce(s,-1);factor(&base,&full,.0703125f);engine_bounce_buffer_free(&full);
    assert(engine_track_set_solo(s->engine,theme,false) && engine_track_set_muted(s->engine,strings,true));full=bounce(s,-1);factor(&base,&full,0);engine_bounce_buffer_free(&full);assert(engine_track_set_muted(s->engine,strings,false));
    before=capture(s);next=before;next.tracks[strings].folded=true;assert(daw_action_set_routing(s,&before,&next,&out)==DAW_ACTION_APPLIED);
    assert(engine_group_visible_count(engine_get_tracks(s->engine),out.track_count)==4 && engine_group_visible_row(engine_get_tracks(s->engine),out.track_count,0)==-1);
    full=bounce(s,-1);factor(&base,&full,.3515625f);engine_bounce_buffer_free(&full);
    assert(daw_action_routing_history(s,&out,false,&before)==DAW_ACTION_APPLIED);
    assert(engine_group_visible_track(engine_get_tracks(s->engine),before.track_count,0)==strings && engine_group_visible_track(engine_get_tracks(s->engine),before.track_count,3)==0);
    next=before;next.tracks[brass].sibling_order=1;assert(daw_action_set_routing(s,&before,&next,&out)==DAW_ACTION_APPLIED);
    EngineRoutingSnapshot saved=out;uint64_t ids[ENGINE_ROUTING_TRACK_CAP];for(int t=0;t<out.track_count;++t)ids[t]=engine_get_tracks(s->engine)[t].persistent_id;
    assert(session_save_to_file(s,argv[1]));assert(session_load_from_file(s,argv[1]));before=capture(s);assert(before.track_count==saved.track_count);
    for(int t=0;t<before.track_count;++t)assert(engine_get_tracks(s->engine)[t].persistent_id==ids[t]);
    full=bounce(s,-1);factor(&base,&full,.3515625f);engine_bounce_buffer_free(&full);
    Engine* audition=engine_create(&s->runtime_cfg);assert(audition);
    assert(engine_add_midi_clip_to_track(audition,0,0,12000,NULL));
    EngineInstrumentParams params=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    assert(engine_midi_audition_note_on(audition,0,ENGINE_INSTRUMENT_PRESET_PURE_SINE,params,60,.5f));
    assert(engine_midi_audition_note_on(s->engine,0,ENGINE_INSTRUMENT_PRESET_PURE_SINE,params,60,.5f));
    float live_base[8192*2]={0},live_group[8192*2]={0},live_scratch[256];
    for(int offset=0;offset<8192;offset+=128){engine_mix_midi_audition_only(audition,offset,128,live_base+offset*2,live_scratch,2);
        engine_mix_midi_audition_only(s->engine,offset,128,live_group+offset*2,live_scratch,2);}
    // Compare after the existing live FX parameter ramps have settled.
    EngineBounceBuffer a={.data=live_base+4096*2,.frame_count=4096,.channels=2},b={.data=live_group+4096*2,.frame_count=4096,.channels=2};factor(&a,&b,.0703125f);
    assert(engine_midi_audition_note_off(s->engine,60));engine_destroy(audition);
    FxRegistryEntry registry[]={{.id=9911,.name="d3",.get_desc=delay3,.create=delay_create},{.id=9912,.name="d5",.get_desc=delay5,.create=delay_create},{.id=9913,.name="d4",.get_desc=delay4,.create=delay_create}};
    assert(fxm_register_builtin(s->engine->fxm,registry,3));assert(engine_fx_track_add(s->engine,0,9911) && engine_fx_track_add(s->engine,theme,9912) && engine_fx_track_add(s->engine,strings,9912) && engine_fx_track_add(s->engine,room,9912) && engine_fx_master_add(s->engine,9913));
    full=bounce(s,-1);factor(&base,&full,.3515625f);engine_bounce_buffer_free(&full);
    EngineSourcePlan* plan=engine_source_plan_prepare_export(s->engine,12000,0);assert(plan && engine_source_plan_export_latency(plan)==22);
    float output[256],scratch[256];rendering=true;assert(engine_source_plan_render_export(plan,0,128,output,scratch,2));rendering=false;assert(!render_allocations);engine_source_plan_discard(s->engine,plan);
    before=capture(s);next=before;next.tracks[0].parent_id=next.tracks[strings].track_id;allocation_fail=0;
    assert(daw_action_set_routing(s,&before,&next,&out)==DAW_ACTION_ENGINE_REJECTED);allocation_fail=-1;out=capture(s);assert(engine_routing_snapshot_equal(&before,&out));
    full=bounce(s,-1);factor(&base,&full,.3515625f);engine_bounce_buffer_free(&full);
    before=capture(s);next=before;
    next.tracks[strings].volume_automation=(EngineMixerAutomation){.count=2,.before=ENGINE_AUTOMATION_BOUNDARY_HOLD,.after=ENGINE_AUTOMATION_BOUNDARY_HOLD,.points={{0,0,ENGINE_AUTOMATION_CURVE_LINEAR},{4096,1,ENGINE_AUTOMATION_CURVE_LINEAR}}};
    next.tracks[strings].sends[0].automation=(EngineMixerAutomation){.count=2,.before=ENGINE_AUTOMATION_BOUNDARY_HOLD,.after=ENGINE_AUTOMATION_BOUNDARY_HOLD,.points={{0,0,ENGINE_AUTOMATION_CURVE_LINEAR},{4096,.5f,ENGINE_AUTOMATION_CURVE_LINEAR}}};
    assert(daw_action_set_routing(s,&before,&next,&out)==DAW_ACTION_APPLIED);full=bounce(s,-1);
    for(int f=0;f<12000;++f){double g=f<4096?(double)f/4096:1;double gain=.3125*g*(1+.25*g);for(int ch=0;ch<2;++ch)assert(fabs(full.data[f*2+ch]-base.data[f*2+ch]*gain)<1e-6);}
    engine_bounce_buffer_free(&full);plan=engine_source_plan_prepare_export(s->engine,12000,0);assert(plan && engine_source_plan_export_latency(plan)==22);rendering=true;assert(engine_source_plan_render_export(plan,0,128,output,scratch,2));rendering=false;assert(!render_allocations);engine_source_plan_discard(s->engine,plan);
    engine_bounce_buffer_free(&base);undo_manager_free(&s->undo);engine_destroy(s->engine);free(s);ui_font_shutdown();TTF_Quit();SDL_Quit();
    puts("track_groups_test: passed (nested sums, own/group/master FX exactly once, isolation, inherited solo/mute, 22-frame serial PDC, fold parity, persistent IDs, undo/redo, cycle/reference guards and preparation rollback)");
}
