#pragma once
#include <stdbool.h>
struct AppState;
// Owns a private local endpoint; its transport thread never accesses application state.
typedef struct DawControlBridge DawControlBridge;
// Starts an optional endpoint in an existing owner-only directory, refusing existing paths.
DawControlBridge* daw_control_bridge_start(const char* path, bool allow_write);
// Dispatches one request on the owner thread and returns true only for a newly applied edit.
bool daw_control_bridge_poll(DawControlBridge* bridge, struct AppState* state);
// Stops transport and releases retained receipts before application state is destroyed.
void daw_control_bridge_stop(DawControlBridge* bridge);
