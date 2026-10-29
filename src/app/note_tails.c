#include "app_state.h"
#include "daw/mix_capture.h"
#include <stdlib.h>
#include <math.h>
// Orders viable authored onsets by track, time and stable row identity.
typedef struct Onset {uint64_t track,frame;int index;} Onset;
// Establishes deterministic same-track ordering without changing captured note order.
static int compare(const void* a,const void* b){const Onset* x=a,*y=b;if(x->track!=y->track)return x->track<y->track?-1:1;if(x->frame!=y->frame)return x->frame<y->frame?-1:1;return (x->index>y->index)-(x->index<y->index);}
// Captures original indices and next distinct viable authored onset on each track.
bool daw_note_tails_prepare(AppState* state,DawMixCapture* c) {
    c->tails=calloc(1,sizeof(*c->tails));if(!c->tails)return false;DawNoteTails* out=c->tails;
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    for(int s=0;s<c->snapshot->sources;++s)out->count+=c->snapshot->regions[s].notes;
    if(out->count>DAW_ARRANGEMENT_NOTE_CAP)return false;out->notes=calloc((size_t)out->count+1,sizeof(*out->notes));Onset* sorted=calloc((size_t)out->count+1,sizeof(*sorted));if(!out->notes || !sorted){free(sorted);return false;}
    int row=0,valid=0;
    for(int s=0;s<c->snapshot->sources;++s){const DawMixSource* region=&c->snapshot->regions[s];out->base[s]=row;if(!region->midi)continue;const EngineClip* clip=NULL;
        for(int t=0;t<c->snapshot->tracks;++t)if(tracks[t].runtime_id==region->track_id)for(int k=0;k<tracks[t].clip_count;++k)if(tracks[t].clips[k].creation_index==region->clip_id)clip=&tracks[t].clips[k];if(!clip){free(sorted);return false;}
        for(int n=0;n<clip->midi_notes.note_count;++n){const EngineMidiNote* note=&clip->midi_notes.notes[n];DawNoteEnding* dest=&out->notes[row];*dest=(DawNoteEnding){.source=s,.note_index=n,.authored_start=note->start_frame,.authored_gate=note->duration_frames,.pitch=note->note,.velocity=note->velocity,.next_onset=UINT64_MAX,.eligible=note->duration_frames && note->velocity>0 && note->start_frame<region->duration};if(dest->eligible)sorted[valid++]=(Onset){region->track_id,region->start+note->start_frame,row};++row;}
    }
    qsort(sorted,valid,sizeof(*sorted),compare);for(int i=valid-1;i>=0;){int first=i;while(first>0 && sorted[first-1].track==sorted[i].track && sorted[first-1].frame==sorted[i].frame)--first;uint64_t next=i+1<valid && sorted[i+1].track==sorted[i].track?sorted[i+1].frame:UINT64_MAX;for(int j=first;j<=i;++j)out->notes[sorted[j].index].next_onset=next;i=first-1;}free(sorted);return row==out->count;
}
// Copies actual private prepared gates, choke fences and mono articulation through the source owner.
int daw_note_tails_source(void* user,uint64_t track,uint64_t clip,EngineInstrumentSource* source) {
    DawMixCapture* c=user;for(int s=0;s<c->snapshot->sources;++s)if(c->snapshot->regions[s].track_id==track && c->snapshot->regions[s].clip_id==clip){c->tails->prepared[s]=true;EngineNoteEnding ending;
        for(int n=0;engine_instrument_source_note_ending(source,n,&ending);++n){if(ending.original_index<0 || ending.original_index>=c->snapshot->regions[s].notes)return -1;DawNoteEnding* dest=&c->tails->notes[c->tails->base[s]+ending.original_index];dest->prepared=true;dest->production=ending;uint64_t off=c->snapshot->regions[s].start+ending.damper_off;if(dest->eligible && off>c->tails->last_gate[s])c->tails->last_gate[s]=off;}return s;}return -1;
}
// Accumulates original source activity only within the measured range, without PCM retention.
bool daw_note_tails_observe(void* user,int source,uint64_t start,const float* samples,int frames,int channels) {
    DawMixCapture* c=user;if(source<0 || source>=c->snapshot->sources || channels!=c->channels)return false;uint64_t gate=c->tails->last_gate[source];
    for(int f=0;f<frames;++f){uint64_t frame=start+(uint64_t)f;if(frame<c->start || frame>=c->end)continue;for(int ch=0;ch<channels;++ch){double value=samples[f*channels+ch];if(!isfinite(value))return false;double a=fabs(value);DawSourceTailChannel* m=&c->tails->source[source][ch];++m->samples;m->power+=value*value;if(a>m->peak)m->peak=a;if(a>=DAW_TAIL_THRESHOLD){if(!m->active)m->first=frame;++m->active;m->last=frame;}if(gate && frame>=gate){++m->post_samples;m->post_power+=value*value;if(a>m->post_peak)m->post_peak=a;if(a>=DAW_TAIL_THRESHOLD){++m->post_active;m->post_last=frame;}}}}return true;
}
