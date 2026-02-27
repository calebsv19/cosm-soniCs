#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core_jobs.h"

// Represents a DAW main-thread job callback signature.
typedef void (*DawMainThreadJobFn)(void* user_ctx);

// Captures DAW main-thread job queue diagnostics.
typedef struct DawMainThreadJobsStats {
    uint32_t pending;
    uint64_t enqueued;
    uint64_t executed;
    uint64_t dropped;
    uint64_t budget_stops;
} DawMainThreadJobsStats;

// Initializes DAW main-thread jobs queue storage.
void daw_mainthread_jobs_init(void);
// Shuts down DAW main-thread jobs queue state.
void daw_mainthread_jobs_shutdown(void);
// Resets DAW main-thread jobs queue contents and counters.
void daw_mainthread_jobs_reset(void);
// Enqueues one DAW main-thread job.
bool daw_mainthread_jobs_enqueue(DawMainThreadJobFn fn, void* user_ctx);
// Runs jobs up to a millisecond budget and returns executed count.
size_t daw_mainthread_jobs_run_budget_ms(uint64_t budget_ms);
// Runs up to max_jobs jobs and returns executed count.
size_t daw_mainthread_jobs_run_n(size_t max_jobs);
// Copies DAW main-thread jobs diagnostics.
void daw_mainthread_jobs_snapshot(DawMainThreadJobsStats* out);
// Returns the underlying core_jobs queue when initialized.
CoreJobs* daw_mainthread_jobs_get_core_jobs(void);
