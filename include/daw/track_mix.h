#pragma once
#include <stdbool.h>
#include <stdint.h>
struct AppState;
// Owns one gain or pan scalar without capturing other mixer/instrument state.
typedef struct DawTrackMixEdit {
    uint64_t generation,track_id;
    bool pan;
    float before,after;
} DawTrackMixEdit;
// Publishes one scalar against exact lifetime/value guards, retaining the prior value on failure.
bool daw_track_mix_write(struct AppState*,const DawTrackMixEdit*,float expected,float value);
// Replays one scalar history transition while preserving all unrelated track content and controls.
bool daw_track_mix_history_apply(struct AppState*,const DawTrackMixEdit*,bool after);
