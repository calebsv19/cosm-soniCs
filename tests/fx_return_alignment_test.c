#include "effects/effects_manager.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static bool rendering;
static int render_allocations;
// Counts allocation attempts inside the production effects-manager render boundary.
static void* counted_malloc(size_t size) { if(rendering)++render_allocations;return malloc(size); }
// Counts zeroed allocation attempts inside the production effects-manager render boundary.
static void* counted_calloc(size_t count,size_t size) { if(rendering)++render_allocations;return calloc(count,size); }
// Counts resized allocation attempts inside the production effects-manager render boundary.
static void* counted_realloc(void* pointer,size_t size) { if(rendering)++render_allocations;return realloc(pointer,size); }
#define malloc counted_malloc
#define calloc counted_calloc
#define realloc counted_realloc
#include "../src/effects/effects_manager.c"
#undef malloc
#undef calloc
#undef realloc

// Models an independently delayed serial processor for exact signal-alignment expectations.
struct FxHandle { float samples[32]; uint32_t write,delay; };
// Produces a latency-bearing fixture descriptor independent of compensation implementation.
static int delayed_desc(FxDesc* desc,uint32_t delay) { *desc=(FxDesc){.name="Delay fixture",.api_version=FX_API_VERSION,.flags=FX_FLAG_INPLACE_OK|FX_FLAG_HAS_LATENCY,.num_inputs=1,.num_outputs=1,.latency_samples=delay};return 1; }
// Describes the three-sample source stage.
static int source_desc(FxDesc* desc) { return delayed_desc(desc,3); }
// Describes the five-sample return stage.
static int long_return_desc(FxDesc* desc) { return delayed_desc(desc,5); }
// Describes the two-sample parallel return stage.
static int short_return_desc(FxDesc* desc) { return delayed_desc(desc,2); }
// Describes the four-sample final master stage.
static int master_desc(FxDesc* desc) { return delayed_desc(desc,4); }
// Delays actual samples independently of the manager's alignment calculations.
static void delayed_process(FxHandle* handle,const float* in,float* out,int frames,int channels) {
    for(int n=0;n<frames;++n){uint32_t read=(handle->write+16-handle->delay)%16;
        for(int ch=0;ch<channels;++ch){float value=in[n*channels+ch];out[n*channels+ch]=handle->samples[read*2+ch];handle->samples[handle->write*2+ch]=value;}
        handle->write=(handle->write+1)%16;}
}
// Resets the independently delayed fixture at explicit discontinuities.
static void delayed_reset(FxHandle* handle) { memset(handle->samples,0,sizeof(handle->samples));handle->write=0; }
// Releases one fixture-owned DSP history.
static void delayed_destroy(FxHandle* handle) { free(handle); }
// Creates one fixture DSP instance with the published fixed latency.
static int delayed_create(const FxDesc* desc,FxHandle** handle,FxVTable* table,uint32_t rate,uint32_t block,uint32_t channels) {
    (void)rate;(void)block;(void)channels;*handle=calloc(1,sizeof(**handle));if(!*handle)return 0;(*handle)->delay=desc->latency_samples;
    *table=(FxVTable){.process=delayed_process,.reset=delayed_reset,.destroy=delayed_destroy};return 1;
}
// Prepares two independent source rows, two shared returns and one serial master effect.
static EffectsManager* control(void) {
    FxConfig config={.sample_rate=48000,.max_block=64,.max_channels=2};EffectsManager* manager=fxm_create(&config);
    FxRegistryEntry entries[]={{.id=9901,.name="Source delay",.get_desc=source_desc,.create=delayed_create},
        {.id=9902,.name="Long return",.get_desc=long_return_desc,.create=delayed_create},
        {.id=9903,.name="Short return",.get_desc=short_return_desc,.create=delayed_create},
        {.id=9904,.name="Master delay",.get_desc=master_desc,.create=delayed_create}};
    assert(manager && fxm_register_builtin(manager,entries,4) && fxm_set_track_count(manager,4));
    assert(fxm_track_add(manager,0,9901) && fxm_track_add(manager,2,9902) && fxm_track_add(manager,3,9903) && fxm_master_add(manager,9904));
    assert(fxm_set_track_return(manager,2,true) && fxm_set_track_return(manager,3,true));
    assert(!fxm_set_track_return(manager,-1,true) && !fxm_set_track_return(manager,4,true));return manager;
}
// Renders complete two-stage parallel routing with fixed independent impulse expectations.
static void block(EffectsManager* manager,int start,int frames,float* output) {
    float source[2][128]={{0}},dry[128]={0},returns[2][128]={{0}};
    for(int i=0;i<frames;++i)if(start+i==0 || start+i==25)for(int t=0;t<2;++t){source[t][i*2]=1;source[t][i*2+1]=.5f;}
    rendering=true;fxm_begin_render_block(manager,frames);
    for(int t=0;t<2;++t){fxm_render_track(manager,t,source[t],frames,2);fxm_align_track(manager,t,source[t],frames,2);
        for(int i=0;i<frames*2;++i){dry[i]+=source[t][i];returns[0][i]+=.5f*source[t][i];returns[1][i]+=.5f*source[t][i];}}
    fxm_align_dry(manager,dry,frames,2);
    for(int t=0;t<2;++t){fxm_render_track(manager,2+t,returns[t],frames,2);fxm_align_track(manager,2+t,returns[t],frames,2);for(int i=0;i<frames*2;++i)dry[i]+=returns[t][i];}
    fxm_render_master(manager,dry,frames,2);rendering=false;
    assert(fxm_processing_latency(manager)==12);memcpy(output+start*2,dry,(size_t)frames*2*sizeof(float));
}
// Checks exact group delay, partition invariance, compatible history transfer and reset behavior.
int main(void) {
    EffectsManager* authored=control();EffectsManager* whole=fxm_clone_for_render(authored);EffectsManager* split=fxm_clone_for_render(authored);
    assert(whole && split && fxm_track_is_return(whole,2) && !fxm_track_is_return(whole,0));
    float a[128]={0},b[128]={0},c[128]={0};block(whole,0,64,a);
    for(int start=0;start<64;++start)block(split,start,1,b);
    assert(!memcmp(a,b,sizeof(a)));
    for(int frame=0;frame<64;++frame){assert(a[frame*2]==((frame==12 || frame==37)?4:0));assert(a[frame*2+1]==((frame==12 || frame==37)?2:0));}
    fxm_reset_render_state(split);block(split,0,7,c);EffectsManager* next=fxm_clone_for_render(authored);int map[]={0,1,2,3};
    assert(next && fxm_transfer_render_state(next,split,map,4));fxm_destroy(split);block(next,7,57,c);assert(!memcmp(a,c,sizeof(a)));
    fxm_reset_render_state(next);memset(c,0,sizeof(c));block(next,0,64,c);assert(!memcmp(a,c,sizeof(a)));
    assert(render_allocations==0);fxm_destroy(next);fxm_destroy(whole);
    assert(fxm_set_track_return(authored,2,false) && fxm_set_track_return(authored,3,false));EffectsManager* old=fxm_clone_for_render(authored);
    fxm_begin_render_block(old,1);assert(fxm_processing_latency(old)==9 && !fxm_track_is_return(old,2));fxm_destroy(old);fxm_destroy(authored);
    puts("fx_return_alignment_test: success (source/return/master impulse alignment, dry/wet phase, stereo partitions, history/reset, zero render allocations and legacy latency)");return 0;
}
