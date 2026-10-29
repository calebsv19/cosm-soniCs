#pragma once
#include "engine/instrument.h"

// Identifies the dedicated deterministic percussion voice family.
bool engine_instrument_percussion_preset(EngineInstrumentPresetId preset);
// Identifies hat voices that can share a region-local choke group.
bool engine_instrument_percussion_hat(EngineInstrumentPresetId preset);
// Identifies gate-controlled pitched bass voices.
bool engine_instrument_percussion_808(EngineInstrumentPresetId preset);
// Resolves the combined hat preset's closed/open General MIDI note map.
EngineInstrumentPresetId engine_instrument_percussion_note_voice(EngineInstrumentPresetId preset, uint8_t note);
// Samples an 808 with a prepared continuous phase and instantaneous frequency.
float engine_instrument_percussion_sample_phase(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    double instantaneous, double phase, int rate, uint64_t frame, uint64_t gate);
// Distinguishes natural drum hits from gate-controlled pitched 808 voices.
bool engine_instrument_percussion_one_shot(EngineInstrumentPresetId preset);
// Evaluates the percussion body/noise amplitude envelope used by diagnostic plots.
float engine_instrument_percussion_envelope(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    int rate, uint64_t frame, uint64_t gate);
// Samples a pitch-swept body and independently decaying noise without mutable RNG or allocation.
float engine_instrument_percussion_sample(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    double frequency, int rate, uint64_t frame, uint64_t gate);
