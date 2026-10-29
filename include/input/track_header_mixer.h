#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
struct AppState;
// Retains a relative header gesture and its keyboard focus by stable lifetime/history ownership.
typedef struct DawTrackHeaderMixer {
    bool dragging,pan,focus_pan,wait_release;
    uint64_t generation,track_id,serial,focus_id,focus_generation;
    int start_y;
    float start_position,scale;
    SDL_Rect bounds;
} DawTrackHeaderMixer;
// Routes native header gain/pan gestures and focused keyboard increments before timeline selection.
bool daw_track_header_mixer_event(struct AppState*,const SDL_Event*);
// Ends accepted continuous edits on resize/focus/modal/geometry changes without losing their undo entry.
void daw_track_header_mixer_finish(struct AppState*,bool revert);
// Cancels stale geometry or target ownership during sampled application updates.
void daw_track_header_mixer_sync(struct AppState*);
