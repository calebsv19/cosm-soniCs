#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double bpm;          // beats per minute
    int ts_num;          // time signature numerator (e.g., 4)
    int ts_den;          // time signature denominator (e.g., 4)
    double sample_rate;  // Hz
} TempoState;

typedef struct {
    double sample_rate;
    int64_t sample_pos;
    double time_seconds;
    double time_beats;
    TempoState tempo;
} TimeContext;

// Reasonable defaults: 120 BPM, 4/4, provided sample_rate.
TempoState tempo_state_default(double sample_rate);

// Clamp tempo/time signature to safe ranges.
void tempo_state_clamp(TempoState* tempo);

// Conversions between seconds/beats/samples using the given tempo.
double tempo_seconds_to_beats(double seconds, const TempoState* tempo);
double tempo_beats_to_seconds(double beats, const TempoState* tempo);
double tempo_seconds_to_samples(double seconds, const TempoState* tempo);
double tempo_samples_to_seconds(int64_t samples, const TempoState* tempo);

// Populate a TimeContext from a sample position and tempo.
void time_context_populate(TimeContext* ctx, int64_t sample_pos, const TempoState* tempo);
