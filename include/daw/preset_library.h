#pragma once
#include "engine/instrument.h"
#include <stddef.h>
#define DAW_PRESET_CAPACITY 128
#define DAW_PRESET_PATH_CAPACITY 1024
// Stores a complete portable sound independently of its display name and filesystem location.
typedef struct DawUserPreset {
    char id[33], revision[33], name[96];
    EngineInstrumentPresetId base;
    EngineInstrumentParams params;
} DawUserPreset;
// Copies a bounded library view, reporting invalid files and overflow instead of hiding them.
typedef struct DawPresetList {
    DawUserPreset items[DAW_PRESET_CAPACITY];
    int count, invalid_count;
    bool truncated;
    char root[DAW_PRESET_PATH_CAPACITY];
} DawPresetList;
// Separates successful publication from input, revision, storage and compatibility failures.
typedef enum DawPresetStatus {
    DAW_PRESET_OK, DAW_PRESET_INVALID, DAW_PRESET_NOT_FOUND, DAW_PRESET_CONFLICT,
    DAW_PRESET_IO, DAW_PRESET_BUSY, DAW_PRESET_UNSUPPORTED
} DawPresetStatus;
// Resolves the configured personal library without creating it during reads.
bool daw_preset_library_root(char* out, size_t capacity, bool create);
// Validates an opaque identifier before it can become a filename.
bool daw_preset_id_valid(const char* id);
// Returns a stable diagnostic for library and application callers.
const char* daw_preset_status_message(DawPresetStatus status);
// Copies sorted entries without retaining file handles or borrowing mutable JSON memory.
DawPresetStatus daw_preset_library_list(DawPresetList* out);
// Loads only the addressed current revision, rejecting links and unsupported sound versions.
DawPresetStatus daw_preset_library_get(const char* id, const char* revision, DawUserPreset* out);
// Creates a unique file or explicitly updates a matching revision with one recoverable previous copy.
DawPresetStatus daw_preset_library_save(const char* name, EngineInstrumentPresetId base,
    EngineInstrumentParams params, const char* id, const char* revision, DawUserPreset* out, bool* durable);
// Compares complete sound settings, including parameters dormant in the selected voice.
bool daw_preset_sound_equal(const DawUserPreset* preset, EngineInstrumentPresetId base, EngineInstrumentParams params);

// Validates bounded printable UTF-8 labels shared by instrument and effect-chain libraries.
bool daw_preset_name_valid(const char* name);
