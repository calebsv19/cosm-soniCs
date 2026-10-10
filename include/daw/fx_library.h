#pragma once
#include "daw/fx_actions.h"
#include "daw/preset_library.h"
#include "engine/engine.h"
#define DAW_FX_LIBRARY_CAPACITY 128
// Stores complete ordered sound settings without runtime effect or track identities.
typedef struct DawFxPreset { char id[33], revision[33], name[96]; FxMasterSnapshot chain; } DawFxPreset;
// Owns a bounded sorted library view and explicit invalid-file/overflow diagnostics.
typedef struct DawFxPresetList { DawFxPreset items[DAW_FX_LIBRARY_CAPACITY]; int count, invalid_count; bool truncated; } DawFxPresetList;
// Loads a portable chain using the current registry, without creating library files or directories.
DawPresetStatus daw_fx_library_get(const Engine*, const char* id, const char* revision, DawFxPreset*);
// Lists current compatible chains independently of any project target or chain state.
DawPresetStatus daw_fx_library_list(const Engine*, DawFxPresetList*);
// Creates a uniquely identified chain or updates an explicit revision, preserving one previous document.
DawPresetStatus daw_fx_library_save(const Engine*, const char* name, const FxMasterSnapshot*, const char* id, const char* revision, DawFxPreset*, bool* durable);
// Reports storage and application outcomes independently, retaining complete accepted readback when available.
typedef struct DawFxPresetResult { DawActionStatus action; DawPresetStatus library; bool has_preset, durable; DawFxPreset preset; DawFxSnapshot snapshot; } DawFxPresetResult;
// Saves only an exact current owner-thread chain, without adding project history or changing sound.
DawFxPresetResult daw_action_save_fx_preset(struct AppState*, const DawFxSnapshot*, const char* name, const char* id, const char* revision);
// Copies a current library revision through one guarded portable replacement and complete project history.
DawFxPresetResult daw_action_apply_fx_preset(struct AppState*, const DawFxSnapshot*, const char* id, const char* revision);
