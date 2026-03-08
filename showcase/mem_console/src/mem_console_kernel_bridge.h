#ifndef MEM_CONSOLE_KERNEL_BRIDGE_H
#define MEM_CONSOLE_KERNEL_BRIDGE_H

#include <stdint.h>

#include "core_jobs.h"
#include "core_kernel.h"
#include "core_sched.h"
#include "core_wake.h"
#include "mem_console_state.h"

enum {
    MEM_CONSOLE_KERNEL_BRIDGE_MODULE_CAPACITY = 1,
    MEM_CONSOLE_KERNEL_BRIDGE_SCHED_CAPACITY = 16,
    MEM_CONSOLE_KERNEL_BRIDGE_JOBS_CAPACITY = 32
};

typedef struct MemConsoleKernelBridge {
    CoreKernel kernel;
    CoreKernelModule modules[MEM_CONSOLE_KERNEL_BRIDGE_MODULE_CAPACITY];
    CoreSched sched;
    CoreSchedTimer sched_timers[MEM_CONSOLE_KERNEL_BRIDGE_SCHED_CAPACITY];
    CoreJobs jobs;
    CoreJob jobs_backing[MEM_CONSOLE_KERNEL_BRIDGE_JOBS_CAPACITY];
    CoreWake wake;
    uint64_t tick_count;
    uint64_t last_work_units;
    uint64_t last_now_ns;
    int last_render_requested;
    int initialized;
} MemConsoleKernelBridge;

CoreResult mem_console_kernel_bridge_init(MemConsoleKernelBridge *bridge);
void mem_console_kernel_bridge_shutdown(MemConsoleKernelBridge *bridge);
void mem_console_kernel_bridge_tick(MemConsoleKernelBridge *bridge,
                                    MemConsoleState *state,
                                    uint64_t now_ns);

#endif
