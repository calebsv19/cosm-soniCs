#pragma once
#include "daw/midi_actions.h"
#include "engine/engine.h"
#define DAW_MIDI_LAYOUT_TRACK_CAP 128
#define DAW_MIDI_LAYOUT_CLIP_CAP 2048
struct UndoMidiRegion;
// Copies one lane placement without retaining engine storage or mutable array indices.
typedef struct DawMidiPlacement { uint64_t track_id, clip_id, start, duration, offset; EngineClipKind kind; } DawMidiPlacement;
// Owns bounded exact project placement and tempo state for guarded structural creation.
typedef struct DawMidiLayout {
    uint64_t generation;
    int track_count, clip_count, tempo_count;
    uint64_t track_ids[DAW_MIDI_LAYOUT_TRACK_CAP];
    DawMidiPlacement clips[DAW_MIDI_LAYOUT_CLIP_CAP];
    double sample_rate;
    TempoEvent tempo[DAW_MIDI_TEMPO_EVENT_CAP];
} DawMidiLayout;
// Copies the project layout on its owner thread, including a zero-track project.
DawMidiResult daw_action_get_midi_layout(const struct AppState*, DawMidiLayout*);
// Creates an empty region and optionally one new trailing track as a single reserved transaction.
DawMidiResult daw_action_create_midi_region(struct AppState*, const DawMidiLayout*, const DawActionTarget* track,
                                          uint64_t start, uint64_t duration, DawActionTarget* out);
// Extends only the right edge against exact MIDI state, preserving notes, offsets and sound.
DawMidiResult daw_action_extend_midi_region(struct AppState*, const DawMidiSnapshot*, uint64_t duration);
// Restores guarded region creation/extension history without consuming stale commands.
bool daw_midi_region_history_restore(struct AppState*, struct UndoMidiRegion*, bool after);
