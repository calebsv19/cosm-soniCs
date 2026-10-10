#pragma once
#include "daw/midi_actions.h"
// Selects one explicit whole-region expression transformation.
typedef enum DawMidiExpressionKind {
    DAW_MIDI_QUANTIZE, DAW_MIDI_VELOCITY_SCALE, DAW_MIDI_HUMANIZE
} DawMidiExpressionKind;
// Describes physical/beat settings for reproducible expression with unchanged note lengths.
typedef struct DawMidiExpression {
    DawMidiExpressionKind kind;
    double grid_beats, strength, swing, velocity_scale, timing_ms, velocity_amount;
    uint32_t seed;
} DawMidiExpression;
// Transforms a complete exact snapshot through the shared one-step MIDI edit owner.
DawMidiResult daw_action_transform_midi(struct AppState*, const DawMidiSnapshot*, DawMidiExpression);
