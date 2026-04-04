#include "daw/daw_app_main.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef enum DawAppStage {
    DAW_APP_STAGE_INIT = 0,
    DAW_APP_STAGE_BOOTSTRAPPED,
    DAW_APP_STAGE_CONFIG_LOADED,
    DAW_APP_STAGE_STATE_SEEDED,
    DAW_APP_STAGE_SUBSYSTEMS_READY,
    DAW_APP_STAGE_RUNTIME_STARTED,
    DAW_APP_STAGE_LOOP_COMPLETED,
    DAW_APP_STAGE_SHUTDOWN_COMPLETED
} DawAppStage;

typedef struct DawDispatchSummary {
    uint32_t dispatch_count;
    int dispatch_succeeded;
    int last_dispatch_exit_code;
    bool used_legacy_entry;
} DawDispatchSummary;

typedef enum DawWrapperError {
    DAW_WRAPPER_ERROR_NONE = 0,
    DAW_WRAPPER_ERROR_BOOTSTRAP_FAILED = 1,
    DAW_WRAPPER_ERROR_CONFIG_LOAD_FAILED = 2,
    DAW_WRAPPER_ERROR_STATE_SEED_FAILED = 3,
    DAW_WRAPPER_ERROR_SUBSYSTEMS_INIT_FAILED = 4,
    DAW_WRAPPER_ERROR_RUNTIME_START_FAILED = 5,
    DAW_WRAPPER_ERROR_DISPATCH_FAILED = 6,
    DAW_WRAPPER_ERROR_STAGE_TRANSITION_FAILED = 7
} DawWrapperError;

typedef struct DawShutdownSummary {
    uint32_t release_steps;
    bool released_run_loop;
    bool released_runtime;
    bool released_subsystems;
    bool released_state;
    bool released_config;
    bool released_bootstrap;
} DawShutdownSummary;

typedef struct DawAppMainContext {
    DawAppStage stage;
    int exit_code;
    int (*legacy_entry)(void);
    DawDispatchSummary dispatch_summary;
    DawShutdownSummary shutdown_summary;
    DawWrapperError wrapper_error;
    bool bootstrapped;
    bool config_loaded;
    bool state_seeded;
    bool subsystems_initialized;
    bool runtime_started;
    bool run_loop_completed;
    bool shutdown_completed;
} DawAppMainContext;

static int daw_app_default_legacy_entry(void) {
    return 1;
}

static DawAppMainContext g_daw_app_ctx = {
    .stage = DAW_APP_STAGE_INIT,
    .exit_code = 1,
    .wrapper_error = DAW_WRAPPER_ERROR_NONE,
    .legacy_entry = daw_app_default_legacy_entry
};

static void daw_log_wrapper_error(const char *fn_name,
                                  DawWrapperError wrapper_error,
                                  DawAppStage stage,
                                  int exit_code,
                                  const char *detail) {
    fprintf(stderr,
            "daw: wrapper error fn=%s code=%d stage=%d exit_code=%d detail=%s\n",
            fn_name ? fn_name : "unknown",
            (int)wrapper_error,
            (int)stage,
            exit_code,
            detail ? detail : "n/a");
}

// Guards stage progression so the top-level lifecycle remains deterministic.
static bool daw_app_transition_stage(DawAppMainContext *ctx,
                                     DawAppStage expected,
                                     DawAppStage next,
                                     const char *stage_name,
                                     const char *fn_name) {
    if (!ctx) {
        return false;
    }
    if (ctx->stage != expected) {
        fprintf(stderr,
                "daw: stage transition violation fn=%s stage=%s expected=%d actual=%d next=%d\n",
                fn_name ? fn_name : "unknown",
                stage_name ? stage_name : "unknown",
                (int)expected,
                (int)ctx->stage,
                (int)next);
        return false;
    }
    ctx->stage = next;
    return true;
}

static bool daw_app_bootstrap_ctx(DawAppMainContext *ctx) {
    int (*legacy_entry)(void) = daw_app_default_legacy_entry;

    if (!ctx) {
        return false;
    }

    if (ctx->legacy_entry) {
        legacy_entry = ctx->legacy_entry;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->legacy_entry = legacy_entry;
    ctx->exit_code = 1;
    ctx->wrapper_error = DAW_WRAPPER_ERROR_NONE;

    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_INIT,
                                  DAW_APP_STAGE_BOOTSTRAPPED,
                                  "daw_app_bootstrap",
                                  __func__)) {
        return false;
    }
    ctx->bootstrapped = true;
    return true;
}

static bool daw_app_config_load_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_BOOTSTRAPPED,
                                  DAW_APP_STAGE_CONFIG_LOADED,
                                  "daw_app_config_load",
                                  __func__)) {
        return false;
    }
    ctx->config_loaded = true;
    return true;
}

static bool daw_app_state_seed_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_CONFIG_LOADED,
                                  DAW_APP_STAGE_STATE_SEEDED,
                                  "daw_app_state_seed",
                                  __func__)) {
        return false;
    }
    ctx->state_seeded = true;
    return true;
}

static bool daw_app_subsystems_init_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_STATE_SEEDED,
                                  DAW_APP_STAGE_SUBSYSTEMS_READY,
                                  "daw_app_subsystems_init",
                                  __func__)) {
        return false;
    }
    ctx->subsystems_initialized = true;
    return true;
}

static bool daw_runtime_start_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_SUBSYSTEMS_READY,
                                  DAW_APP_STAGE_RUNTIME_STARTED,
                                  "daw_runtime_start",
                                  __func__)) {
        return false;
    }
    ctx->runtime_started = true;
    return true;
}

// CP2 dispatch seam: explicitly route runtime dispatch through one helper.
static int daw_app_dispatch_runtime(DawAppMainContext *ctx, DawDispatchSummary *summary) {
    DawDispatchSummary local = {0};

    if (!ctx || !ctx->legacy_entry) {
        return 1;
    }

    local.dispatch_count = 1u;
    local.dispatch_succeeded = 0;
    local.last_dispatch_exit_code = 1;
    local.used_legacy_entry = true;

    if (summary) {
        *summary = local;
    }

    local.last_dispatch_exit_code = ctx->legacy_entry();
    local.dispatch_succeeded = (local.last_dispatch_exit_code == 0);
    if (summary) {
        *summary = local;
    }
    return local.last_dispatch_exit_code;
}

static int daw_app_run_loop_ctx(DawAppMainContext *ctx) {
    int dispatch_exit = 1;

    if (!ctx) {
        return 1;
    }
    if (ctx->stage != DAW_APP_STAGE_RUNTIME_STARTED) {
        return 1;
    }

    dispatch_exit = daw_app_dispatch_runtime(ctx, &ctx->dispatch_summary);
    if (!daw_app_transition_stage(ctx,
                                  DAW_APP_STAGE_RUNTIME_STARTED,
                                  DAW_APP_STAGE_LOOP_COMPLETED,
                                  "daw_app_run_loop",
                                  __func__)) {
        return 1;
    }
    ctx->exit_code = dispatch_exit;
    ctx->run_loop_completed = true;
    return dispatch_exit;
}

static void daw_app_release_run_loop(DawAppMainContext *ctx) {
    if (!ctx || !ctx->run_loop_completed) {
        return;
    }
    ctx->run_loop_completed = false;
    ctx->shutdown_summary.released_run_loop = true;
    ctx->shutdown_summary.release_steps++;
}

static void daw_app_release_runtime(DawAppMainContext *ctx) {
    if (!ctx || !ctx->runtime_started) {
        return;
    }
    ctx->runtime_started = false;
    ctx->dispatch_summary.dispatch_count = 0u;
    ctx->dispatch_summary.used_legacy_entry = false;
    ctx->shutdown_summary.released_runtime = true;
    ctx->shutdown_summary.release_steps++;
}

static void daw_app_release_subsystems(DawAppMainContext *ctx) {
    if (!ctx || !ctx->subsystems_initialized) {
        return;
    }
    ctx->subsystems_initialized = false;
    ctx->shutdown_summary.released_subsystems = true;
    ctx->shutdown_summary.release_steps++;
}

static void daw_app_release_state(DawAppMainContext *ctx) {
    if (!ctx || !ctx->state_seeded) {
        return;
    }
    ctx->state_seeded = false;
    ctx->shutdown_summary.released_state = true;
    ctx->shutdown_summary.release_steps++;
}

static void daw_app_release_config(DawAppMainContext *ctx) {
    if (!ctx || !ctx->config_loaded) {
        return;
    }
    ctx->config_loaded = false;
    ctx->shutdown_summary.released_config = true;
    ctx->shutdown_summary.release_steps++;
}

static void daw_app_release_bootstrap(DawAppMainContext *ctx) {
    if (!ctx || !ctx->bootstrapped) {
        return;
    }
    ctx->bootstrapped = false;
    ctx->shutdown_summary.released_bootstrap = true;
    ctx->shutdown_summary.release_steps++;
}

// CP4 lifetime hardening: release wrapper-owned lifecycle state in reverse order.
static void daw_app_release_owned_resources(DawAppMainContext *ctx) {
    if (!ctx) {
        return;
    }
    daw_app_release_run_loop(ctx);
    daw_app_release_runtime(ctx);
    daw_app_release_subsystems(ctx);
    daw_app_release_state(ctx);
    daw_app_release_config(ctx);
    daw_app_release_bootstrap(ctx);
}

static void daw_app_shutdown_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->stage == DAW_APP_STAGE_SHUTDOWN_COMPLETED) {
        return;
    }
    daw_app_release_owned_resources(ctx);
    ctx->stage = DAW_APP_STAGE_SHUTDOWN_COMPLETED;
    ctx->shutdown_completed = true;
}

bool daw_app_bootstrap(void) {
    return daw_app_bootstrap_ctx(&g_daw_app_ctx);
}

bool daw_app_config_load(void) {
    return daw_app_config_load_ctx(&g_daw_app_ctx);
}

bool daw_app_state_seed(void) {
    return daw_app_state_seed_ctx(&g_daw_app_ctx);
}

bool daw_app_subsystems_init(void) {
    return daw_app_subsystems_init_ctx(&g_daw_app_ctx);
}

bool daw_runtime_start(void) {
    return daw_runtime_start_ctx(&g_daw_app_ctx);
}

void daw_app_set_legacy_entry(int (*legacy_entry)(void)) {
    if (legacy_entry) {
        g_daw_app_ctx.legacy_entry = legacy_entry;
    }
}

int daw_app_run_loop(void) {
    return daw_app_run_loop_ctx(&g_daw_app_ctx);
}

void daw_app_shutdown(void) {
    daw_app_shutdown_ctx(&g_daw_app_ctx);
}

int daw_app_main_run(void) {
    int exit_code = 1;

    if (!daw_app_bootstrap_ctx(&g_daw_app_ctx)) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_BOOTSTRAP_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              exit_code,
                              "bootstrap failed");
        return exit_code;
    }
    if (!daw_app_config_load_ctx(&g_daw_app_ctx)) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_CONFIG_LOAD_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              g_daw_app_ctx.exit_code,
                              "config load failed");
        goto shutdown;
    }
    if (!daw_app_state_seed_ctx(&g_daw_app_ctx)) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_STATE_SEED_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              g_daw_app_ctx.exit_code,
                              "state seed failed");
        goto shutdown;
    }
    if (!daw_app_subsystems_init_ctx(&g_daw_app_ctx)) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_SUBSYSTEMS_INIT_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              g_daw_app_ctx.exit_code,
                              "subsystems init failed");
        goto shutdown;
    }
    if (!daw_runtime_start_ctx(&g_daw_app_ctx)) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_RUNTIME_START_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              g_daw_app_ctx.exit_code,
                              "runtime start failed");
        goto shutdown;
    }

    exit_code = daw_app_run_loop_ctx(&g_daw_app_ctx);
    if (exit_code != 0) {
        g_daw_app_ctx.wrapper_error = DAW_WRAPPER_ERROR_DISPATCH_FAILED;
        daw_log_wrapper_error(__func__,
                              g_daw_app_ctx.wrapper_error,
                              g_daw_app_ctx.stage,
                              exit_code,
                              "dispatch returned non-zero");
    }
shutdown:
    daw_app_shutdown_ctx(&g_daw_app_ctx);
    fprintf(stderr,
            "daw: wrapper exit stage=%d exit_code=%d dispatch_count=%u dispatch_ok=%d last_dispatch_exit=%d wrapper_error=%d\n",
            (int)g_daw_app_ctx.stage,
            exit_code,
            g_daw_app_ctx.dispatch_summary.dispatch_count,
            g_daw_app_ctx.dispatch_summary.dispatch_succeeded,
            g_daw_app_ctx.dispatch_summary.last_dispatch_exit_code,
            (int)g_daw_app_ctx.wrapper_error);
    return exit_code;
}
