#pragma once

#include "engine/instrument.h"
#include <stdbool.h>
#include <stdint.h>

struct AppState;

// Identifies the authored object addressed by an application action.
typedef enum DawActionTargetKind {
    DAW_ACTION_TARGET_TRACK = 0,
    DAW_ACTION_TARGET_MIDI_REGION
} DawActionTargetKind;

// Binds an object to one engine/project lifetime, without exposing pointers or array indices.
typedef struct DawActionTarget {
    uint64_t project_generation;
    uint64_t track_id;
    uint64_t clip_id;
    DawActionTargetKind kind;
} DawActionTarget;

// Distinguishes committed authored edits from no-ops and explicit rejection causes.
typedef enum DawActionStatus {
    DAW_ACTION_APPLIED = 0,
    DAW_ACTION_UNCHANGED,
    DAW_ACTION_INVALID,
    DAW_ACTION_WRONG_THREAD,
    DAW_ACTION_STALE_TARGET,
    DAW_ACTION_BUSY,
    DAW_ACTION_HISTORY_UNAVAILABLE,
    DAW_ACTION_ENGINE_REJECTED
} DawActionStatus;

// Stores exact authored instrument settings, including inactive or inherited overrides for history.
typedef struct DawInstrumentState {
    EngineInstrumentPresetId preset;
    EngineInstrumentParams params;
    bool enabled;
    bool inherits_track;
} DawInstrumentState;

// Reports authored settings alongside the effective settings heard by a MIDI region.
typedef struct DawInstrumentSnapshot {
    DawActionTarget target;
    DawInstrumentState stored;
    EngineInstrumentPresetId effective_preset;
    EngineInstrumentParams effective_params;
} DawInstrumentSnapshot;

// Requests one preset change and/or a batch of parameter overrides selected by parameter-ID bits.
typedef struct DawInstrumentRequest {
    DawActionTarget target;
    bool set_preset;
    EngineInstrumentPresetId preset;
    uint32_t parameter_mask;
    EngineInstrumentParams params;
} DawInstrumentRequest;

// Returns accepted authored state; success does not claim render-worker adoption or audible delivery.
typedef struct DawActionResult {
    DawActionStatus status;
    const char* message;
    bool has_snapshot;
    DawInstrumentSnapshot snapshot;
} DawActionResult;

// Holds ownership of one reserved instrument gesture across updates and selection changes.
typedef struct DawInstrumentGesture {
    DawActionTarget target;
    uint64_t history_serial;
    bool active;
} DawInstrumentGesture;

// Resolves generation-bound identities to owner-thread indices for common application actions.
DawActionStatus daw_action_target_resolve(const struct AppState*, DawActionTarget, int* track, int* clip);
// Captures a target on the control thread; clip_index -1 addresses a track default.
DawActionStatus daw_instrument_target_capture(const struct AppState* state, int track_index,
                                             int clip_index, DawActionTarget* out);
// Copies a bounded instrument snapshot on the owning control thread.
DawActionResult daw_action_get_instrument(const struct AppState* state, DawActionTarget target);
// Applies a complete request in one publication and one pre-reserved undo entry.
DawActionResult daw_action_set_instrument(struct AppState* state, const DawInstrumentRequest* request);
// Reserves history before any continuous instrument edit may publish.
DawActionResult daw_action_begin_instrument(struct AppState* state, DawActionTarget target,
                                           DawInstrumentGesture* gesture);
// Updates only the captured target while the caller still owns its history reservation.
DawActionResult daw_action_update_instrument(struct AppState* state, DawInstrumentGesture* gesture,
                                            const DawInstrumentRequest* request);
// Commits the accepted preview or restores its baseline; rejected cancellation retains ownership.
DawActionResult daw_action_finish_instrument(struct AppState* state, DawInstrumentGesture* gesture,
                                            bool commit);
// Restores scalar instrument history through the same generation/identity checks without adding history.
bool daw_instrument_restore(struct AppState* state, DawActionTarget target, const DawInstrumentState* value);
