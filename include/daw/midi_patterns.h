#pragma once
#include "daw/midi_actions.h"
#define DAW_MIDI_PATTERN_CAP 16
// Selects a repeatable four-quarter-beat major-key starter phrase.
typedef enum DawMidiPatternKind { DAW_MIDI_MELODY, DAW_MIDI_CHORDS, DAW_MIDI_ARPEGGIO } DawMidiPatternKind;
// Specifies a tonic pitch, region-relative beat offset and positive normalized note velocity.
typedef struct DawMidiPattern { DawMidiPatternKind kind; int root_note; double start_beat; float velocity; } DawMidiPattern;
// Retains bounded generated events with no project mutation or borrowed storage.
typedef struct DawMidiPatternNotes { EngineMidiNote notes[DAW_MIDI_PATTERN_CAP]; int count; } DawMidiPatternNotes;
// Converts one major-key phrase through the captured project tempo map with strict pitch/bounds checks.
DawMidiResult daw_midi_pattern_build(const DawMidiSnapshot*, DawMidiPattern, DawMidiPatternNotes*);
