#include "engine/engine_internal.h"
#include "engine/routing.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("engine_routing_test",(v),(m))
static bool reject_publication,rendering;
static int publications,render_allocations,fail_after=-1;
// Injects refusal at the real routing publication boundary.
static bool publish(Engine* engine){++publications;return !reject_publication && engine_request_rebuild_sources(engine);}
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_routing.c"
#undef engine_request_rebuild_sources
// Injects allocation failure only into independently prepared source plans.
static bool fail(void){if(rendering)++render_allocations;if(fail_after<0)return false;if(!fail_after)return true;--fail_after;return false;}
// Instruments private source-plan allocation and counts new render boundary allocations.
static void* allocate(size_t size){return fail()?NULL:malloc(size);}
// Instruments zeroed private source-plan allocation.
static void* zeroed(size_t count,size_t size){return fail()?NULL:calloc(count,size);}
#define malloc allocate
#define calloc zeroed
#include "../src/engine/engine_source_plan.c"
#include "../src/engine/engine_audio.c"
#undef malloc
#undef calloc

// Models a latency-bearing effect independent of engine compensation calculations.
struct FxHandle{float ring[32];unsigned write,delay;};
// Describes a three-sample source delay.
static int source_delay(FxDesc* d){*d=(FxDesc){.name="source delay",.api_version=FX_API_VERSION,.flags=FX_FLAG_INPLACE_OK|FX_FLAG_HAS_LATENCY,.num_inputs=1,.num_outputs=1,.latency_samples=3};return 1;}
// Describes a five-sample return delay.
static int return_delay(FxDesc* d){source_delay(d);d->latency_samples=5;return 1;}
// Describes a four-sample master delay.
static int master_delay(FxDesc* d){source_delay(d);d->latency_samples=4;return 1;}
// Delays actual signal samples with an independently indexed stereo ring.
static void process_delay(FxHandle* h,const float* in,float* out,int frames,int channels){for(int n=0;n<frames;++n){unsigned read=(h->write+16-h->delay)%16;for(int ch=0;ch<channels;++ch){float value=in[n*channels+ch];out[n*channels+ch]=h->ring[read*2+ch];h->ring[h->write*2+ch]=value;}h->write=(h->write+1)%16;}}
// Resets the independent fixture's actual delay memory.
static void reset_delay(FxHandle* h){memset(h->ring,0,sizeof(h->ring));h->write=0;}
// Frees one independently owned fixture effect.
static void free_delay(FxHandle* h){free(h);}
// Prepares a fixed-latency fixture effect without production render allocation.
static int create_delay(const FxDesc* d,FxHandle** h,FxVTable* v,uint32_t rate,uint32_t block,uint32_t channels){(void)rate;(void)block;(void)channels;*h=calloc(1,sizeof(**h));if(!*h)return 0;(*h)->delay=d->latency_samples;*v=(FxVTable){.process=process_delay,.reset=reset_delay,.destroy=free_delay};return 1;}
// Builds two identical musical sources and one initially ordinary empty lane.
static Engine* fixture(void){EngineRuntimeConfig cfg;config_set_defaults(&cfg);cfg.sample_rate=48000;Engine* e=engine_create(&cfg);CHECK(e!=NULL,"engine");
    CHECK(engine_add_track(e)==1 && engine_add_track(e)==2,"lanes");
    for(int t=0;t<2;++t){CHECK(engine_add_midi_clip_to_track(e,t,0,12000,NULL),"region");CHECK(engine_clip_midi_add_note(e,t,0,(EngineMidiNote){0,9600,60,.5f},NULL),"note");}
    return e;
}
// Captures one complete bounded routing precondition.
static EngineRoutingSnapshot capture(Engine* e){EngineRoutingSnapshot out;CHECK(engine_routing_capture(e,&out),"capture");return out;}
// Renders private production audio while retaining the original authored levels.
static EngineBounceBuffer bounce(Engine* e,const EngineBounceOptions* options){EngineBounceBuffer out={0};CHECK(engine_bounce_range_to_buffer_with_options(e,0,12000,options,NULL,NULL,&out),"production bounce");return out;}
// Checks independently expected output factors rather than comparing the mixer to itself.
static void factor(const EngineBounceBuffer* source,const EngineBounceBuffer* output,float gain,const char* message){CHECK(source->frame_count==output->frame_count && source->channels==output->channels,message);for(size_t n=0;n<source->frame_count*(size_t)source->channels;++n)CHECK(fabsf(output->data[n]-source->data[n]*gain)<1e-6f,message);}
// Qualifies shared post-fader sums, return gain/solo/mute, isolated/dry capture and serial latency.
static void routing_signal(void){Engine* e=fixture();EngineBounceOptions one=engine_bounce_options_default();one.isolate_track=true;one.track_index=0;one.normalize_if_clipping=false;
    EngineBounceBuffer base=bounce(e,&one);EngineRoutingSnapshot before=capture(e),next=before;next.tracks[2].is_return=true;next.tracks[0].sends[0]=(EngineSend){next.tracks[2].track_id,.5f};next.tracks[1].sends[0]=(EngineSend){next.tracks[2].track_id,.25f};
    CHECK(engine_routing_set(e,&before,&next),"shared routing");FxInstId gain=engine_fx_track_add(e,2,1);CHECK(gain && engine_fx_track_set_param(e,2,gain,0,-6.020599913f),"return gain effect");
    EngineBounceOptions options=engine_bounce_options_default();options.normalize_if_clipping=false;EngineBounceBuffer full=bounce(e,&options);factor(&base,&full,2.375f,"two source shared return sum");engine_bounce_buffer_free(&full);
    options.bypass_track_fx=options.bypass_master_fx=true;full=bounce(e,&options);factor(&base,&full,2,"dry bypass removes return duplication");engine_bounce_buffer_free(&full);
    options=one;full=bounce(e,&options);factor(&base,&full,1.25f,"source audition keeps its return effects");engine_bounce_buffer_free(&full);
    options.track_index=2;full=bounce(e,&options);factor(&base,&full,.375f,"return-only audition preserves contributing sources and processing");engine_bounce_buffer_free(&full);
    options.bypass_track_fx=options.bypass_master_fx=true;full=bounce(e,&options);factor(&base,&full,.75f,"dry return audition captures its input sum");engine_bounce_buffer_free(&full);
    CHECK(engine_track_set_solo(e,2,true),"return solo");full=bounce(e,NULL);factor(&base,&full,.375f,"solo return receives sources without dry sum");engine_bounce_buffer_free(&full);
    CHECK(engine_track_set_solo(e,2,false) && engine_track_set_solo(e,0,true),"source solo");full=bounce(e,NULL);factor(&base,&full,1.25f,"source solo propagates through return");engine_bounce_buffer_free(&full);
    CHECK(engine_track_set_solo(e,0,false) && engine_track_set_muted(e,2,true),"return mute");full=bounce(e,NULL);factor(&base,&full,2,"muted return leaves dry sources");engine_bounce_buffer_free(&full);
    CHECK(engine_track_set_muted(e,2,false) && engine_track_set_gain(e,0,.5f),"source fader");full=bounce(e,NULL);factor(&base,&full,1.75f,"send follows source fader");engine_bounce_buffer_free(&full);CHECK(engine_track_set_gain(e,0,1),"restore fader");
    FxRegistryEntry entries[]={{.id=9911,.name="source delay",.get_desc=source_delay,.create=create_delay},{.id=9912,.name="return delay",.get_desc=return_delay,.create=create_delay},{.id=9913,.name="master delay",.get_desc=master_delay,.create=create_delay}};
    CHECK(fxm_register_builtin(e->fxm,entries,3),"latency fixtures");CHECK(engine_fx_track_add(e,0,9911) && engine_fx_track_add(e,2,9912) && engine_fx_master_add(e,9913),"serial delay stages");
    full=bounce(e,NULL);factor(&base,&full,2.375f,"production source/return/master PDC remains sample aligned");engine_bounce_buffer_free(&full);
    EngineSourcePlan* plan=engine_source_plan_prepare_export(e,12000,0);CHECK(plan!=NULL && engine_source_plan_export_latency(plan)==12,"serial latency readback");
    float out[256],scratch[256];rendering=true;CHECK(engine_source_plan_render_export(plan,0,128,out,scratch,2),"prepared render");rendering=false;CHECK(!render_allocations,"no new routing render allocation");engine_source_plan_discard(e,plan);
    engine_bounce_buffer_free(&base);engine_destroy(e);
}
// Qualifies independent parallel returns and explicit topology capacity boundaries.
static void parallel_returns(void) {
    Engine* e = fixture();
    CHECK(engine_add_track(e) == 3, "second return lane");
    EngineBounceOptions options = engine_bounce_options_default();
    options.isolate_track = true;
    options.track_index = 0;
    options.normalize_if_clipping = false;
    EngineBounceBuffer base = bounce(e, &options);
    EngineRoutingSnapshot before = capture(e), next = before;
    next.tracks[2].is_return = next.tracks[3].is_return = true;
    next.tracks[0].sends[0] = (EngineSend){next.tracks[2].track_id, .5f};
    next.tracks[0].sends[3] = (EngineSend){next.tracks[3].track_id, .25f};
    next.tracks[1].sends[1] = (EngineSend){next.tracks[3].track_id, .5f};
    CHECK(engine_routing_set(e, &before, &next), "parallel return routing with sparse slots");
    FxInstId gain = engine_fx_track_add(e, 3, 1);
    CHECK(gain && engine_fx_track_set_param(e, 3, gain, 0, -6.020599913f), "independent second return processing");
    options.isolate_track = false;
    EngineBounceBuffer full = bounce(e, &options);
    factor(&base, &full, 2.875f, "parallel return sums with independent processing");
    engine_bounce_buffer_free(&full);
    options.isolate_track = true;
    options.track_index = 3;
    full = bounce(e, &options);
    factor(&base, &full, .375f, "selected return excludes the other bus");
    engine_bounce_buffer_free(&full);
    before = capture(e);
    int count = publications;
    CHECK(engine_routing_set(e, &before, &before) && publications == count, "exact no-op avoids publication");
    next = before;
    next.tracks[0].sends[0].return_id = UINT64_MAX;
    CHECK(!engine_routing_set(e, &before, &next), "missing stable destination refused");
    next = before;
    next.tracks[0].sends[0].gain = 1.001f;
    CHECK(!engine_routing_set(e, &before, &next), "out-of-range send refused");
    next = before;
    next.tracks[2].is_return = false;
    CHECK(!engine_routing_set(e, &before, &next), "bound destination role cannot be removed");
    next = before;
    next.tracks[2].sends[0] = (EngineSend){next.tracks[3].track_id, .5f};
    CHECK(!engine_routing_set(e, &before, &next), "return-to-return routing refused");
    engine_bounce_buffer_free(&base);
    engine_destroy(e);
}
// Qualifies exact guards, invalid topology, occupied return refusal and actual preparation rollback.
static void boundaries(void){Engine* e=fixture();EngineRoutingSnapshot before=capture(e),next=before;next.tracks[2].is_return=true;next.tracks[0].sends[0]=(EngineSend){next.tracks[2].track_id,.5f};
    reject_publication=true;CHECK(!engine_routing_set(e,&before,&next),"publication refusal");reject_publication=false;EngineRoutingSnapshot current=capture(e);CHECK(!memcmp(&current,&before,sizeof(current)),"routing rollback");
    int failures=0;for(int point=0;point<100;++point){fail_after=point;bool accepted=engine_routing_set(e,&before,&next);fail_after=-1;if(accepted)break;++failures;current=capture(e);CHECK(!memcmp(&current,&before,sizeof(current)),"prepared allocation failure retains authored routing");}CHECK(failures>=7,"new return preparation failure coverage");
    current=capture(e);CHECK(current.tracks[2].is_return && !engine_routing_set(e,&before,&next),"old routing guard stale");
    CHECK(!engine_remove_track(e,2) && !engine_add_midi_clip_to_track(e,2,0,100,NULL),"referenced return protected from deletion/source insertion");
    CHECK(!engine_track_midi_set_instrument_enabled(e,2,true),"return refuses instrument enable");
    CHECK(!engine_midi_audition_note_on(e,2,ENGINE_INSTRUMENT_PRESET_PURE_SINE,engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE),60,.5f),"return refuses instrument audition");
    next=current;next.tracks[2].sends[0]=(EngineSend){current.tracks[2].track_id,.1f};CHECK(!engine_routing_set(e,&current,&next),"feedback refused");
    next=current;next.tracks[0].sends[1]=next.tracks[0].sends[0];CHECK(!engine_routing_set(e,&current,&next),"duplicate send refused");
    next=current;next.tracks[0].sends[0].gain=NAN;CHECK(!engine_routing_set(e,&current,&next),"nonfinite refused");
    next=current;next.tracks[0].is_return=true;CHECK(!engine_routing_set(e,&current,&next),"occupied source cannot become return");
    next=current;memset(next.tracks[0].sends,0,sizeof(next.tracks[0].sends));CHECK(engine_routing_set(e,&current,&next) && engine_remove_track(e,2),"explicit disconnect allows return removal");
    engine_destroy(e);
}
// Runs contained engine routing qualification without user projects or audio devices.
int main(void){CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER),"SDL");routing_signal();parallel_returns();boundaries();SDL_Quit();puts("engine_routing_test: success (shared returns, post-fader/dry/isolated/solo/mute, exact production PDC, source protection, atomic rollback and prepared allocation failures)");return 0;}
