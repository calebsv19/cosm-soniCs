#include "engine/piano_expression.h"
#include "audio/media_clip.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pins one prepared mechanical recording for the process lifetime.
typedef struct PianoMechanicalSample { const char* file; AudioMediaClip media; } PianoMechanicalSample;
static PianoMechanicalSample mechanical[] = {
#include "../../assets/instruments/salamander_piano_v1/expression/sample_map.inc"
};
_Static_assert(sizeof(mechanical)/sizeof(mechanical[0]) == 92, "88 releases and four pedal variations");
static pthread_once_t mechanical_once = PTHREAD_ONCE_INIT;
static bool mechanical_ready;
// Loads finite centered recordings once on source creation and rejects incomplete packages.
static void load_mechanical(void) {
    const char* root=getenv("DAW_INSTRUMENT_BANKS_ROOT"); if (!root || !*root) root="assets/instruments";
    char path[4096];
    for (int i=0;i<92;++i) {
        int n=snprintf(path,sizeof(path),"%s/salamander_piano_v1/%s",root,mechanical[i].file);
        AudioMediaClip* m=&mechanical[i].media;
        if(n<0 || n>=(int)sizeof(path) || !audio_media_clip_load_wav(path,48000,m) ||
            m->channels!=1 || m->sample_rate!=48000 || m->frame_count>48000 || m->frame_count<2) goto fail;
        for(uint64_t f=0;f<m->frame_count;++f) if(!isfinite(m->samples[f])) goto fail;
    }
    mechanical_ready=true; return;
fail:
    for(int i=0;i<92;++i) audio_media_clip_free(&mechanical[i].media);
    fprintf(stderr,"Piano release/pedal bank unavailable\n");
}
// Publishes immutable preload readiness to source preparation.
bool engine_piano_expression_init(void) { pthread_once(&mechanical_once,load_mechanical); return mechanical_ready; }
// Interpolates one unpitched, naturally ending mechanical recording.
static float mechanical_at(int index, uint64_t age, int rate) {
    if(!mechanical_ready || index<0 || index>=92 || rate<=0) return 0;
    const AudioMediaClip* m=&mechanical[index].media;
    double position=(double)age*48000/rate;
    if(position >= m->frame_count-1) return 0;
    uint64_t a=(uint64_t)position; float t=(float)(position-a);
    return m->samples[a]*(1-t)+m->samples[a+1]*t;
}
// Uses the original chromatic key identity and reduces mechanics on slowly released phrases.
float engine_piano_key_release(uint8_t note,float velocity,float amount,float level,int rate,uint64_t age,uint64_t gate) {
    if(note<21 || note>108 || age<gate || amount<=0 || rate<=0) return 0;
    double held=(double)gate/rate;
    return mechanical_at(note-21,age-gate,rate)*.014125375f*amount*level*
        powf(fmaxf(0,velocity),.82f)*(float)exp(-held*.23);
}
// Initializes stable unit-circle modes; damping is applied separately on every sample.
void engine_piano_expression_reset(EnginePianoExpression* s,int rate) {
    memset(s,0,sizeof(*s)); s->rate=rate>0?rate:48000;
    for(int k=0;k<88;++k) {
        double angle=6.283185307179586*440*pow(2.,(k+21-69)/12.)/s->rate;
        s->cosine[k]=cos(angle); s->sine[k]=sin(angle);
    }
}
// Excites only undamped strings, damps pedal-up energy, and avoids state surviving a transport jump.
float engine_piano_expression_process(EnginePianoExpression* s,EngineInstrumentParams p,float dry,const bool held[88],uint64_t frame) {
    if(s->started && frame!=s->next_frame) engine_piano_expression_reset(s,s->rate);
    bool down=p.sustain_pedal>=.5f;
    if(down!=s->pedal_down) { s->pedal_down=down;s->pedal_frame=frame;s->pedal_variant=s->pedal_counts[down?0:1]++ & 1u;s->pedal_active=true; }
    s->started=true;s->next_frame=frame+1;
    double open_decay=exp(-1./(s->rate*.75)),closed_decay=exp(-1./(s->rate*.025));
    double sum=0;
    for(int k=0;k<88;++k) {
        bool open=down || held[k]; double decay=open?open_decay:closed_decay;
        double a=s->real[k],b=s->imaginary[k];
        s->real[k]=decay*(a*s->cosine[k]-b*s->sine[k])+(open?dry*.00018:0);
        s->imaginary[k]=decay*(a*s->sine[k]+b*s->cosine[k]);
        if(fabs(s->real[k])+fabs(s->imaginary[k])<1e-12) s->real[k]=s->imaginary[k]=0;
        sum+=s->real[k];
    }
    // A zero amount bypasses both layers exactly; recorded pedal noise starts only on transitions.
    float pedal=frame>=s->pedal_frame && s->pedal_active ? mechanical_at(88+(down?0:2)+s->pedal_variant,frame-s->pedal_frame,s->rate):0;
    return (float)(sum*.12*p.string_resonance)+pedal*.1f*p.release_noise*p.level;
}
// Finds a lane's next point by binary search rather than scanning notes or allocating in rendering.
static uint64_t next_point(const EngineAutomationLane* lane,uint64_t frame,uint64_t offset,uint64_t end) {
    if(lane->target!=ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN_PEDAL) return end;
    int lo=0,hi=lane->point_count;
    while(lo<hi){int mid=lo+(hi-lo)/2;uint64_t at=lane->points[mid].frame;
        if(at<=frame-offset)lo=mid+1;else hi=mid;}
    if(lo==lane->point_count || lane->points[lo].frame>UINT64_MAX-offset)return end;
    uint64_t at=lane->points[lo].frame+offset;return at<end?at:end;
}
// Captures sustain at note-off and resolves sample-exact hold/linear/smooth pedal-up thresholds.
uint64_t engine_piano_damper_frame(uint64_t off,uint64_t end,uint64_t start,
    const EngineAutomationLane* track,int tc,const EngineAutomationLane* clip,int cc,
    EngineInstrumentParams (*params_at)(void*,uint64_t),void* context) {
    if(off>=end || params_at(context,off).sustain_pedal<.5f) return off;
    uint64_t left=off;
    while(left<end) {
        uint64_t right=end;
        for(int i=0;i<tc;++i){uint64_t q=next_point(&track[i],left+start,0,end+start);if(q-start<right)right=q-start;}
        for(int i=0;i<cc;++i){uint64_t q=next_point(&clip[i],left,0,end);if(q<right)right=q;}
        if(right<=left)break;
        if(params_at(context,right).sustain_pedal<.5f) {
            while(right-left>1){uint64_t mid=left+(right-left)/2;if(params_at(context,mid).sustain_pedal<.5f)right=mid;else left=mid;}
            return right;
        }
        left=right;
    }
    return end;
}
