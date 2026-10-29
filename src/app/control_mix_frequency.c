#include "daw/mix_capture.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
// Preserves exact authored frame identities on the JSON wire.
static void id(cJSON* j,const char* key,uint64_t v){char text[32];snprintf(text,sizeof(text),"%" PRIu64,v);cJSON_AddStringToObject(j,key,text);}
// Represents calibrated spectral silence without an arbitrary finite dB floor.
static cJSON* db(double p){return p>0?cJSON_CreateNumber(10*log10(p)):cJSON_CreateNull();}
// Reads one immutable channel/window spectrum and disjoint octave-like integrated bands.
void daw_control_mix_frequency_json(cJSON* j,const DawMixCapture* c,int node,int window,int channel) {
    const DawMixFrequency* s=&c->frequency;cJSON_AddStringToObject(j,"schema","sonics_mix_frequency_v1");cJSON_AddBoolToObject(j,"available",s->frames>0);
    cJSON_AddNumberToObject(j,"window_offset",window);cJSON_AddNumberToObject(j,"next_window_offset",window<DAW_FREQUENCY_WINDOWS-1?window+1:-1);cJSON_AddNumberToObject(j,"channel_offset",channel);cJSON_AddNumberToObject(j,"next_channel_offset",channel<c->channels-1?channel+1:-1);
    cJSON_AddStringToObject(j,"stage",node==c->snapshot->tracks?"master_output_post_sanitize":"output_post_bus_gain_pan_pre_edge_compensation");
    if(!s->frames){cJSON_AddStringToObject(j,"reason","requested range has fewer than three samples");return;}
    id(j,"start_frame",s->start[window]);id(j,"end_frame",s->start[window]+s->frames);id(j,"latency_frames",c->stages[node][node==c->snapshot->tracks?ENGINE_MIX_SANITIZED:ENGINE_MIX_OUTPUT].latency);
    cJSON_AddNumberToObject(j,"sample_count",s->frames);cJSON_AddNumberToObject(j,"fft_size",DAW_FREQUENCY_FFT);cJSON_AddNumberToObject(j,"bin_count",DAW_FREQUENCY_BINS);cJSON_AddNumberToObject(j,"bin_spacing_hz",(double)c->sample_rate/DAW_FREQUENCY_FFT);cJSON_AddNumberToObject(j,"window_resolution_hz",(double)c->sample_rate/s->frames);cJSON_AddNumberToObject(j,"nyquist_hz",c->sample_rate*.5);
    cJSON_AddStringToObject(j,"calibration","periodic Hann; one-sided bin mean-square power=endpoint_factor*abs(DFT)^2/(fft_size*sum(window^2)); dBFS=10log10(power), silence null; bin k at k*sample_rate/fft_size; no DC removal, normalization, mono downmix, A/K weighting or perceptual masking verdict");
    cJSON_AddStringToObject(j,"coverage","32 short windows centered near level-bin midpoints and clamped inside measured range; overlap/repeated windows and uncovered intervals are possible; short windows are zero padded to FFT size; not a continuous spectrogram or an energy integral across the entire requested range");
    size_t index=((size_t)node*c->channels+channel)*DAW_FREQUENCY_WINDOWS+window;const float* power=s->powers+index*DAW_FREQUENCY_BINS;double total=0;int peak=0;
    cJSON* bins=cJSON_AddArrayToObject(j,"bin_mean_square_power");for(int k=0;k<DAW_FREQUENCY_BINS;++k){cJSON_AddItemToArray(bins,cJSON_CreateNumber(power[k]));total+=power[k];if(power[k]>power[peak])peak=k;}
    cJSON_AddNumberToObject(j,"window_weighted_mean_square",total);cJSON_AddItemToObject(j,"window_weighted_rms_dbfs",db(total));cJSON_AddItemToObject(j,"dominant_bin_hz",total>0?cJSON_CreateNumber((double)peak*c->sample_rate/DAW_FREQUENCY_FFT):cJSON_CreateNull());
    const double edges[]={0,31.25,62.5,125,250,500,1000,2000,4000,8000,16000,32000};double low=0,nyquist=c->sample_rate*.5;cJSON* bands=cJSON_AddArrayToObject(j,"bands");
    for(int b=1;low<nyquist;++b){double high=b<12?fmin(edges[b],nyquist):nyquist;double sum=0;for(int k=0;k<DAW_FREQUENCY_BINS;++k){double hz=(double)k*c->sample_rate/DAW_FREQUENCY_FFT;if(hz>=low && (hz<high || (high==nyquist && hz==high)))sum+=power[k];}
        cJSON* band=cJSON_CreateObject();cJSON_AddItemToArray(bands,band);cJSON_AddNumberToObject(band,"low_hz",low);cJSON_AddNumberToObject(band,"high_hz",high);cJSON_AddNumberToObject(band,"mean_square_power",sum);cJSON_AddItemToObject(band,"rms_dbfs",db(sum));low=high;
    }
}
