#include "daw/mix_loudness.h"
#include "../extern/ebur128/ebur128.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Prints unavailable numeric values as valid JSON null.
static void number(double v) {if(isfinite(v))printf("%.12g",v);else printf("null");}
// Measures a private raw float fixture; long library-only qualification does not expand the capture limit.
int main(int argc,char** argv) {
    assert(argc==5 || argc==6);int rate=atoi(argv[1]),channels=atoi(argv[2]),block=atoi(argv[3]);assert(block>0 && channels>0 && channels<=8);
    FILE* file=fopen(argv[4],"rb");assert(file);float* samples=calloc((size_t)block*channels,sizeof(float));assert(samples);
    DawLoudness l={0};assert(daw_loudness_init(&l,rate,channels));size_t n;if(argc==6){l.programme=true;l.short_terms=calloc(DAW_LOUDNESS_SHORT_TERMS,sizeof(double));assert(l.short_terms);}
    while((n=fread(samples,sizeof(float)*channels,block,file))){
        if(!l.programme && l.frames+(uint64_t)n>(uint64_t)rate*30 && l.available){assert(ebur128_add_frames_float(l.state,samples,n)==EBUR128_SUCCESS);l.frames+=n;}
        else assert(daw_loudness_add(&l,samples,(int)n));
    }assert(!ferror(file));fclose(file);free(samples);double before[2]={0};if(l.available)for(int ch=0;ch<channels;++ch)assert(ebur128_true_peak(l.state,ch,&before[ch])==EBUR128_SUCCESS);assert(daw_loudness_finish(&l));assert(!daw_loudness_finish(&l) && !daw_loudness_add(&l,NULL,0));
    printf("{\"available\":%s,\"frames\":%llu,\"I\":",l.available?"true":"false",(unsigned long long)l.frames);number(l.integrated);
    printf(",\"M\":");number(l.momentary);printf(",\"S\":");number(l.short_term);printf(",\"gate\":");number(l.relative_gate);
    printf(",\"LRA\":");number(l.lra);printf(",\"lra_windows\":%d,\"lra_gated\":%d",l.short_term_count,l.lra_gated_count);
    printf(",\"tp\":[");for(int ch=0;ch<(channels>2?0:channels);++ch){if(ch)printf(",");number(l.true_peak[ch]>0?20*log10(l.true_peak[ch]):-INFINITY);}
    printf("],\"unflushed_tp\":[");for(int ch=0;ch<(channels>2?0:channels);++ch){if(ch)printf(",");number(before[ch]>0?20*log10(before[ch]):-INFINITY);}
    printf("],\"history\":[");for(int i=0;i<l.count;++i){if(i)printf(",");printf("[%llu,",(unsigned long long)l.at[i]);number(l.m[i]);printf(",");number(l.s[i]);printf("]");}printf("]}\n");return 0;
}
