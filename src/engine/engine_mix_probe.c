#include "engine/engine_internal.h"
#include "engine/mix_probe.h"
#include "engine/groups.h"
// Derives input arrival time from the graph or legacy source/return compensation stages.
static uint64_t input_latency(const EngineMixState* mix,int track) {
    if(mix->group_mix)return engine_group_mix_input_latency(mix->group_mix,track);
    if(track<0)return fxm_processing_latency(mix->fxm)-fxm_track_processing_latency(mix->fxm,-1,false);
    if(!mix->tracks[track].is_return)return 0;
    uint64_t maximum=0;
    for(int t=0;t<mix->track_count;++t)if(!mix->tracks[t].is_return) {
        uint64_t delay=fxm_track_processing_latency(mix->fxm,t,false);if(delay>maximum)maximum=delay;
    }
    return maximum;
}
// Calls a preallocated private accumulator without allocation or live telemetry.
void engine_mix_probe_emit(EngineMixState* mix,int track,EngineMixProbeStage stage,uint64_t start,const float* samples,int frames,int channels) {
    if(!mix || !mix->probe || mix->probe->failed || !mix->probe->observe)return;
    uint64_t latency=input_latency(mix,track);
    if((track>=0 && stage>=ENGINE_MIX_INSERTS_EQ) || (track<0 && stage>=ENGINE_MIX_OUTPUT))
        latency+=fxm_track_processing_latency(mix->fxm,track,false);
    if(!mix->probe->observe(mix->probe->user,track,stage,start,latency,samples,frames,channels))mix->probe->failed=true;
}

// Maps cumulative serial delay to the same authored clock used by graph taps.
static void effect_observe(void* user,int track,int position,FxInstId id,bool after,uint64_t serial,const float* audio,const float* reduction,int frames,int channels) {
    EngineMixState* mix=user;EngineMixProbe* probe=mix->probe;
    if(!probe || probe->failed || !probe->effect)return;
    if(!probe->effect(probe->user,track,position,id,after,probe->block_start,input_latency(mix,track)+serial,audio,reduction,frames,channels))probe->failed=true;
}
// Attaches preallocated callbacks to an unpublished production export manager.
bool engine_mix_probe_install(EngineMixState* mix,EngineMixProbe* probe) {
    if(!mix || mix->owner)return false;
    if(!fxm_set_diagnostic_callback(mix->fxm,probe && probe->effect ? effect_observe : NULL,mix))return false;
    mix->probe=probe;return true;
}
