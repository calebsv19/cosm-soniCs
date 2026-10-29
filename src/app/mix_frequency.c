#include "daw/mix_capture.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Maps one retained node/channel/time-window to independent spectral storage.
static size_t index_of(const DawMixCapture* c,int node,int channel,int window) {
    return ((size_t)node*c->channels+channel)*DAW_FREQUENCY_WINDOWS+window;
}
// Computes a deterministic double-precision radix-two transform on preallocated stack work arrays.
static void transform(double* re,double* im) {
    const int n=DAW_FREQUENCY_FFT;
    for(int i=1,j=0;i<n;++i){int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){double v=re[i];re[i]=re[j];re[j]=v;v=im[i];im[i]=im[j];im[j]=v;}}
    for(int length=2;length<=n;length<<=1){double angle=-6.2831853071795864769/length,wr=cos(angle),wi=sin(angle);
        for(int base=0;base<n;base+=length){double ar=1,ai=0;for(int j=0;j<length/2;++j){int a=base+j,b=a+length/2;double br=re[b]*ar-im[b]*ai,bi=re[b]*ai+im[b]*ar;re[b]=re[a]-br;im[b]=im[a]-bi;re[a]+=br;im[a]+=bi;double next=ar*wr-ai*wi;ai=ai*wr+ar*wi;ar=next;}}}
}
// Allocates a bounded set of centered windows wholly inside the requested measured range.
bool daw_mix_frequency_prepare(DawMixCapture* c) {
    DawMixFrequency* s=&c->frequency;uint64_t length=c->end-c->start;
    if(length<3)return true;s->frames=length<DAW_FREQUENCY_FFT?(int)length:DAW_FREQUENCY_FFT;
    for(int b=0;b<DAW_FREQUENCY_WINDOWS;++b){uint64_t center=c->start+((uint64_t)(2*b+1)*length)/(2*DAW_FREQUENCY_WINDOWS);uint64_t begin=center-c->start>(uint64_t)s->frames/2?center-s->frames/2:c->start;if(begin>c->end-s->frames)begin=c->end-s->frames;s->start[b]=begin;}
    size_t windows=(size_t)(c->snapshot->tracks+1)*c->channels*DAW_FREQUENCY_WINDOWS;
    s->samples=calloc(windows*(size_t)s->frames,sizeof(float));s->powers=calloc(windows*DAW_FREQUENCY_BINS,sizeof(float));s->counts=calloc((size_t)(c->snapshot->tracks+1)*DAW_FREQUENCY_WINDOWS,sizeof(uint64_t));
    return s->samples && s->powers && s->counts;
}
// Copies only intersecting windows, independently of production block partitions or graph latency.
bool daw_mix_frequency_observe(DawMixCapture* c,int node,uint64_t block,uint64_t latency,const float* audio,int frames,int channels) {
    DawMixFrequency* s=&c->frequency;if(!s->frames)return true;if(channels!=c->channels)return false;
    if(block+(uint64_t)frames<=latency || block+(uint64_t)frames-latency<=c->start || (block>=latency && block-latency>=c->end))return true;
    for(int b=0;b<DAW_FREQUENCY_WINDOWS;++b){uint64_t begin=s->start[b]+latency,end=begin+s->frames;
        uint64_t lo=block>begin?block:begin,hi=block+(uint64_t)frames<end?block+(uint64_t)frames:end;if(lo>=hi)continue;
        for(uint64_t f=lo;f<hi;++f)for(int ch=0;ch<channels;++ch){float v=audio[(size_t)(f-block)*channels+ch];if(!isfinite(v))return false;s->samples[index_of(c,node,ch,b)*s->frames+f-begin]=v;}
        s->counts[node*DAW_FREQUENCY_WINDOWS+b]+=hi-lo;
    }
    return true;
}
// Applies a periodic Hann and Parseval normalization for one-sided integrated bin mean-square power.
bool daw_mix_frequency_finish(DawMixCapture* c) {
    DawMixFrequency* s=&c->frequency;if(!s->frames)return true;
    double weights[DAW_FREQUENCY_FFT],sum_square=0;
    for(int f=0;f<s->frames;++f){weights[f]=.5-.5*cos(6.2831853071795864769*f/s->frames);sum_square+=weights[f]*weights[f];}
    for(int node=0;node<=c->snapshot->tracks;++node)for(int b=0;b<DAW_FREQUENCY_WINDOWS;++b){if(s->counts[node*DAW_FREQUENCY_WINDOWS+b]!=(uint64_t)s->frames)return false;
        for(int ch=0;ch<c->channels;++ch){size_t index=index_of(c,node,ch,b);double re[DAW_FREQUENCY_FFT]={0},im[DAW_FREQUENCY_FFT]={0};
            for(int f=0;f<s->frames;++f)re[f]=s->samples[index*s->frames+f]*weights[f];transform(re,im);
            for(int k=0;k<DAW_FREQUENCY_BINS;++k){double scale=(k==0 || k==DAW_FREQUENCY_FFT/2)?1:2;float power=(float)(scale*(re[k]*re[k]+im[k]*im[k])/(DAW_FREQUENCY_FFT*sum_square));if(!isfinite(power))return false;s->powers[index*DAW_FREQUENCY_BINS+k]=power;}
        }
    }
    free(s->samples);s->samples=NULL;free(s->counts);s->counts=NULL;return true;
}
// Frees temporary and retained buffers safely after any preparation or render failure.
void daw_mix_frequency_free(DawMixFrequency* s) {if(!s)return;free(s->samples);free(s->powers);free(s->counts);memset(s,0,sizeof(*s));}
