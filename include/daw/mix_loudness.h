#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DAW_LOUDNESS_POINTS 301
#define DAW_LOUDNESS_PROGRAM_SECONDS 600
#define DAW_LOUDNESS_TAIL_SECONDS 30
#define DAW_LOUDNESS_SHORT_TERMS 6301
#include "ebur128/ebur128.h"
struct cJSON;
struct DawMixCapture;
// Owns private loudness DSP until finish and immutable original-level passage measurements afterward.
typedef struct DawLoudness {
    ebur128_state* state;
    uint64_t frames;
    int rate,channels,count;
    bool available,finished,programme;
    int short_term_count,lra_gated_count;
    double* short_terms;
    double lra,lra_threshold,lra_low,lra_high;
    double integrated,momentary,short_term,relative_gate,true_peak[2];
    uint64_t at[DAW_LOUDNESS_POINTS];
    double m[DAW_LOUDNESS_POINTS],s[DAW_LOUDNESS_POINTS];
} DawLoudness;
// Creates a meter with explicit unit-weight mono or left/right stereo and qualified sample rates.
bool daw_loudness_init(DawLoudness*,int,int);
// Feeds finite interleaved samples, recording complete windows every 100 milliseconds.
bool daw_loudness_add(DawLoudness*,const float*,int);
// Freezes loudness before zero-padding only the true-peak interpolator's terminal delay.
bool daw_loudness_finish(DawLoudness*);
// Releases private meter state without changing the captured results.
void daw_loudness_free(DawLoudness*);
// Computes current EBU LRA from complete 3 s windows sampled every 100 ms.
bool daw_loudness_range_finish(DawLoudness*);
// Creates one independent processed-output meter for every private graph node.
bool daw_mix_loudness_prepare(struct DawMixCapture*);
// Observes only the selected range on the node's own latency-corrected clock.
bool daw_mix_loudness_observe(struct DawMixCapture*,int,uint64_t,uint64_t,const float*,int,int);
// Finishes all node measurements, rejecting missing or duplicated samples.
bool daw_mix_loudness_finish(struct DawMixCapture*);
// Serializes immutable selected-range loudness, complete-window history and channel true peaks.
void daw_control_mix_loudness_json(struct cJSON*,const struct DawMixCapture*,int);
