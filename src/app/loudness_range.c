#include "daw/mix_loudness.h"
#include <math.h>
#include <stdlib.h>
// Orders finite loudness observations after the absolute and relative gates.
static int compare(const void* a,const void* b) {double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
// Interpolates a percentile in a sorted loudness distribution without rounding its range.
static double percentile(const double* values,int count,double fraction) {double index=(count-1)*fraction;int low=(int)index,high=low+1<count?low+1:low;return values[low]+(values[high]-values[low])*(index-low);}
// Implements EBU Tech 3342 2023 with complete 3 s windows at 10 Hz and explicit empty-gate results.
bool daw_loudness_range_finish(DawLoudness* l) {
    l->lra=l->lra_threshold=l->lra_low=l->lra_high=-INFINITY;
    int count=0;double sum=0;
    for(int i=0;i<l->short_term_count;++i){double v=l->short_terms[i];if(isfinite(v) && v>=-70){l->short_terms[count++]=v;sum+=pow(10,(v+.691)/10);}}
    if(!count)return true;l->lra_threshold=-.691+10*log10(sum/count)-20;
    int gated=0;for(int i=0;i<count;++i)if(l->short_terms[i]>=l->lra_threshold)l->short_terms[gated++]=l->short_terms[i];
    l->lra_gated_count=gated;if(!gated)return true;
    qsort(l->short_terms,gated,sizeof(double),compare);l->lra_low=percentile(l->short_terms,gated,.10);l->lra_high=percentile(l->short_terms,gated,.95);l->lra=l->lra_high-l->lra_low;return true;
}
