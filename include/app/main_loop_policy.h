#ifndef DAW_MAIN_LOOP_POLICY_H
#define DAW_MAIN_LOOP_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum DawGateScenario {
    DAW_GATE_SCENARIO_IDLE = 0,
    DAW_GATE_SCENARIO_PLAYBACK = 1,
    DAW_GATE_SCENARIO_INTERACTION = 2
} DawGateScenario;

typedef struct DawLoopRuntimePolicy {
    uint32_t max_wait_ms;
    uint32_t heartbeat_ms;
    uint64_t jobs_budget_ms;
    int message_drain_budget;
    bool diagnostics;
    bool diagnostics_json;
} DawLoopRuntimePolicy;

typedef struct DawLoopGatePolicy {
    bool enabled;
    DawGateScenario scenario;
    uint32_t min_waits_playback;
    double max_active_pct_idle;
    double min_blocked_pct_idle;
    double max_active_pct_interaction;
} DawLoopGatePolicy;

void daw_load_loop_policy_from_env(DawLoopRuntimePolicy *loop_policy,
                                   DawLoopGatePolicy *gate_policy);

#endif
