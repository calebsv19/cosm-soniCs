#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DAW_FREQUENCY_FFT 2048
#define DAW_FREQUENCY_BINS (DAW_FREQUENCY_FFT/2+1)
#define DAW_FREQUENCY_WINDOWS 32
struct DawMixCapture;
struct cJSON;
// Owns bounded output-window PCM during capture and immutable one-sided bin powers afterward.
typedef struct DawMixFrequency {
    int frames;
    uint64_t start[DAW_FREQUENCY_WINDOWS];
    float* samples;
    float* powers;
    uint64_t* counts;
} DawMixFrequency;
// Prepares all spectral storage before the private graph renders.
bool daw_mix_frequency_prepare(struct DawMixCapture*);
// Copies only selected node-output windows on latency-corrected authored time.
bool daw_mix_frequency_observe(struct DawMixCapture*,int,uint64_t,uint64_t,const float*,int,int);
// Computes calibrated power spectra and releases the temporary window PCM.
bool daw_mix_frequency_finish(struct DawMixCapture*);
// Releases only private spectral storage.
void daw_mix_frequency_free(DawMixFrequency*);
// Serializes one channel/window page without exposing temporary PCM.
void daw_control_mix_frequency_json(struct cJSON*,const struct DawMixCapture*,int,int,int);
