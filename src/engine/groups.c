#include "engine/groups.h"
// Detects authored buses without imposing a new track cap on legacy flat projects.
bool engine_groups_present(const EngineTrack* tracks, int count) {
    for(int i=0;tracks && i<count;++i) if(tracks[i].is_group)return true;
    return false;
}
// Resolves stable parent/send bindings in the current owned track array.
int engine_group_index(const EngineTrack* tracks,int count,uint64_t id) {
    if(!id)return -1;
    for(int i=0;tracks && i<count;++i)if(tracks[i].runtime_id==id)return i;
    return -1;
}
// Walks a bounded parent chain, refusing cyclic or missing metadata.
bool engine_group_descendant(const EngineTrack* tracks,int count,int child,int parent) {
    for(int depth=0;child>=0 && child<count && depth<count;++depth) {
        child=engine_group_index(tracks,count,tracks[child].parent_id);
        if(child==parent && parent>=0)return true;
    }
    return false;
}
// Counts bounded ancestors for the timeline's indentation.
int engine_group_depth(const EngineTrack* tracks,int count,int track) {
    int depth=0;
    while(track>=0 && track<count && depth<count) {
        track=engine_group_index(tracks,count,tracks[track].parent_id); if(track<0)break; ++depth;
    }
    return depth;
}
// Implements inherited mute, source/group solo closure and independent return solo selection.
bool engine_group_enabled(const EngineTrack* tracks,int count,int track) {
    const EngineTrack* row=&tracks[track];
    if(row->is_return) {
        bool any=false;for(int i=0;i<count;++i)if(tracks[i].is_return && tracks[i].solo && !tracks[i].muted)any=true;
        return !row->muted && (!any || row->solo) ? true : false;
    }
    if(!row->is_group && !row->active)return 0;
    bool selected=row->solo, any=false;
    for(int i=0;i<count;++i)if(!tracks[i].is_return && !tracks[i].muted && tracks[i].solo && (tracks[i].active || tracks[i].is_group)) {
        any=true;if(row->is_group && engine_group_descendant(tracks,count,i,track))selected=true;
    }
    int parent=track;
    for(int depth=0;parent>=0 && depth<count;++depth) {
        if(tracks[parent].muted)return 0;
        if(tracks[parent].solo)selected=true;
        parent=engine_group_index(tracks,count,tracks[parent].parent_id);
    }
    return !any || selected ? true : false;
}
// Applies the static fader only after computing inherited audibility.
float engine_group_gain(const EngineTrack* tracks,int count,int track) {return engine_group_enabled(tracks,count,track)?tracks[track].gain:0;}
// Detects authored ordering separately from whether nested audio processing is necessary.
static bool projected(const EngineTrack* tracks,int count) {
    for(int t=0;t<count;++t)if(tracks[t].is_group || tracks[t].sibling_order)return true;
    return false;
}
// Builds deterministic sibling-ordered preorder rows, pruning folded descendants.
static void project(const EngineTrack* tracks,int count,uint64_t parent,int* rows,int* used,int depth) {
    if(depth>=count)return;
    bool taken[ENGINE_ROUTING_TRACK_CAP]={0};
    for(int round=0;round<count;++round) {
        int best=-1;
        for(int t=0;t<count;++t)if(!taken[t] && tracks[t].parent_id==parent &&
            (best<0 || tracks[t].sibling_order<tracks[best].sibling_order))best=t;
        if(best<0)break;taken[best]=true;rows[(*used)++]=best;
        if(tracks[best].is_group && !tracks[best].folded)project(tracks,count,tracks[best].runtime_id,rows,used,depth+1);
    }
}
// Maps a visible row without changing any engine/FX indices.
int engine_group_visible_track(const EngineTrack* tracks,int count,int row) {
    if(row<0)return -1;
    if(!projected(tracks,count))return row<count ? row : -1;
    if(count>ENGINE_ROUTING_TRACK_CAP)return -1;
    int rows[ENGINE_ROUTING_TRACK_CAP],used=0;project(tracks,count,0,rows,&used,0);return row<used ? rows[row] : -1;
}
// Finds the visual row or reports a hidden descendant.
int engine_group_visible_row(const EngineTrack* tracks,int count,int track) {
    if(track<0 || track>=count)return -1;
    if(!projected(tracks,count))return track;
    if(count>ENGINE_ROUTING_TRACK_CAP)return -1;
    int rows[ENGINE_ROUTING_TRACK_CAP],used=0;project(tracks,count,0,rows,&used,0);
    for(int i=0;i<used;++i)if(rows[i]==track)return i;return -1;
}
// Returns a visible-row count while preserving arbitrary legacy flat counts.
int engine_group_visible_count(const EngineTrack* tracks,int count) {
    if(!projected(tracks,count))return count;
    if(count>ENGINE_ROUTING_TRACK_CAP)return 0;
    int rows[ENGINE_ROUTING_TRACK_CAP],used=0;project(tracks,count,0,rows,&used,0);return used;
}
