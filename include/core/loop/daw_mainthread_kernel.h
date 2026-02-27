#pragma once

#include <stdbool.h>
#include <stdint.h>

// Captures DAW kernel adapter diagnostics for loop orchestration.
typedef struct DawMainThreadKernelStats {
    bool initialized;
    bool render_requested;
    uint64_t last_tick_work_units;
} DawMainThreadKernelStats;

// Initializes the DAW main-thread kernel adapter backed by shared core runtime primitives.
bool daw_mainthread_kernel_init(void);
// Shuts down the DAW main-thread kernel adapter.
void daw_mainthread_kernel_shutdown(void);
// Ticks the DAW main-thread kernel once using a provided timestamp.
void daw_mainthread_kernel_tick(uint64_t now_ns);
// Copies DAW main-thread kernel diagnostics.
void daw_mainthread_kernel_snapshot(DawMainThreadKernelStats* out);
