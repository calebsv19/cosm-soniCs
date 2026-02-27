#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "core_sched.h"

// Represents a DAW main-thread timer callback function signature.
typedef void (*DawMainThreadTimerCallback)(void* user_data);

// Captures timer scheduler diagnostics for DAW loop telemetry.
typedef struct DawMainThreadTimerSchedulerStats {
    uint32_t timer_count;
    uint32_t fired_count;
    bool has_deadline;
    Uint32 next_deadline_ms;
} DawMainThreadTimerSchedulerStats;

// Initializes DAW main-thread timer scheduler state.
void daw_mainthread_timer_scheduler_init(void);
// Shuts down DAW main-thread timer scheduler state.
void daw_mainthread_timer_scheduler_shutdown(void);
// Resets DAW main-thread timer scheduler entries and counters.
void daw_mainthread_timer_scheduler_reset(void);
// Schedules a one-shot timer and returns a DAW timer id.
int daw_mainthread_timer_schedule_once(Uint32 delay_ms,
                                       DawMainThreadTimerCallback cb,
                                       void* user_data,
                                       const char* label);
// Schedules a repeating timer and returns a DAW timer id.
int daw_mainthread_timer_schedule_repeating(Uint32 interval_ms,
                                            DawMainThreadTimerCallback cb,
                                            void* user_data,
                                            const char* label);
// Cancels a timer by id and returns true when the timer existed.
bool daw_mainthread_timer_cancel(int timer_id);
// Returns the next timer deadline in milliseconds when available.
bool daw_mainthread_timer_scheduler_next_deadline_ms(Uint32* out_deadline_ms);
// Fires due timers and returns the number of callbacks fired.
int daw_mainthread_timer_scheduler_fire_due(Uint32 now_ms);
// Copies timer scheduler diagnostics.
void daw_mainthread_timer_scheduler_snapshot(DawMainThreadTimerSchedulerStats* out);
// Returns the underlying core_sched scheduler instance when initialized.
CoreSched* daw_mainthread_timer_scheduler_get_core_sched(void);
