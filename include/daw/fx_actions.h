#pragma once
#include "daw/app_actions.h"
#include "engine/fx_chain.h"
struct AppState;
// Copies one generation-bound track or master effect chain without borrowed engine memory.
typedef struct DawFxSnapshot { uint64_t generation, track_id; FxMasterSnapshot chain; } DawFxSnapshot;
// Owns the exact before/after chain state used by guarded structural and parameter history.
typedef struct DawFxHistory { DawFxSnapshot before, after; } DawFxHistory;
// Copies a track chain; track_id zero addresses the master for the supplied engine lifetime.
DawActionStatus daw_action_get_fx(const struct AppState*, uint64_t generation, uint64_t track_id, DawFxSnapshot*);
// Compares semantic chain values independently of padding and unused parameter storage.
bool daw_fx_snapshot_equal(const DawFxSnapshot*, const DawFxSnapshot*);
// Applies one guarded chain transaction after reserving complete history ownership.
DawActionStatus daw_action_edit_fx(struct AppState*, const DawFxSnapshot*, const EngineFxChainRequest*, DawFxSnapshot*);
// Restores only the matching complete state at the current history cursor.
bool daw_fx_history_restore(struct AppState*, const DawFxHistory*, bool after);
