#pragma once
#include "daw/midi_regions.h"
#define DAW_ARRANGEMENT_BATCH_CAP 16
#define DAW_ARRANGEMENT_CLIP_CAP 256
#define DAW_ARRANGEMENT_NOTE_CAP 32768
#define DAW_ARRANGEMENT_POINT_CAP 65536
struct UndoCommand;
// Owns complete project placement/tempo and immutable clip/track-sound preconditions.
typedef struct DawArrangementSnapshot { DawMidiLayout layout; EngineClipContentSnapshot* content; } DawArrangementSnapshot;
// Describes one explicit existing-region edit; omitted destination retains its current lane.
typedef struct DawArrangementEdit {
    EngineArrangementOp operation;
    DawActionTarget target;
    uint64_t destination_track_id, start_frame, duration_frames;
} DawArrangementEdit;
// Carries exact structural history and stable region identities for batch selection/readback.
typedef struct DawArrangementHistory {
    DawMidiLayout before,after;
    int count;
    uint64_t input_ids[DAW_ARRANGEMENT_BATCH_CAP],output_ids[DAW_ARRANGEMENT_BATCH_CAP];
} DawArrangementHistory;
// Captures bounded immutable arrangement state on the application control owner.
DawMidiResult daw_action_get_arrangement(const struct AppState*,DawArrangementSnapshot*);
// Checks every captured placement, note, sound and automation field without hashes or mutable references.
bool daw_arrangement_snapshot_current(const struct AppState*,const DawArrangementSnapshot*);
// Releases only one copied precondition; project-invalidated snapshots can still be released safely.
void daw_arrangement_snapshot_free(DawArrangementSnapshot*);
// Reserves independent history before publishing a complete nonoverlapping batch with stable output identities.
DawMidiResult daw_action_arrange(struct AppState*,const DawArrangementSnapshot*,const DawArrangementEdit*,int count,DawActionTarget* outputs);
// Replays arrangement content only when the opposing complete layout and affected content still match.
bool daw_arrangement_history_restore(struct AppState*,const struct UndoCommand*,bool after);
