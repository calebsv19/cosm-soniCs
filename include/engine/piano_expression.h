#pragma once
#include "engine/instrument.h"

// Owns fixed string modes and pedal transition playback without render-time allocation.
typedef struct EnginePianoExpression {
    double cosine[88], sine[88], real[88], imaginary[88];
    int rate;
    bool pedal_down, started, pedal_active;
    uint64_t pedal_frame, next_frame;
    unsigned pedal_variant, pedal_counts[2];
} EnginePianoExpression;
// Preloads the attributed key-release and pedal recordings before rendering.
bool engine_piano_expression_init(void);
// Clears resonant energy and computes rate-specific string coefficients.
void engine_piano_expression_reset(EnginePianoExpression* state, int rate);
// Evaluates a chromatic mechanical key-release layer with held-time attenuation.
float engine_piano_key_release(uint8_t note, float velocity, float amount, float level,
    int rate, uint64_t age, uint64_t key_gate);
// Drives open string modes and recorded pedal transitions from the dry piano signal.
float engine_piano_expression_process(EnginePianoExpression* state, EngineInstrumentParams params,
    float dry, const bool held[88], uint64_t frame);
// Finds the first pedal-up threshold after key release using prepared automation segments.
uint64_t engine_piano_damper_frame(uint64_t key_off, uint64_t end, uint64_t start,
    const EngineAutomationLane* track, int track_count, const EngineAutomationLane* clip, int clip_count,
    EngineInstrumentParams (*params_at)(void*, uint64_t), void* context);
