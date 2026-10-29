#include "daw/mix_capture.h"
#include "../../extern/ebur128/ebur128.h"
#include <math.h>
#include <stdlib.h>
// Initializes explicit mono/stereo mapping and keeps unsupported configurations observable.
bool daw_loudness_init(DawLoudness* l,int rate,int channels) {
    if(!l || l->state)return false;
    l->rate=rate;l->channels=channels;l->integrated=l->momentary=l->short_term=l->relative_gate=-INFINITY;
    l->available=(channels==1 || channels==2) && (rate==44100 || rate==48000 || rate==96000);
    if(!l->available)return true;
    l->state=ebur128_init(channels,rate,EBUR128_MODE_I|EBUR128_MODE_S|EBUR128_MODE_TRUE_PEAK);
    if(!l->state)return false;
    for(int ch=0;ch<channels;++ch)if(ebur128_set_channel(l->state,ch,channels==1?EBUR128_CENTER:ch?EBUR128_RIGHT:EBUR128_LEFT)!=EBUR128_SUCCESS)return false;
    return true;
}
// Reads only complete momentary and short-term windows at the actual sample endpoint.
static bool point(DawLoudness* l,bool regular) {
    double m=-INFINITY,s=-INFINITY;
    if(l->frames>=(uint64_t)l->rate*4/10 && ebur128_loudness_momentary(l->state,&m)!=EBUR128_SUCCESS)return false;
    if(l->frames>=(uint64_t)l->rate*3 && ebur128_loudness_shortterm(l->state,&s)!=EBUR128_SUCCESS)return false;
    l->momentary=m;l->short_term=s;
    if(l->programme){if(regular && l->frames>=(uint64_t)l->rate*3){if(l->short_term_count>=DAW_LOUDNESS_SHORT_TERMS)return false;l->short_terms[l->short_term_count++]=s;}return true;}
    if(l->count>=DAW_LOUDNESS_POINTS)return false;
    int i=l->count++;l->at[i]=l->frames;l->m[i]=m;l->s[i]=s;return true;
}
// Splits input at fixed sample endpoints so history does not depend on render-block partitioning.
bool daw_loudness_add(DawLoudness* l,const float* samples,int frames) {
    if(!l || l->finished || frames<0 || (!samples && frames))return false;
    for(int f=0;f<frames*l->channels;++f)if(!isfinite(samples[f]))return false;
    if(!l->available){l->frames+=(uint64_t)frames;return true;}
    uint64_t step=(uint64_t)l->rate/10;
    while(frames){int n=(int)(step-l->frames%step);if(n>frames)n=frames;
        if(ebur128_add_frames_float(l->state,samples,n)!=EBUR128_SUCCESS)return false;
        samples+=(size_t)n*l->channels;frames-=n;l->frames+=(uint64_t)n;
        if(l->frames%step==0 && !point(l,true))return false;
    }return true;
}
// Snapshots selected-range loudness before flushing the 49-tap interpolator with 32 zero frames.
bool daw_loudness_finish(DawLoudness* l) {
    if(!l || l->finished)return false;l->finished=true;if(!l->available)return true;
    if(l->programme || !l->count || l->at[l->count-1]!=l->frames)if(!point(l,false))return false;
    if(ebur128_loudness_global(l->state,&l->integrated)!=EBUR128_SUCCESS || ebur128_relative_threshold(l->state,&l->relative_gate)!=EBUR128_SUCCESS)return false;
    if(l->programme && !daw_loudness_range_finish(l))return false;
    float zeros[64]={0};if(ebur128_add_frames_float(l->state,zeros,32)!=EBUR128_SUCCESS)return false;
    for(int ch=0;ch<l->channels;++ch)if(ebur128_true_peak(l->state,ch,&l->true_peak[ch])!=EBUR128_SUCCESS)return false;
    daw_loudness_free(l);return true;
}
// Destroys only independently owned meter state.
void daw_loudness_free(DawLoudness* l) {if(!l)return;if(l->state)ebur128_destroy(&l->state);free(l->short_terms);l->short_terms=NULL;}
// Allocates bounded private per-node meters before rendering.
bool daw_mix_loudness_prepare(DawMixCapture* c) {
    c->loudness=calloc((size_t)c->snapshot->tracks+1,sizeof(DawLoudness));if(!c->loudness)return false;
    for(int n=0;n<=c->snapshot->tracks;++n){DawLoudness* l=&c->loudness[n];if(!daw_loudness_init(l,c->sample_rate,c->channels))return false;l->programme=c->whole_song;if(l->programme && l->available){l->short_terms=calloc(DAW_LOUDNESS_SHORT_TERMS,sizeof(double));if(!l->short_terms)return false;}}return true;
}
// Clips one processed tap block to the requested authored interval without zero-filling missing input.
bool daw_mix_loudness_observe(DawMixCapture* c,int node,uint64_t block,uint64_t latency,const float* samples,int frames,int channels) {
    int first=0,last=frames;while(first<last && (block+(uint64_t)first<latency || block+(uint64_t)first-latency<c->start))++first;
    while(last>first && block+(uint64_t)last-1-latency>=c->end)--last;
    return daw_loudness_add(&c->loudness[node],samples+(size_t)first*channels,last-first);
}
// Rejects incomplete output streams and freezes every node's detached result.
bool daw_mix_loudness_finish(DawMixCapture* c) {
    for(int n=0;n<=c->snapshot->tracks;++n)if(c->loudness[n].frames!=c->end-c->start || !daw_loudness_finish(&c->loudness[n]))return false;return true;
}
