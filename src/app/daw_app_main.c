#include "daw/daw_app_main.h"

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
    bool used_legacy_entry;
} DawDispatchSummary;

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
    .legacy_entry = daw_app_default_legacy_entry
};

// Guards stage progression so the top-level lifecycle remains deterministic.
static bool daw_app_transition_stage(DawAppMainContext *ctx,
                                     DawAppStage expected,
                                     DawAppStage next) {
    if (!ctx || ctx->stage != expected) {
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

    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_INIT, DAW_APP_STAGE_BOOTSTRAPPED)) {
        return false;
    }
    ctx->bootstrapped = true;
    return true;
}

static bool daw_app_config_load_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_BOOTSTRAPPED, DAW_APP_STAGE_CONFIG_LOADED)) {
        return false;
    }
    ctx->config_loaded = true;
    return true;
}

static bool daw_app_state_seed_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_CONFIG_LOADED, DAW_APP_STAGE_STATE_SEEDED)) {
        return false;
    }
    ctx->state_seeded = true;
    return true;
}

static bool daw_app_subsystems_init_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_STATE_SEEDED, DAW_APP_STAGE_SUBSYSTEMS_READY)) {
        return false;
    }
    ctx->subsystems_initialized = true;
    return true;
}

static bool daw_runtime_start_ctx(DawAppMainContext *ctx) {
    if (!ctx) {
        return false;
    }
    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_SUBSYSTEMS_READY, DAW_APP_STAGE_RUNTIME_STARTED)) {
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
    local.used_legacy_entry = true;

    if (summary) {
        *summary = local;
    }

    return ctx->legacy_entry();
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
    if (!daw_app_transition_stage(ctx, DAW_APP_STAGE_RUNTIME_STARTED, DAW_APP_STAGE_LOOP_COMPLETED)) {
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
        return exit_code;
    }
    if (!daw_app_config_load_ctx(&g_daw_app_ctx)) {
        goto shutdown;
    }
    if (!daw_app_state_seed_ctx(&g_daw_app_ctx)) {
        goto shutdown;
    }
    if (!daw_app_subsystems_init_ctx(&g_daw_app_ctx)) {
        goto shutdown;
    }
    if (!daw_runtime_start_ctx(&g_daw_app_ctx)) {
        goto shutdown;
    }

    exit_code = daw_app_run_loop_ctx(&g_daw_app_ctx);
shutdown:
    daw_app_shutdown_ctx(&g_daw_app_ctx);
    return exit_code;
}
