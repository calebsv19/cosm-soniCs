#include "engine/routing.h"
#include "engine/engine_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Captures exact ordered identities and authored send fields without mutable engine references.
bool engine_routing_capture(const Engine* engine,EngineRoutingSnapshot* out) {
    if(!engine || !out || !engine_is_control_thread(engine) || engine->track_count>ENGINE_ROUTING_TRACK_CAP)return false;
    memset(out,0,sizeof(*out));out->generation=engine_instance_id(engine);out->track_count=engine->track_count;
    for(int t=0;t<engine->track_count;++t){out->tracks[t].track_id=engine->tracks[t].runtime_id;out->tracks[t].is_return=engine->tracks[t].is_return;
        memcpy(out->tracks[t].sends,engine->tracks[t].sends,sizeof(out->tracks[t].sends));}
    return true;
}
// Compares authored routing without relying on caller struct padding.
bool engine_routing_snapshot_equal(const EngineRoutingSnapshot* a,const EngineRoutingSnapshot* b) {
    if(!a || !b || a->track_count < 0 || a->track_count > ENGINE_ROUTING_TRACK_CAP || a->generation!=b->generation || a->track_count!=b->track_count)return false;
    for(int t=0;t<a->track_count;++t){if(a->tracks[t].track_id!=b->tracks[t].track_id || a->tracks[t].is_return!=b->tracks[t].is_return)return false;
        for(int s=0;s<ENGINE_SEND_CAP;++s)if(a->tracks[t].sends[s].return_id!=b->tracks[t].sends[s].return_id || a->tracks[t].sends[s].gain!=b->tracks[t].sends[s].gain)return false;}
    return true;
}
// Refuses feedback, missing/duplicate destinations, occupied return lanes and invalid levels.
bool engine_routing_validate(const Engine* engine,const EngineRoutingSnapshot* next) {
    if(!engine || !next || !engine_is_control_thread(engine) || next->generation!=engine_instance_id(engine) || next->track_count!=engine->track_count || next->track_count<0 || next->track_count>ENGINE_ROUTING_TRACK_CAP)return false;
    int returns=0;
    for(int t=0;t<next->track_count;++t){const EngineRoutingTrack* row=&next->tracks[t];
        if(row->track_id!=engine->tracks[t].runtime_id)return false;
        if(row->is_return && (++returns>ENGINE_RETURN_CAP || engine->tracks[t].clip_count || engine->tracks[t].midi_instrument_enabled))return false;
        for(int s=0;s<ENGINE_SEND_CAP;++s){EngineSend send=row->sends[s];if(!isfinite(send.gain) || send.gain<0 || send.gain>1 || (!send.return_id && send.gain) || (row->is_return && (send.return_id || send.gain)))return false;
            if(!send.return_id)continue;bool found=false;
            for(int d=0;d<next->track_count;++d)if(next->tracks[d].track_id==send.return_id && next->tracks[d].is_return && d!=t)found=true;
            if(!found)return false;for(int earlier=0;earlier<s;++earlier)if(row->sends[earlier].return_id==send.return_id)return false;}
    }
    return true;
}
// Changes only authored routing metadata while candidate rendering keeps its independent ownership.
static void assign(Engine* engine,const EngineRoutingSnapshot* snapshot) {
    for(int t=0;t<snapshot->track_count;++t){engine->tracks[t].is_return=snapshot->tracks[t].is_return;
        memcpy(engine->tracks[t].sends,snapshot->tracks[t].sends,sizeof(engine->tracks[t].sends));}
}
// Publishes one complete routing revision and restores prior metadata on preparation failure.
bool engine_routing_set(Engine* engine,const EngineRoutingSnapshot* expected,const EngineRoutingSnapshot* next) {
    if(!engine || !expected || !next || !engine_is_control_thread(engine))return false;
    EngineRoutingSnapshot* current=malloc(sizeof(*current));if(!current)return false;
    bool accepted=engine_routing_capture(engine,current) && engine_routing_snapshot_equal(current,expected) && engine_routing_validate(engine,next);
    if(accepted && !engine_routing_snapshot_equal(current,next)){assign(engine,next);accepted=engine_request_rebuild_sources(engine);if(!accepted)assign(engine,current);}
    free(current);return accepted;
}
// Protects explicit destination bindings, including temporarily zero-level sends.
bool engine_routing_track_referenced(const Engine* engine,uint64_t id) {
    if(!engine || !id)return false;
    for(int t=0;t<engine->track_count;++t)for(int s=0;s<ENGINE_SEND_CAP;++s)if(engine->tracks[t].sends[s].return_id==id)return true;
    return false;
}
