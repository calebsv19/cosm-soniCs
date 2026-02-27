#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "core_wake.h"

// Tracks wake bridge counters and registration state for DAW main-thread wake events.
typedef struct DawMainThreadWakeStats {
    uint32_t event_type;
    uint32_t pushes;
    uint32_t received;
    uint32_t push_failures;
} DawMainThreadWakeStats;

// Initializes the DAW wake bridge using an SDL user event backed by core_wake external hooks.
bool daw_mainthread_wake_init(void);
// Shuts down the DAW wake bridge and clears wake registration state.
void daw_mainthread_wake_shutdown(void);
// Signals the DAW wake bridge to wake the UI loop.
bool daw_mainthread_wake_push(void);
// Returns true when an SDL event is the DAW wake event.
bool daw_mainthread_wake_is_event(const SDL_Event* event);
// Records that a wake event was received by the main thread.
void daw_mainthread_wake_note_received(void);
// Waits for an SDL event up to timeout and returns true when one is received.
SDL_bool daw_mainthread_wake_wait_for_event(uint32_t timeout_ms, SDL_Event* out_event);
// Copies current DAW wake stats for diagnostics.
void daw_mainthread_wake_snapshot(DawMainThreadWakeStats* out);
// Returns the underlying core_wake bridge when initialized.
CoreWake* daw_mainthread_wake_get_core_wake(void);
