#include "app/main_loop_policy.h"

#include <stdlib.h>
#include <string.h>

static uint32_t env_u32_or_default(const char *name,
                                   uint32_t fallback,
                                   uint32_t min_value,
                                   uint32_t max_value) {
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (!end || end == value || *end != '\0') {
        return fallback;
    }
    if (parsed < min_value) {
        parsed = min_value;
    }
    if (parsed > max_value) {
        parsed = max_value;
    }
    return (uint32_t)parsed;
}

static double env_double_or_default(const char *name,
                                    double fallback,
                                    double min_value,
                                    double max_value) {
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (!end || end == value || *end != '\0') {
        return fallback;
    }
    if (parsed < min_value) {
        parsed = min_value;
    }
    if (parsed > max_value) {
        parsed = max_value;
    }
    return parsed;
}

static bool env_bool_or_default(const char *name, bool fallback) {
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    if (strcmp(value, "1") == 0 ||
        strcmp(value, "true") == 0 ||
        strcmp(value, "TRUE") == 0 ||
        strcmp(value, "yes") == 0 ||
        strcmp(value, "on") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 ||
        strcmp(value, "FALSE") == 0 ||
        strcmp(value, "no") == 0 ||
        strcmp(value, "off") == 0) {
        return false;
    }
    return fallback;
}

static DawGateScenario env_gate_scenario(DawGateScenario fallback) {
    const char *scenario = getenv("DAW_SCENARIO");
    if (!scenario || !scenario[0]) {
        return fallback;
    }
    if (strcmp(scenario, "playback") == 0) {
        return DAW_GATE_SCENARIO_PLAYBACK;
    }
    if (strcmp(scenario, "interaction") == 0) {
        return DAW_GATE_SCENARIO_INTERACTION;
    }
    return DAW_GATE_SCENARIO_IDLE;
}

void daw_load_loop_policy_from_env(DawLoopRuntimePolicy *loop_policy,
                                   DawLoopGatePolicy *gate_policy) {
    const char *loop_diag_format = NULL;
    if (!loop_policy || !gate_policy) {
        return;
    }

    loop_policy->max_wait_ms = env_u32_or_default(
        "DAW_LOOP_MAX_WAIT_MS", loop_policy->max_wait_ms, 1, 250);
    loop_policy->heartbeat_ms = env_u32_or_default(
        "DAW_LOOP_HEARTBEAT_MS", loop_policy->heartbeat_ms, 16, 2000);
    loop_policy->jobs_budget_ms = env_u32_or_default(
        "DAW_LOOP_JOBS_BUDGET_MS", (uint32_t)loop_policy->jobs_budget_ms, 1, 50);
    loop_policy->message_drain_budget = (int)env_u32_or_default(
        "DAW_LOOP_MESSAGE_BUDGET", (uint32_t)loop_policy->message_drain_budget, 1, 256);
    loop_policy->diagnostics = env_bool_or_default(
        "DAW_LOOP_DIAG_LOG", loop_policy->diagnostics);
    loop_policy->diagnostics_json = env_bool_or_default(
        "DAW_LOOP_DIAG_JSON", loop_policy->diagnostics_json);
    loop_diag_format = getenv("DAW_LOOP_DIAG_FORMAT");
    if (loop_diag_format && loop_diag_format[0] &&
        strcmp(loop_diag_format, "json") == 0) {
        loop_policy->diagnostics_json = true;
    }
    if (loop_policy->diagnostics_json) {
        loop_policy->diagnostics = true;
    }

    gate_policy->enabled = env_bool_or_default(
        "DAW_LOOP_GATE_EVAL", gate_policy->enabled);
    gate_policy->scenario = env_gate_scenario(gate_policy->scenario);
    gate_policy->min_waits_playback = env_u32_or_default(
        "DAW_GATE_MIN_WAITS_PLAYBACK", gate_policy->min_waits_playback, 0, 1000);
    gate_policy->max_active_pct_idle = env_double_or_default(
        "DAW_GATE_MAX_ACTIVE_PCT_IDLE", gate_policy->max_active_pct_idle, 0.0, 100.0);
    gate_policy->min_blocked_pct_idle = env_double_or_default(
        "DAW_GATE_MIN_BLOCKED_PCT_IDLE", gate_policy->min_blocked_pct_idle, 0.0, 100.0);
    gate_policy->max_active_pct_interaction = env_double_or_default(
        "DAW_GATE_MAX_ACTIVE_PCT_INTERACTION", gate_policy->max_active_pct_interaction, 0.0, 100.0);
}
