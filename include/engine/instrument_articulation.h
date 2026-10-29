#pragma once
#include "engine/instrument.h"

// Describes one ordered note transition and its prepared continuous pitch trajectory.
typedef struct EngineArticulationEvent {
    EngineMidiNote note;
    int note_index;
    uint64_t chain_start;
    double start_hz, target_hz, glide_seconds, cycles;
    double pitch_amount, pitch_tau;
    float previous_velocity, pitch_semitones, pitch_decay_ms;
    bool legato;
} EngineArticulationEvent;

// Owns bounded transition storage prepared outside the audio callback.
typedef struct EngineArticulationTimeline {
    EngineArticulationEvent* events;
    int count, capacity;
} EngineArticulationTimeline;

// Supplies existing automated instrument settings at a note onset.
typedef EngineInstrumentParams (*EngineArticulationParamsAt)(void* context, uint64_t frame);
// Reserves transition storage before live notes or source publication.
bool engine_articulation_reserve(EngineArticulationTimeline* timeline, int capacity);
// Releases the timeline on its owning control thread.
void engine_articulation_free(EngineArticulationTimeline* timeline);
// Orders notes deterministically and prepares pitch, phase and region-local choke relationships.
void engine_articulation_prepare(EngineArticulationTimeline* timeline, const EngineMidiNote* notes,
    int count, int rate, EngineArticulationParamsAt params_at, void* context);
// Recomputes physical-time pitch transitions after a sample-rate change without allocation.
void engine_articulation_retime(EngineArticulationTimeline* timeline, int rate,
    EngineArticulationParamsAt params_at, void* context);
// Returns the last-onset-wins transition at a region-relative frame.
const EngineArticulationEvent* engine_articulation_event_at(const EngineArticulationTimeline* timeline, uint64_t frame);
// Reports the base glide frequency and integrated cycles for an independent diagnostic check.
void engine_articulation_pitch(const EngineArticulationEvent* event, int rate, uint64_t frame,
    double* frequency, double* cycles);
// Samples a single phase-continuous 808 voice with legato envelope and velocity handling.
float engine_articulation_808_sample(const EngineArticulationTimeline* timeline,
    EngineInstrumentPresetId preset, EngineInstrumentParams params, int rate, uint64_t frame);
