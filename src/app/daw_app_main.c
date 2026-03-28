#include "daw/daw_app_main.h"

#include <string.h>

typedef struct DawAppLifecycleState {
    bool bootstrapped;
    bool config_loaded;
    bool state_seeded;
    bool subsystems_initialized;
    bool runtime_started;
    bool run_loop_completed;
    bool shutdown_completed;
    int exit_code;
} DawAppLifecycleState;

static DawAppLifecycleState g_daw_app_lifecycle = {0};

static int daw_app_default_legacy_entry(void) {
    return 1;
}

static int (*g_daw_app_legacy_entry)(void) = daw_app_default_legacy_entry;

bool daw_app_bootstrap(void) {
    memset(&g_daw_app_lifecycle, 0, sizeof(g_daw_app_lifecycle));
    g_daw_app_lifecycle.bootstrapped = true;
    return true;
}

bool daw_app_config_load(void) {
    if (!g_daw_app_lifecycle.bootstrapped) {
        return false;
    }
    g_daw_app_lifecycle.config_loaded = true;
    return true;
}

bool daw_app_state_seed(void) {
    if (!g_daw_app_lifecycle.config_loaded) {
        return false;
    }
    g_daw_app_lifecycle.state_seeded = true;
    return true;
}

bool daw_app_subsystems_init(void) {
    if (!g_daw_app_lifecycle.state_seeded) {
        return false;
    }
    g_daw_app_lifecycle.subsystems_initialized = true;
    return true;
}

bool daw_runtime_start(void) {
    if (!g_daw_app_lifecycle.subsystems_initialized) {
        return false;
    }
    g_daw_app_lifecycle.runtime_started = true;
    return true;
}

void daw_app_set_legacy_entry(int (*legacy_entry)(void)) {
    if (legacy_entry) {
        g_daw_app_legacy_entry = legacy_entry;
    }
}

int daw_app_run_loop(void) {
    if (!g_daw_app_lifecycle.runtime_started) {
        return 1;
    }
    g_daw_app_lifecycle.exit_code = g_daw_app_legacy_entry();
    g_daw_app_lifecycle.run_loop_completed = true;
    return g_daw_app_lifecycle.exit_code;
}

void daw_app_shutdown(void) {
    if (!g_daw_app_lifecycle.bootstrapped) {
        return;
    }
    g_daw_app_lifecycle.shutdown_completed = true;
}

int daw_app_main_run(void) {
    int exit_code = 1;
    if (!daw_app_bootstrap()) {
        return exit_code;
    }
    if (!daw_app_config_load()) {
        daw_app_shutdown();
        return exit_code;
    }
    if (!daw_app_state_seed()) {
        daw_app_shutdown();
        return exit_code;
    }
    if (!daw_app_subsystems_init()) {
        daw_app_shutdown();
        return exit_code;
    }
    if (!daw_runtime_start()) {
        daw_app_shutdown();
        return exit_code;
    }
    exit_code = daw_app_run_loop();
    daw_app_shutdown();
    return exit_code;
}
