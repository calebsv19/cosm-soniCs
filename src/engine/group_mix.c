#include "engine/groups.h"
#include "engine/engine_internal.h"
#include <stdlib.h>
#include <string.h>
// Owns one compensated, stable-bound main-output or auxiliary edge.
typedef struct GroupEdge {
    int from,to,slot; uint64_t source_id,destination_id; uint32_t capacity,write; float* ring;
} GroupEdge;
// Owns a bounded topological graph, node sums and per-edge delays for one render revision.
struct EngineGroupMix {
    int count,edge_count,channels,block,order[ENGINE_ROUTING_TRACK_CAP];
    uint64_t incoming[ENGINE_ROUTING_TRACK_CAP+1],outgoing[ENGINE_ROUTING_TRACK_CAP];
    GroupEdge edges[ENGINE_ROUTING_TRACK_CAP*(ENGINE_SEND_CAP+1)]; float* buffers;
};
// Appends one prepared edge using a master sentinel beyond the final track index.
static bool edge(EngineGroupMix* graph,const EngineMixState* mix,int from,int to,int slot) {
    if(graph->edge_count>=ENGINE_ROUTING_TRACK_CAP*(ENGINE_SEND_CAP+1))return false;
    GroupEdge* e=&graph->edges[graph->edge_count++];*e=(GroupEdge){.from=from,.to=to,.slot=slot,
        .source_id=mix->tracks[from].runtime_id,.destination_id=to<graph->count ? mix->tracks[to].runtime_id : 0};return true;
}
// Computes path arrival times from dependency order using maximum bounds or actual DSP latency.
static bool timing(EngineGroupMix* graph,const EngineMixState* mix,bool maximum) {
    memset(graph->incoming,0,sizeof(graph->incoming));
    for(int i=0;i<graph->count;++i) {
        int t=graph->order[i];uint64_t latency=fxm_track_processing_latency(mix->fxm,t,maximum);
        graph->outgoing[t]=graph->incoming[t]+latency;if(graph->outgoing[t]>=UINT32_MAX)return false;
        for(int j=0;j<graph->edge_count;++j)if(graph->edges[j].from==t) {
            int dest=graph->edges[j].to;if(graph->outgoing[t]>graph->incoming[dest])graph->incoming[dest]=graph->outgoing[t];
        }
    }
    return true;
}
// Prepares all buffers/rings and validates a dependency ordering before a render publication.
EngineGroupMix* engine_group_mix_create(EngineMixState* mix,int block,int channels) {
    if(!mix || mix->track_count<1 || mix->track_count>ENGINE_ROUTING_TRACK_CAP || block<1 || channels<1)return NULL;
    EngineGroupMix* graph=calloc(1,sizeof(*graph));if(!graph)return NULL;
    graph->count=mix->track_count;graph->block=block;graph->channels=channels;
    for(int t=0;t<graph->count;++t) {
        int parent=engine_group_index(mix->tracks,graph->count,mix->tracks[t].parent_id);
        if((mix->tracks[t].parent_id && (parent<0 || !mix->tracks[parent].is_group || parent==t)) ||
            (mix->tracks[t].is_return && parent>=0) || (mix->tracks[t].is_return && mix->tracks[t].is_group))goto fail;
        if(!edge(graph,mix,t,mix->isolated_return==t ? graph->count : (parent>=0 ? parent : graph->count),-1))goto fail;
        for(int slot=0;slot<ENGINE_SEND_CAP;++slot)if(mix->tracks[t].sends[slot].return_id) {
            int dest=engine_group_index(mix->tracks,graph->count,mix->tracks[t].sends[slot].return_id);
            if(dest<0 || !mix->tracks[dest].is_return || mix->tracks[t].is_return || dest==t || !edge(graph,mix,t,dest,slot))goto fail;
        }
    }
    int incoming[ENGINE_ROUTING_TRACK_CAP]={0}; bool used[ENGINE_ROUTING_TRACK_CAP]={0};
    for(int j=0;j<graph->edge_count;++j)if(graph->edges[j].to<graph->count)++incoming[graph->edges[j].to];
    for(int i=0;i<graph->count;++i) {
        int t=0;while(t<graph->count && (used[t] || incoming[t]))++t;if(t==graph->count)goto fail;
        used[t]=true;graph->order[i]=t;
        for(int j=0;j<graph->edge_count;++j)if(graph->edges[j].from==t && graph->edges[j].to<graph->count)--incoming[graph->edges[j].to];
    }
    if(!timing(graph,mix,true))goto fail;
    size_t bytes=(size_t)graph->count*block*channels*sizeof(float);
    for(int j=0;j<graph->edge_count;++j) {
        GroupEdge* e=&graph->edges[j];uint64_t capacity=graph->incoming[e->to] ? graph->incoming[e->to]+1 : 0;
        uint64_t cost=capacity*channels*sizeof(float);if(cost>128u*1024u*1024u || bytes+cost>128u*1024u*1024u)goto fail;
        e->capacity=(uint32_t)capacity;bytes+=(size_t)cost;
        if(capacity && !(e->ring=calloc((size_t)capacity*channels,sizeof(float))))goto fail;
    }
    graph->buffers=calloc((size_t)graph->count*block*channels,sizeof(float));if(!graph->buffers)goto fail;
    if(!timing(graph,mix,false))goto fail;
    return graph;
fail: engine_group_mix_destroy(graph);return NULL;
}
// Frees all privately owned graph storage off the render thread.
void engine_group_mix_destroy(EngineGroupMix* graph) {
    if(!graph)return;for(int i=0;i<graph->edge_count;++i)free(graph->edges[i].ring);free(graph->buffers);free(graph);
}
// Removes stale delayed samples after seek/stop/reset without changing topology.
void engine_group_mix_reset(EngineGroupMix* graph) {
    if(!graph)return;for(int i=0;i<graph->edge_count;++i){GroupEdge* e=&graph->edges[i];e->write=0;
        if(e->ring)memset(e->ring,0,(size_t)e->capacity*graph->channels*sizeof(float));}
}
// Swaps compatible edge histories across surviving stable bindings without allocating on the worker.
void engine_group_mix_transfer(EngineGroupMix* next,EngineGroupMix* old) {
    if(!next || !old || next->channels!=old->channels)return;
    for(int i=0;i<next->edge_count;++i)for(int j=0;j<old->edge_count;++j) {
        GroupEdge* a=&next->edges[i];GroupEdge* b=&old->edges[j];
        if(a->source_id==b->source_id && a->destination_id==b->destination_id && a->slot==b->slot && a->capacity==b->capacity) {
            float* ring=a->ring;a->ring=b->ring;b->ring=ring;uint32_t position=a->write;a->write=b->write;b->write=position;break;
        }
    }
}
// Resets block sums and calculates dynamic actual arrival times inside the prepared bounds.
bool engine_group_mix_begin(EngineGroupMix* graph,EngineMixState* mix,int frames,int channels) {
    if(!graph || frames>graph->block || channels!=graph->channels || !timing(graph,mix,false))return false;
    memset(graph->buffers,0,(size_t)graph->count*graph->block*channels*sizeof(float));
    for(int i=0;i<graph->edge_count;++i) {
        GroupEdge* e=&graph->edges[i];uint64_t delay=graph->incoming[e->to]-graph->outgoing[e->from];
        if(delay && delay>=e->capacity)return false;
    }
    return true;
}
// Returns the dependency-ordered engine index at a bounded node position.
int engine_group_mix_track(const EngineGroupMix* graph,int position) { return graph && position<graph->count ? graph->order[position] : -1; }
// Borrows a node sum for a single render block.
float* engine_group_mix_buffer(EngineGroupMix* graph,int track) { return graph->buffers+(size_t)track*graph->block*graph->channels; }
// Forwards each main output and send through its independent compensation edge.
void engine_group_mix_forward(EngineGroupMix* graph,EngineMixState* mix,int track,float* master,int frames,int channels) {
    bool return_solo=false;for(int t=0;t<graph->count;++t)if(mix->tracks[t].is_return && mix->tracks[t].solo && !mix->tracks[t].muted)return_solo=true;
    for(int j=0;j<graph->edge_count;++j) {
        GroupEdge* e=&graph->edges[j];if(e->from!=track)continue;
        bool enabled=true;
        if(e->slot>=0 && mix->bypass_returns)enabled=false;
        if(e->to==graph->count) {
            if(mix->isolated_return>=0)enabled=track==mix->isolated_return;
            else if(return_solo && !mix->tracks[track].is_return)enabled=false;
        }
        float* source=engine_group_mix_buffer(graph,track);
        float* target=e->to==graph->count ? master : engine_group_mix_buffer(graph,e->to);
        uint32_t delay=(uint32_t)(graph->incoming[e->to]-graph->outgoing[track]);
        for(int frame=0;frame<frames;++frame) {
            float gain=e->slot<0 ? 1 : mix->tracks[track].sends[e->slot].gain;
            if(e->slot>=0){const EngineMixerAutomation* a=&mix->tracks[track].sends[e->slot].automation;uint64_t rendered=mix->automation_frame+(uint64_t)frame;
                if(a->count)gain=rendered>=graph->outgoing[track]?engine_mixer_automation_eval(a,rendered-graph->outgoing[track],gain):0;}
            for(int ch=0;ch<channels;++ch) {
                float value=source[frame*channels+ch]*gain;
                if(e->capacity) {
                    e->ring[(size_t)e->write*channels+ch]=value;
                    uint32_t read=(e->write+e->capacity-delay)%e->capacity;
                    value=e->ring[(size_t)read*channels+ch];
                }
                if(enabled)target[frame*channels+ch]+=value;
            }
            if(e->capacity)e->write=(e->write+1)%e->capacity;
        }
    }
}
// Reports the complete dependency-path latency plus the final master chain.
uint64_t engine_group_mix_latency(const EngineGroupMix* graph,const EngineMixState* mix) {
    return graph->incoming[graph->count]+fxm_track_processing_latency(mix->fxm,-1,false);
}

// Reads prepared path arrival time without consulting live state or allocating.
uint64_t engine_group_mix_input_latency(const EngineGroupMix* graph,int track) {
    return graph ? graph->incoming[track<0 ? graph->count : track] : 0;
}
