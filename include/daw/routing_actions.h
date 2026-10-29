#pragma once
#include "daw/app_actions.h"
#include "engine/routing.h"
#include "undo/undo_manager.h"
struct AppState;
// Owns complete routing metadata for one exact guarded project history transition.
typedef struct DawRoutingHistory {
    EngineRoutingSnapshot before, after;
    bool creates_return;
    UndoCreatedTrack created;
} DawRoutingHistory;
// Captures complete routing on the application owner thread without changing authored state.
DawActionStatus daw_action_get_routing(const struct AppState*, EngineRoutingSnapshot*);
// Reserves complete history and publishes a valid routing candidate once against exact current state.
DawActionStatus daw_action_set_routing(struct AppState*, const EngineRoutingSnapshot* expected,
                                     const EngineRoutingSnapshot* next, EngineRoutingSnapshot* out);
// Restores only the matching complete routing at the shared undo/redo cursor.
bool daw_routing_history_restore(struct AppState*, const DawRoutingHistory*, bool after);
// Applies scoped routing undo/redo after checking the supplied current routing capture.
DawActionStatus daw_action_routing_history(struct AppState*, const EngineRoutingSnapshot* expected,
                                         bool redo, EngineRoutingSnapshot* out);

// Creates one named group after reserving complete routing and created-lane history.
DawActionStatus daw_action_create_group(struct AppState*, const EngineRoutingSnapshot* expected, const char* name, EngineRoutingSnapshot* out);
// Creates one trailing empty return after reserving complete routing and created-lane history.
DawActionStatus daw_action_create_return(struct AppState*, const EngineRoutingSnapshot* expected,
                                        EngineRoutingSnapshot* out);
