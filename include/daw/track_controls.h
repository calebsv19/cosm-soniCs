#pragma once
#include "daw/app_actions.h"
#include "engine/engine.h"
// Identifies one independently owned header field for scoped writes and history.
typedef enum DawTrackControl { DAW_TRACK_NAME, DAW_TRACK_GAIN, DAW_TRACK_PAN } DawTrackControl;
// Copies mixer/label state for one exact track or bus lifetime without borrowed storage.
typedef struct DawTrackControls {
    DawActionTarget target;
    uint64_t persistent_id;
    char name[ENGINE_CLIP_NAME_MAX];
    float gain, pan;
    bool muted, solo, is_group, is_return;
} DawTrackControls;
// Copies current controls through generation and runtime-identity validation.
DawActionStatus daw_track_controls_get(const struct AppState*, DawActionTarget, DawTrackControls*);
// Compares only the addressed field while preserving unrelated controls and authored content.
bool daw_track_controls_equal(const DawTrackControls*, const DawTrackControls*, DawTrackControl);
// Reserves history before applying one guarded base mixer or label edit.
DawActionStatus daw_track_controls_set(struct AppState*, const DawTrackControls*, DawTrackControl, const char*, float);
// Consumes only matching target/field history at the shared cursor against exact current values.
DawActionStatus daw_track_controls_history(struct AppState*, const DawTrackControls*, DawTrackControl, bool);
// Publishes an exact label transition for native and MCP history replay.
bool daw_track_name_write(struct AppState*, DawActionTarget, const char*, const char*);
