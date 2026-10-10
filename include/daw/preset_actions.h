#pragma once
#include "daw/app_actions.h"
#include "daw/preset_library.h"
// Captures a library publication outcome separately from project undo history.
typedef struct DawPresetSaveResult {
    DawPresetStatus status;
    DawUserPreset preset;
    bool durable;
} DawPresetSaveResult;
// Describes one isolated, standardized three-second PCM audition artifact.
typedef struct DawPresetPreview {
    char path[DAW_PRESET_PATH_CAPACITY];
    double peak, rms;
    bool durable;
} DawPresetPreview;
// Saves the effective sound only while the target still matches the caller's captured snapshot.
DawPresetSaveResult daw_action_save_preset(struct AppState*, const DawInstrumentSnapshot*, const char* name,
    const char* id, const char* revision);
// Applies a complete library revision through existing reserved instrument history.
DawActionResult daw_action_apply_preset(struct AppState*, DawActionTarget, const char* id, const char* revision);
// Auditions a finite held C4 or C-major chord through the selected track without authoring notes.
DawActionResult daw_action_audition_instrument(struct AppState*, DawActionTarget, bool chord);
// Renders a bounded private source to an atomically published PCM WAV without touching the project or device.
DawPresetStatus daw_action_preview_preset(struct AppState*, const char* id, const char* revision,
    bool chord, DawPresetPreview* out);
