#pragma once
#include "daw/app_actions.h"
#include "engine/engine_eq.h"
struct UndoEqCurveEdit;
// Copies one dedicated EQ curve bound to a project and stable track lifetime; zero track ID selects master.
typedef struct DawEqSnapshot { uint64_t generation,track_id; EngineEqCurve curve; } DawEqSnapshot;
// Reads accepted dedicated EQ parameters without consulting mutable UI drafts.
DawActionStatus daw_eq_get(const struct AppState*,uint64_t,uint64_t,DawEqSnapshot*);
// Compares all meaningful stored curve fields exactly, including disabled controls.
bool daw_eq_equal(const DawEqSnapshot*,const DawEqSnapshot*);
// Publishes one guarded curve through the existing engine path and synchronizes native caches.
bool daw_eq_write(struct AppState*,const DawEqSnapshot*,const EngineEqCurve*);
// Reserves one complete curve history step before publication, preserving all unrelated sound state.
DawActionStatus daw_eq_set(struct AppState*,const DawEqSnapshot*,const EngineEqCurve*);
// Replays only matching dedicated EQ history at the shared cursor against exact current parameters.
DawActionStatus daw_eq_history(struct AppState*,const DawEqSnapshot*,bool);
// Binds native and MCP EQ history to accepted parameters and exact engine/track lifetime before reservation.
bool daw_eq_bind_history(struct AppState*,struct UndoEqCurveEdit*);
// Applies native/MCP EQ history through the same exact expected-curve publication guard.
bool daw_eq_restore(struct AppState*,const struct UndoEqCurveEdit*,bool);
