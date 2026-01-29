#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double bpm;          // beats per minute
    int ts_num;          // time signature numerator (e.g., 4)
    int ts_den;          // time signature denominator (e.g., 4)
    double sample_rate;  // Hz
} TempoState;

// Describes a tempo change at a beat position.
typedef struct {
    double beat;
    double bpm;
} TempoEvent;

// Describes a time signature change at a beat position.
typedef struct {
    double beat;
    int ts_num;
    int ts_den;
} TimeSignatureEvent;

// Stores tempo changes across the timeline in beat space.
typedef struct {
    TempoEvent* events;
    int event_count;
    int event_capacity;
    double sample_rate;
} TempoMap;

// Stores time signature changes across the timeline in beat space.
typedef struct {
    TimeSignatureEvent* events;
    int event_count;
    int event_capacity;
} TimeSignatureMap;

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

// Initialize a tempo map with a default event at beat 0.
void tempo_map_init(TempoMap* map, double sample_rate);
// Release memory owned by a tempo map.
void tempo_map_free(TempoMap* map);
// Replace all tempo events with the provided list.
bool tempo_map_set_events(TempoMap* map, const TempoEvent* events, int count);
// Add or update a tempo event at the given beat.
bool tempo_map_upsert_event(TempoMap* map, double beat, double bpm);
// Update a tempo event by index and keep the map sorted.
bool tempo_map_update_event(TempoMap* map, int index, double beat, double bpm);
// Return the active tempo event for a beat position.
const TempoEvent* tempo_map_event_at_beat(const TempoMap* map, double beat);
// Convert between beats and seconds using the tempo map.
double tempo_map_beats_to_seconds(const TempoMap* map, double beats);
double tempo_map_seconds_to_beats(const TempoMap* map, double seconds);
// Convert between beats and samples using the tempo map.
double tempo_map_beats_to_samples(const TempoMap* map, double beats);
double tempo_map_samples_to_beats(const TempoMap* map, int64_t samples);

// Initialize a time signature map with a default 4/4 event at beat 0.
void time_signature_map_init(TimeSignatureMap* map);
// Release memory owned by a time signature map.
void time_signature_map_free(TimeSignatureMap* map);
// Replace all time signature events with the provided list.
bool time_signature_map_set_events(TimeSignatureMap* map, const TimeSignatureEvent* events, int count);
// Add or update a time signature event at the given beat.
bool time_signature_map_upsert_event(TimeSignatureMap* map, double beat, int ts_num, int ts_den);
// Return the active time signature event for a beat position.
const TimeSignatureEvent* time_signature_map_event_at_beat(const TimeSignatureMap* map, double beat);
// Return the beat unit length (in beat space) for a time signature.
double time_signature_beat_unit(const TimeSignatureEvent* sig);
// Return the beats-per-bar length (in beat space) for a time signature.
double time_signature_beats_per_bar(const TimeSignatureEvent* sig);
// Compute bar/beat indices for a beat position with time signature changes.
void time_signature_map_beat_to_bar_beat(const TimeSignatureMap* map,
                                         double beat,
                                         int* out_bar,
                                         int* out_beat,
                                         double* out_sub_beat,
                                         int* out_ts_num,
                                         int* out_ts_den);

// Populate a TimeContext from a sample position and tempo.
void time_context_populate(TimeContext* ctx, int64_t sample_pos, const TempoState* tempo);
