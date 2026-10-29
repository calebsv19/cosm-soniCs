#pragma once
#include "engine/instrument.h"
// Preloads and validates the fixed bundled acoustic sample banks on the control thread.
bool engine_instrument_samples_init(void);
// Reports original violin bank readiness without performing I/O or allocation.
bool engine_instrument_samples_ready(void);
// Evaluates a deterministic looped mono violin voice with recorded soft/strong dynamics.
float engine_instrument_samples_note(uint8_t note, float velocity, EngineInstrumentParams params,
    int rate, uint64_t age, uint64_t gate);

// Evaluates the actual sample voice gate envelope, including independent click-protection floors.
float engine_instrument_samples_envelope(EngineInstrumentParams params, int rate, uint64_t age, uint64_t gate);

// Describes one pinned sample bank's actual sounding-pitch coverage and dynamics.
typedef struct EngineSampleBankInfo {
    const char* id;
    int minimum_note, maximum_note, recorded_pitches, layers;
    const char* articulation;
} EngineSampleBankInfo;
// Classifies only the installed sampled acoustic voices.
bool engine_instrument_sampled_preset(EngineInstrumentPresetId preset);
// Reads immutable bank metadata without file access or allocation.
bool engine_instrument_sample_bank_info(EngineInstrumentPresetId preset, EngineSampleBankInfo* out);
// Reports bank readiness after the shared control-thread preload.
bool engine_instrument_sample_bank_ready(EngineInstrumentPresetId preset);
// Samples a selected bank with adjacent recorded-layer blending and the common protected gate.
float engine_instrument_sample_bank_note(EngineInstrumentPresetId preset, uint8_t note, float velocity,
    EngineInstrumentParams params, int rate, uint64_t age, uint64_t gate);

// Selects an octave of C within the recorded range for finite audition and waveform inspection.
uint8_t engine_instrument_preview_note(EngineInstrumentPresetId preset);
