#pragma once
#include "engine/instrument.h"

// Owns one voice's TPT low-pass memory and smoothed coefficients, never shared between audio and diagnostics.
typedef struct EngineInstrumentFilter {
    double integrator1, integrator2, g, k, target_g, target_k, smoothing;
    float cutoff, resonance;
    uint64_t next_frame;
    bool active;
} EngineInstrumentFilter;

// Identifies the additive voices that own stateful filtering.
bool engine_instrument_filter_preset(EngineInstrumentPresetId preset);
// Resets one voice and its coefficient smoothing without heap operations.
void engine_instrument_filter_reset(EngineInstrumentFilter* filter);
// Filters one raw oscillator sample with cutoff/Q bounded to the current sample rate.
float engine_instrument_filter_process(EngineInstrumentFilter* filter, float sample,
    EngineInstrumentParams params, int rate, uint64_t frame);
