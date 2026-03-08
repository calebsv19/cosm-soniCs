#include "mem_console_kernel_bridge.h"

#include <stdio.h>
#include <string.h>

typedef struct MemConsoleKernelBridgeModuleCtx {
    MemConsoleKernelBridge *bridge;
} MemConsoleKernelBridgeModuleCtx;

static bool bridge_module_on_init(void *module_ctx) {
    (void)module_ctx;
    return true;
}

static void bridge_module_on_update(void *module_ctx, uint64_t now_ns) {
    MemConsoleKernelBridgeModuleCtx *ctx = (MemConsoleKernelBridgeModuleCtx *)module_ctx;
    if (!ctx || !ctx->bridge) {
        return;
    }

    ctx->bridge->tick_count += 1u;
    ctx->bridge->last_now_ns = now_ns;
}

static bool bridge_module_on_render_hint(void *module_ctx) {
    (void)module_ctx;
    return false;
}

CoreResult mem_console_kernel_bridge_init(MemConsoleKernelBridge *bridge) {
    static MemConsoleKernelBridgeModuleCtx module_ctx;
    CoreKernelPolicy policy;
    CoreKernelModuleHooks hooks = {0};

    if (!bridge) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid kernel bridge" };
    }

    memset(bridge, 0, sizeof(*bridge));

    if (!core_sched_init(&bridge->sched,
                         bridge->sched_timers,
                         MEM_CONSOLE_KERNEL_BRIDGE_SCHED_CAPACITY)) {
        return (CoreResult){ CORE_ERR_IO, "kernel bridge sched init failed" };
    }
    if (!core_jobs_init(&bridge->jobs,
                        bridge->jobs_backing,
                        MEM_CONSOLE_KERNEL_BRIDGE_JOBS_CAPACITY)) {
        return (CoreResult){ CORE_ERR_IO, "kernel bridge jobs init failed" };
    }
    if (!core_wake_init_cond(&bridge->wake)) {
        return (CoreResult){ CORE_ERR_IO, "kernel bridge wake init failed" };
    }

    policy.idle_mode = CORE_KERNEL_IDLE_SPIN;
    policy.frame_cap_hz = 60u;
    policy.job_budget_ms = 1u;
    policy.max_idle_timeout_ms = 0u;
    policy.worker_thread_count = 0u;
    policy.coalesce_input_events = true;

    if (!core_kernel_init(&bridge->kernel,
                          &policy,
                          &bridge->sched,
                          &bridge->jobs,
                          &bridge->wake,
                          bridge->modules,
                          MEM_CONSOLE_KERNEL_BRIDGE_MODULE_CAPACITY)) {
        core_wake_shutdown(&bridge->wake);
        return (CoreResult){ CORE_ERR_IO, "kernel bridge init failed" };
    }

    module_ctx.bridge = bridge;
    hooks.on_init = bridge_module_on_init;
    hooks.on_update = bridge_module_on_update;
    hooks.on_render_hint = bridge_module_on_render_hint;
    if (!core_kernel_register_module(&bridge->kernel, hooks, &module_ctx)) {
        core_kernel_shutdown(&bridge->kernel);
        core_wake_shutdown(&bridge->wake);
        return (CoreResult){ CORE_ERR_IO, "kernel bridge module register failed" };
    }

    bridge->initialized = 1;
    return core_result_ok();
}

void mem_console_kernel_bridge_shutdown(MemConsoleKernelBridge *bridge) {
    if (!bridge) {
        return;
    }
    if (bridge->initialized) {
        core_kernel_shutdown(&bridge->kernel);
        core_wake_shutdown(&bridge->wake);
    }
    memset(bridge, 0, sizeof(*bridge));
}

void mem_console_kernel_bridge_tick(MemConsoleKernelBridge *bridge,
                                    MemConsoleState *state,
                                    uint64_t now_ns) {
    if (!bridge || !state || !bridge->initialized) {
        return;
    }

    core_kernel_tick(&bridge->kernel, now_ns);
    bridge->last_work_units = core_kernel_last_tick_work_units(&bridge->kernel);
    bridge->last_render_requested = core_kernel_render_requested(&bridge->kernel) ? 1 : 0;

    state->kernel_bridge_enabled = 1;
    state->kernel_tick_count = bridge->tick_count;
    state->kernel_last_work_units = bridge->last_work_units;
    state->kernel_last_render_requested = bridge->last_render_requested;
    (void)snprintf(state->kernel_summary_line,
                   sizeof(state->kernel_summary_line),
                   "Kernel on t%llu w%llu r%d",
                   (unsigned long long)state->kernel_tick_count,
                   (unsigned long long)state->kernel_last_work_units,
                   state->kernel_last_render_requested);
}
