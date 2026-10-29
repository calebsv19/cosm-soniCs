#include "daw/mix_capture.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
// Preserves silence and insufficient windows as null instead of a fabricated loudness floor.
static void value(cJSON* j,const char* key,double v) {if(isfinite(v))cJSON_AddNumberToObject(j,key,v);else cJSON_AddNullToObject(j,key);}
// Preserves exact sample endpoints independently of JSON number precision.
static void frame(cJSON* j,const char* key,uint64_t v) {char s[32];snprintf(s,sizeof(s),"%" PRIu64,v);cJSON_AddStringToObject(j,key,s);}
// Publishes passage-only integrated loudness, ungated windows and reconstructed channel peaks.
void daw_control_mix_loudness_json(cJSON* j,const DawMixCapture* c,int node) {
    const DawLoudness* l=&c->loudness[node];cJSON_AddBoolToObject(j,"available",l->available);
    cJSON_AddStringToObject(j,"implementation","libebur128 1.2.6; K-weighting and BS.1770 absolute/relative gating; 4x true-peak interpolation at 44.1/48 kHz, 2x at 96 kHz");
    cJSON_AddStringToObject(j,"tap",node==c->snapshot->tracks?"master sanitized output":"processed node output");
    cJSON_AddStringToObject(j,"scope",c->whole_song?"whole authored programme from zero plus explicit tail; independent nodes overlap and cannot be summed":"selected passage only; loudness filter reset at start_frame; independent node measurements overlap and cannot be summed; not whole-programme LUFS unless the entire programme is selected");
    if(!l->available){cJSON_AddStringToObject(j,"reason","qualified only for explicitly mapped mono/stereo at 44100, 48000 or 96000 Hz; surround/LFE mapping unavailable");return;}
    cJSON_AddStringToObject(j,"validation","reconstructed EBU Tech 3341 definitions 1-5,9,12,15-19, independent FFmpeg comparisons and analytic edge cases; official ZIP inaccessible; not full EBU Mode certification");
    cJSON_AddStringToObject(j,"channel_layout",l->channels==1?"mono, weight 1 (not dual mono)":"left/right stereo, weights 1/1");
    cJSON_AddNumberToObject(j,"absolute_gate_lufs",-70);cJSON_AddNumberToObject(j,"relative_gate_lu",-10);frame(j,"measured_frames",l->frames);
    value(j,"integrated_lufs",l->integrated);value(j,"momentary_lufs",l->momentary);value(j,"short_term_lufs",l->short_term);value(j,"relative_threshold_lufs",l->relative_gate);
    cJSON_AddStringToObject(j,"windows","ungated M=400 ms, S=3 s; null until complete or for silence; 100 ms endpoints plus exact final endpoint; true-peak boundary zero extension 32 frames excluded from all loudness windows/gates");
    cJSON* peaks=cJSON_AddArrayToObject(j,"channels");for(int ch=0;ch<l->channels;++ch){cJSON* p=cJSON_CreateObject();cJSON_AddItemToArray(peaks,p);cJSON_AddNumberToObject(p,"index",ch);cJSON_AddNumberToObject(p,"true_peak_linear",l->true_peak[ch]);value(p,"true_peak_dbtp",l->true_peak[ch]>0?20*log10(l->true_peak[ch]):-INFINITY);}
    if(c->whole_song){value(j,"loudness_range_lu",l->lra);value(j,"lra_relative_threshold_lufs",l->lra_threshold);value(j,"lra_percentile_10_lufs",l->lra_low);value(j,"lra_percentile_95_lufs",l->lra_high);cJSON_AddNumberToObject(j,"lra_complete_windows",l->short_term_count);cJSON_AddNumberToObject(j,"lra_gated_windows",l->lra_gated_count);cJSON_AddBoolToObject(j,"lra_provisional",l->frames<(uint64_t)l->rate*60);cJSON_AddStringToObject(j,"lra_method","EBU Tech 3342 2023: complete 3 s windows at 100 ms cadence; -70 LUFS absolute, -20 LU relative gate; interpolated 10th/95th percentiles; short programmes may be misleading; no full EBU certification");cJSON_AddStringToObject(j,"history_scope","programme summary; detailed M/S history remains in selected-passage mode");return;}
    cJSON* history=cJSON_AddArrayToObject(j,"history");for(int i=0;i<l->count;++i){cJSON* p=cJSON_CreateObject();cJSON_AddItemToArray(history,p);frame(p,"end_frame",c->start+l->at[i]);value(p,"momentary_lufs",l->m[i]);value(p,"short_term_lufs",l->s[i]);}
}
