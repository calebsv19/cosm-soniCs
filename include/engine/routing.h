#pragma once
#include <stdbool.h>
#include "engine/mixer_automation.h"
#include <stdint.h>
#define ENGINE_SEND_CAP 4
#define ENGINE_RETURN_CAP 4
#define ENGINE_ROUTING_TRACK_CAP 128
struct Engine;
// Stores one post-fader send to an independently owned stable return-lane identity.
typedef struct EngineSend { uint64_t return_id; float gain; EngineMixerAutomation automation; } EngineSend;
// Copies the routing fields of one existing lane without borrowing clip or DSP storage.
typedef struct EngineRoutingTrack { uint64_t track_id; bool is_return; bool is_group; uint64_t parent_id; int sibling_order; bool folded; EngineSend sends[ENGINE_SEND_CAP]; EngineMixerAutomation volume_automation; } EngineRoutingTrack;
// Captures complete bounded track topology and authored routing for exact guarded publication.
typedef struct EngineRoutingSnapshot { uint64_t generation; int track_count; EngineRoutingTrack tracks[ENGINE_ROUTING_TRACK_CAP]; } EngineRoutingSnapshot;
// Copies routing only on the creating control thread, including an empty project.
bool engine_routing_capture(const struct Engine*, EngineRoutingSnapshot*);
// Publishes valid source-to-return routing once after exact expected-state validation.
bool engine_routing_set(struct Engine*, const EngineRoutingSnapshot* expected, const EngineRoutingSnapshot* next);
// Reports incoming authored sends so native track removal cannot silently discard routing.
bool engine_routing_track_referenced(const struct Engine*, uint64_t track_id);

// Compares complete authored routing fields independently of padding.
bool engine_routing_snapshot_equal(const EngineRoutingSnapshot*, const EngineRoutingSnapshot*);
// Validates a complete candidate on the control thread without publishing or changing metadata.
bool engine_routing_validate(const struct Engine*, const EngineRoutingSnapshot*);

// Appends one empty return in one publication; identity zero creates a fresh lifetime and a retained ID supports redo.
bool engine_append_return(struct Engine*, uint64_t identity, uint64_t* out_id);

// Restores authored persistent identities into a private candidate on the owner thread.
bool engine_restore_persistent_ids(struct Engine*, const uint64_t* ids, int count);
// Appends a named empty group in one publication with a retained identity for redo.
bool engine_append_group(struct Engine*, uint64_t identity, const char* name, uint64_t* out_id);
