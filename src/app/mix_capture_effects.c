#include "daw/mix_capture.h"
#include <math.h>
// Records serial boundaries and per-sample applied reduction on their own latency-corrected authored clock.
bool daw_mix_observe_effect(void* user,int track,int position,uint64_t id,bool after,uint64_t block,uint64_t latency,const float* audio,const float* reduction,int frames,int channels) {
    DawMixCapture* c=user;int node=track<0 ? c->snapshot->tracks : track;
    if(node<0 || node>c->snapshot->tracks || channels!=c->channels || position<0 || position>=c->snapshot->nodes[node].fx.chain.count || c->snapshot->nodes[node].fx.chain.items[position].id!=id)return false;
    DawMixEffect* effect=&c->effects[node][position];DawMixStage* stage=&effect->boundary[after?1:0];
    if(stage->available && stage->latency!=latency)return false;stage->available=true;stage->latency=latency;
    if(after && reduction)effect->reduction_available=true;
    uint64_t length=c->end-c->start;
    for(int f=0;f<frames;++f) {
        uint64_t rendered=block+(uint64_t)f;if(rendered<latency)continue;uint64_t frame=rendered-latency;
        if(frame<c->start || frame>=c->end)continue;int bin=(int)((frame-c->start)*DAW_MIX_BINS/length);
        for(int ch=0;ch<channels;++ch) {
            double v=audio[f*channels+ch];if(!isfinite(v))return false;double a=fabs(v);DawMixMeter* m=&stage->channel[ch];
            if(a>m->peak)m->peak=a;m->power+=v*v;++m->samples;if(a>=DAW_TAIL_THRESHOLD){if(!m->active_samples)m->first_active_frame=frame;m->last_active_frame=frame;++m->active_samples;}if(a>=1)++m->full_scale;if(a>1)++m->overload;
            if(a>m->bin_peak[bin])m->bin_peak[bin]=a;m->bin_power[bin]+=v*v;++m->bin_samples[bin];
            if(after && reduction) {
                double db=reduction[f*channels+ch];if(!isfinite(db) || db>0)return false;DawMixReduction* r=&effect->reduction[ch];
                if(db<r->minimum_db)r->minimum_db=db;r->sum_db+=db;++r->samples;
                if(db<r->bin_minimum_db[bin])r->bin_minimum_db[bin]=db;r->bin_sum_db[bin]+=db;++r->bin_samples[bin];
            }
        }
    }
    return true;
}
