#pragma once
#include "daw/app_actions.h"
#include "engine/midi.h"
#include "time/tempo.h"
#define DAW_MIDI_TEMPO_EVENT_CAP 1024
struct UndoMidiNoteEdit;
// Distinguishes accepted MIDI edits from explicit ownership, state and preparation refusals.
typedef enum DawMidiStatus {
    DAW_MIDI_APPLIED, DAW_MIDI_UNCHANGED, DAW_MIDI_INVALID, DAW_MIDI_WRONG_THREAD,
    DAW_MIDI_STALE_TARGET, DAW_MIDI_STATE_CONFLICT, DAW_MIDI_BUSY,
    DAW_MIDI_HISTORY_UNAVAILABLE, DAW_MIDI_ENGINE_REJECTED
} DawMidiStatus;
// Owns independent notes and tempo events for an exact region/timing precondition.
typedef struct DawMidiSnapshot {
    DawActionTarget target;
    uint64_t start_frame, duration_frames, offset_frames;
    int note_count;
    EngineMidiNote* notes;
    TempoMap tempo;
} DawMidiSnapshot;
// Selects complete replacement or a merged append without implicit region extension.
typedef enum DawMidiEditMode { DAW_MIDI_REPLACE, DAW_MIDI_APPEND } DawMidiEditMode;
// Describes one bounded note-list transaction against an explicit captured state.
typedef struct DawMidiRequest {
    const DawMidiSnapshot* expected;
    const EngineMidiNote* notes;
    int note_count;
    DawMidiEditMode mode;
} DawMidiRequest;
// Reports one transaction outcome without borrowing mutable notes or allocating response storage.
typedef struct DawMidiResult { DawMidiStatus status; const char* message; int note_count; } DawMidiResult;
// Copies the addressed region and current tempo map into an empty caller-owned snapshot.
DawMidiResult daw_action_get_midi(const struct AppState*, DawActionTarget, DawMidiSnapshot*);
// Compares complete semantic note, bounds, identity and tempo state without hashes.
bool daw_midi_snapshot_equal(const DawMidiSnapshot*, const DawMidiSnapshot*);
// Reports active transport, recording and gesture ownership before stopped MIDI mutations.
bool daw_midi_actions_busy(const struct AppState*);
// Releases copied notes and tempo events and resets the snapshot for reuse.
void daw_midi_snapshot_free(DawMidiSnapshot*);
// Validates the complete edit and reserves independent history before atomic note publication.
DawMidiResult daw_action_edit_midi(struct AppState*, const DawMidiRequest*);
// Restores guarded note history only when identity, bounds and the expected opposing notes still match.
bool daw_midi_history_restore(struct AppState*, const struct UndoMidiNoteEdit*, bool after);
