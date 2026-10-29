#ifndef ENGINE_HAT_CHOKE_H
#define ENGINE_HAT_CHOKE_H
#include "engine/engine.h"
#include <stddef.h>

// Identifies one audible authored hat onset in absolute track time.
typedef struct EngineHatChokeEvent {
    uint64_t frame, clip_id;
    int note_index;
} EngineHatChokeEvent;

// Owns sorted control-thread onset context only until source preparation completes.
typedef struct EngineHatChokeTimeline {
    EngineHatChokeEvent* events;
    size_t count;
} EngineHatChokeTimeline;

// Captures active opt-in hat regions, excluding onsets at or beyond the input boundary.
bool engine_hat_choke_prepare(EngineHatChokeTimeline*, const Engine*, int track, uint64_t end);
// Copies next-onset fences into one prepared source without retaining authored storage.
bool engine_hat_choke_apply(const EngineHatChokeTimeline*, const EngineClip*, EngineInstrumentSource*);
// Releases preparation-only onset context on success or rollback.
void engine_hat_choke_free(EngineHatChokeTimeline*);
#endif
