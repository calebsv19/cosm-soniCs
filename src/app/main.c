#include "app_state.h"
#include "config.h"
#include "daw/daw_app_main.h"
#include "engine/engine.h"
#include "session.h"
#include "sdl_app_framework.h"
#include "input/inspector_input.h"
#include "input/effects_panel_input.h"
#include "audio/wav_writer.h"
#include "export/daw_pack_export.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/library_browser.h"
#include "ui/transport.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"
#include "session/project_manager.h"
#include "time/tempo.h"
#include "core_time.h"
#include "core/loop/daw_mainthread_wake.h"
#include "core/loop/daw_mainthread_timer.h"
#include "core/loop/daw_mainthread_jobs.h"
#include "core/loop/daw_mainthread_messages.h"
#include "core/loop/daw_mainthread_kernel.h"
#include "core/loop/daw_render_invalidation.h"
#include "render/timer_hud_adapter.h"
#include "timer_hud/time_scope.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void handle_render(AppContext* ctx);
static void perform_bounce(AppContext* ctx, AppState* state);

// Stores DAW wake-loop runtime policy loaded from env or defaults.
typedef struct DawLoopRuntimePolicy {
    uint32_t max_wait_ms;
    uint32_t heartbeat_ms;
    uint64_t jobs_budget_ms;
    int message_drain_budget;
    bool diagnostics;
} DawLoopRuntimePolicy;

typedef enum DawGateScenario {
    DAW_GATE_SCENARIO_IDLE = 0,
    DAW_GATE_SCENARIO_PLAYBACK = 1,
    DAW_GATE_SCENARIO_INTERACTION = 2
} DawGateScenario;

typedef struct DawLoopGatePolicy {
    bool enabled;
    DawGateScenario scenario;
    uint32_t min_waits_playback;
    double max_active_pct_idle;
    double min_blocked_pct_idle;
    double max_active_pct_interaction;
} DawLoopGatePolicy;

static DawLoopRuntimePolicy g_loop_policy = {
    .max_wait_ms = 16,
    .heartbeat_ms = 250,
    .jobs_budget_ms = 2,
    .message_drain_budget = 32,
    .diagnostics = false
};
static DawLoopGatePolicy g_gate_policy = {
    .enabled = false,
    .scenario = DAW_GATE_SCENARIO_IDLE,
    .min_waits_playback = 1,
    .max_active_pct_idle = 15.0,
    .min_blocked_pct_idle = 85.0,
    .max_active_pct_interaction = 90.0
};
static uint64_t g_last_render_present_ms = 0;
static bool g_loop_diag_baseline_ready = false;
static DawMainThreadWakeStats g_prev_wake_stats = {0};
static DawMainThreadTimerSchedulerStats g_prev_timer_stats = {0};
static DawMainThreadMessageQueueStats g_prev_msg_stats = {0};
static DawMainThreadJobsStats g_prev_job_stats = {0};
static uint32_t g_last_render_cadence_wait_ms = 0;
static int g_last_dirty_pane_count = 0;

// Parses a uint32 env var and falls back when the value is missing/invalid.
static uint32_t env_u32_or_default(const char* name, uint32_t fallback, uint32_t min_value, uint32_t max_value) {
    const char* value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    char* end = NULL;
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

// Parses a double env var with clamp and default fallback.
static double env_double_or_default(const char* name, double fallback, double min_value, double max_value) {
    const char* value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    char* end = NULL;
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

// Parses a bool-like env var using 1/true/yes/on as true.
static bool env_bool_or_default(const char* name, bool fallback) {
    const char* value = getenv(name);
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

// Parses gate scenario string from env.
static DawGateScenario env_gate_scenario(void) {
    const char* scenario = getenv("DAW_SCENARIO");
    if (!scenario || !scenario[0]) {
        return DAW_GATE_SCENARIO_IDLE;
    }
    if (strcmp(scenario, "playback") == 0) {
        return DAW_GATE_SCENARIO_PLAYBACK;
    }
    if (strcmp(scenario, "interaction") == 0) {
        return DAW_GATE_SCENARIO_INTERACTION;
    }
    return DAW_GATE_SCENARIO_IDLE;
}

// Loads DAW loop runtime policy from env knobs.
static void daw_load_loop_policy_from_env(void) {
    g_loop_policy.max_wait_ms = env_u32_or_default("DAW_LOOP_MAX_WAIT_MS", 16, 1, 250);
    g_loop_policy.heartbeat_ms = env_u32_or_default("DAW_LOOP_HEARTBEAT_MS", 250, 16, 2000);
    g_loop_policy.jobs_budget_ms = env_u32_or_default("DAW_LOOP_JOBS_BUDGET_MS", 2, 1, 50);
    g_loop_policy.message_drain_budget = (int)env_u32_or_default("DAW_LOOP_MESSAGE_BUDGET", 32, 1, 256);
    g_loop_policy.diagnostics = env_bool_or_default("DAW_LOOP_DIAG_LOG", false);
    g_gate_policy.enabled = env_bool_or_default("DAW_LOOP_GATE_EVAL", false);
    g_gate_policy.scenario = env_gate_scenario();
    g_gate_policy.min_waits_playback = env_u32_or_default("DAW_GATE_MIN_WAITS_PLAYBACK", 1, 0, 1000);
    g_gate_policy.max_active_pct_idle = env_double_or_default("DAW_GATE_MAX_ACTIVE_PCT_IDLE", 15.0, 0.0, 100.0);
    g_gate_policy.min_blocked_pct_idle = env_double_or_default("DAW_GATE_MIN_BLOCKED_PCT_IDLE", 85.0, 0.0, 100.0);
    g_gate_policy.max_active_pct_interaction = env_double_or_default("DAW_GATE_MAX_ACTIVE_PCT_INTERACTION", 90.0, 0.0, 100.0);
}

// Returns DAW invalidation reason bits for SDL events that change visible UI state.
static uint32_t invalidation_reason_from_event(const SDL_Event* event) {
    if (!event) {
        return DAW_RENDER_INVALIDATION_NONE;
    }
    switch (event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:
            return DAW_RENDER_INVALIDATION_INPUT | DAW_RENDER_INVALIDATION_CONTENT;
        case SDL_WINDOWEVENT:
            switch (event->window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_EXPOSED:
                    return DAW_RENDER_INVALIDATION_LAYOUT | DAW_RENDER_INVALIDATION_RESIZE;
                default:
                    return DAW_RENDER_INVALIDATION_NONE;
            }
        default:
            return DAW_RENDER_INVALIDATION_NONE;
    }
}

// Marks a minimal set of panes dirty for pointer-driven events to reduce invalidation fanout.
static void invalidate_panes_for_pointer_event(AppState* state, const SDL_Event* event, uint32_t reason_bits) {
    if (!state || !event || reason_bits == DAW_RENDER_INVALIDATION_NONE) {
        return;
    }
    int px = state->mouse_x;
    int py = state->mouse_y;
    if (event->type == SDL_MOUSEMOTION) {
        px = event->motion.x;
        py = event->motion.y;
    } else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
        px = event->button.x;
        py = event->button.y;
    }

    bool any = false;
    Pane* hit = pane_manager_hit_test(&state->pane_manager, px, py);
    if (hit) {
        daw_invalidate_pane(hit, reason_bits);
        any = true;
    }
    if (state->pane_manager.hovered && state->pane_manager.hovered != hit) {
        daw_invalidate_pane(state->pane_manager.hovered, reason_bits);
        any = true;
    }
    if (any) {
        daw_request_full_redraw(reason_bits);
    }
}

// Routes invalidation to panes based on event type instead of always invalidating all panes.
static void invalidate_for_event(AppState* state, const SDL_Event* event, uint32_t reason_bits) {
    if (!state || !event || reason_bits == DAW_RENDER_INVALIDATION_NONE) {
        return;
    }
    switch (event->type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            invalidate_panes_for_pointer_event(state, event, reason_bits);
            return;
        case SDL_WINDOWEVENT:
            daw_invalidate_all(state->panes, state->pane_count, reason_bits);
            daw_request_full_redraw(reason_bits);
            return;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:
        default:
            daw_invalidate_all(state->panes, state->pane_count, reason_bits);
            daw_request_full_redraw(reason_bits);
            return;
    }
}

static void handle_input(AppContext* ctx) {
    if (!ctx || !ctx->userData || !ctx->has_event) {
        return;
    }
    AppState* state = (AppState*)ctx->userData;
    if (state->bounce_active) {
        return;
    }
    input_manager_handle_event(&state->input_manager, state, &ctx->current_event);
    uint32_t reason_bits = invalidation_reason_from_event(&ctx->current_event);
    if (reason_bits != DAW_RENDER_INVALIDATION_NONE) {
        invalidate_for_event(state, &ctx->current_event, reason_bits);
    }
}

// Returns true for urgent work that should bypass blocking this loop cycle.
static bool daw_loop_has_immediate_work(AppContext* ctx) {
    if (!ctx) {
        return false;
    }
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return false;
    }
    if (ctx->pending_swapchain_recreate) {
        return true;
    }
    if (state->bounce_active || state->bounce_requested) {
        return true;
    }
    if (daw_has_frame_invalidation()) {
        return true;
    }
    if (daw_mainthread_message_queue_has_pending()) {
        return true;
    }

    DawMainThreadJobsStats jobs = {0};
    daw_mainthread_jobs_snapshot(&jobs);
    if (jobs.pending > 0) {
        return true;
    }

    Uint32 deadline_ms = 0;
    if (daw_mainthread_timer_scheduler_next_deadline_ms(&deadline_ms)) {
        Uint32 now_ms = SDL_GetTicks();
        int32_t diff = (int32_t)(deadline_ms - now_ms);
        if (diff <= 1) {
            return true;
        }
    }
    return false;
}

// Computes wake-block timeout from timer and render cadence deadlines.
static uint32_t daw_loop_compute_wait_timeout_ms(AppContext* ctx) {
    uint32_t timeout_ms = g_loop_policy.max_wait_ms;
    g_last_render_cadence_wait_ms = 0;
    if (!ctx) {
        return timeout_ms;
    }
    AppState* state = (AppState*)ctx->userData;
    if (state && (state->bounce_active || state->bounce_requested)) {
        return 0;
    }
    if (daw_has_frame_invalidation() || ctx->pending_swapchain_recreate) {
        return 0;
    }

    if (state && state->engine && engine_transport_is_playing(state->engine)) {
        if (ctx->renderMode == RENDER_ALWAYS) {
            return 0;
        }
        if (ctx->timeSinceLastRender < ctx->renderThreshold) {
            const float remaining_s = ctx->renderThreshold - ctx->timeSinceLastRender;
            uint32_t until_render_ms = (uint32_t)(remaining_s * 1000.0f);
            if (until_render_ms == 0) {
                until_render_ms = 1;
            }
            g_last_render_cadence_wait_ms = until_render_ms;
            if (until_render_ms < timeout_ms) {
                timeout_ms = until_render_ms;
            }
        } else {
            return 0;
        }
    }

    Uint32 next_deadline_ms = 0;
    if (daw_mainthread_timer_scheduler_next_deadline_ms(&next_deadline_ms)) {
        Uint32 now_ms = SDL_GetTicks();
        if ((int32_t)(next_deadline_ms - now_ms) <= 0) {
            return 0;
        }
        uint32_t until_timer_ms = (uint32_t)(next_deadline_ms - now_ms);
        if (until_timer_ms < timeout_ms) {
            timeout_ms = until_timer_ms;
        }
    }
    return timeout_ms;
}

// Handles internal wake events and keeps them out of normal input routing.
static bool daw_loop_is_internal_event(AppContext* ctx, const SDL_Event* event) {
    (void)ctx;
    if (!event) {
        return false;
    }
    if (daw_mainthread_wake_is_event(event)) {
        daw_mainthread_wake_note_received();
        return true;
    }
    return false;
}

// Blocks on the DAW wake bridge wait path for up to timeout_ms.
static bool daw_loop_wait_for_event(AppContext* ctx, uint32_t timeout_ms, SDL_Event* out_event) {
    (void)ctx;
    return daw_mainthread_wake_wait_for_event(timeout_ms, out_event) == SDL_TRUE;
}

// Handles one producer/main-thread message and maps it to targeted invalidation.
static bool daw_loop_handle_mainthread_message(AppState* state, const DawMainThreadMessage* msg) {
    if (!state || !msg) {
        return false;
    }
    switch (msg->type) {
        case DAW_MAINTHREAD_MSG_ENGINE_FX_METER:
        case DAW_MAINTHREAD_MSG_ENGINE_FX_SCOPE:
            if (state->pane_count > 2) {
                daw_invalidate_pane(&state->panes[2], DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
            daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            return true;
        case DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT:
            if (state->pane_count > 0) {
                daw_invalidate_pane(&state->panes[0], DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
            if (state->pane_count > 1) {
                daw_invalidate_pane(&state->panes[1], DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
            daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            return true;
        case DAW_MAINTHREAD_MSG_LIBRARY_SCAN_COMPLETE:
            if (state->pane_count > 3) {
                daw_invalidate_pane(&state->panes[3], DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
            daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
            return true;
        case DAW_MAINTHREAD_MSG_USER:
        default:
            daw_request_full_redraw(DAW_RENDER_INVALIDATION_BACKGROUND);
            return true;
    }
}

// Executes one background runtime slice for timers/jobs/messages/kernel.
static void daw_loop_background_tick(AppContext* ctx, uint64_t now_ns) {
    AppState* state = ctx ? (AppState*)ctx->userData : NULL;
    const Uint32 now_ms = SDL_GetTicks();
    const int timers_fired = daw_mainthread_timer_scheduler_fire_due(now_ms);
    const size_t jobs_ran = daw_mainthread_jobs_run_budget_ms(g_loop_policy.jobs_budget_ms);

    DawMainThreadMessage drained[256];
    int max_drain = g_loop_policy.message_drain_budget;
    if (max_drain > (int)(sizeof(drained) / sizeof(drained[0]))) {
        max_drain = (int)(sizeof(drained) / sizeof(drained[0]));
    }
    int messages_drained = daw_mainthread_message_queue_drain(drained, max_drain);
    bool producer_ui_changed = false;
    for (int i = 0; i < messages_drained; ++i) {
        if (daw_loop_handle_mainthread_message(state, &drained[i])) {
            producer_ui_changed = true;
        }
    }

    daw_mainthread_kernel_tick(now_ns);

    if (timers_fired > 0 || jobs_ran > 0 || messages_drained > 0) {
        daw_request_full_redraw(DAW_RENDER_INVALIDATION_BACKGROUND);
    }
    if (producer_ui_changed) {
        daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND);
    }
}

static void handle_update(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    if (state->bounce_requested && !state->bounce_active) {
        perform_bounce(ctx, state);
        return;
    }
    if (state->bounce_active) {
        return;
    }
    if (ui_ensure_layout(state, ctx->window, ctx->renderer)) {
        const uint32_t reason_bits = DAW_RENDER_INVALIDATION_LAYOUT | DAW_RENDER_INVALIDATION_RESIZE;
        daw_invalidate_all(state->panes, state->pane_count, reason_bits);
        daw_request_full_redraw(reason_bits);
    }
    input_manager_update(&state->input_manager, state);
}

// Returns true when a render should occur now.
static bool daw_loop_should_render_now(AppContext* ctx, uint64_t now_ns) {
    (void)now_ns;
    if (!ctx) {
        return false;
    }
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return false;
    }
    if (state->engine && engine_transport_is_playing(state->engine)) {
        if (ctx->renderMode == RENDER_ALWAYS) {
            return true;
        }
        return ctx->timeSinceLastRender >= ctx->renderThreshold;
    }
    if (daw_has_frame_invalidation()) {
        return true;
    }
    const uint64_t now_ms = SDL_GetTicks64();
    if (g_last_render_present_ms == 0) {
        g_last_render_present_ms = now_ms;
    }
    if (now_ms - g_last_render_present_ms >= g_loop_policy.heartbeat_ms) {
        daw_request_full_redraw(DAW_RENDER_INVALIDATION_BACKGROUND);
        return true;
    }
    return false;
}

// Logs loop diagnostics once per diagnostics period.
static void daw_loop_on_diagnostics(AppContext* ctx, const AppLoopDiagnostics* diag) {
    (void)ctx;
    if (!diag) {
        return;
    }

    DawMainThreadWakeStats wake_stats = {0};
    DawMainThreadTimerSchedulerStats timer_stats = {0};
    DawMainThreadMessageQueueStats msg_stats = {0};
    DawMainThreadJobsStats jobs_stats = {0};
    DawMainThreadKernelStats kernel_stats = {0};
    daw_mainthread_wake_snapshot(&wake_stats);
    daw_mainthread_timer_scheduler_snapshot(&timer_stats);
    daw_mainthread_message_queue_snapshot(&msg_stats);
    daw_mainthread_jobs_snapshot(&jobs_stats);
    daw_mainthread_kernel_snapshot(&kernel_stats);

    if (!g_loop_diag_baseline_ready) {
        g_prev_wake_stats = wake_stats;
        g_prev_timer_stats = timer_stats;
        g_prev_msg_stats = msg_stats;
        g_prev_job_stats = jobs_stats;
        g_loop_diag_baseline_ready = true;
        return;
    }

    const double active_ms = core_time_ns_to_seconds(diag->active_ns) * 1000.0;
    const double blocked_ms = core_time_ns_to_seconds(diag->blocked_ns) * 1000.0;
    const double total_ms = active_ms + blocked_ms;
    const double active_pct = total_ms > 0.0 ? (active_ms / total_ms) * 100.0 : 0.0;
    const double blocked_pct = total_ms > 0.0 ? (blocked_ms / total_ms) * 100.0 : 0.0;
    const uint32_t wake_received_delta = wake_stats.received - g_prev_wake_stats.received;
    const uint32_t timers_fired_delta = timer_stats.fired_count - g_prev_timer_stats.fired_count;
    const uint64_t msg_popped_delta = msg_stats.popped - g_prev_msg_stats.popped;
    const uint64_t msg_coalesced_delta = msg_stats.coalesced_drops - g_prev_msg_stats.coalesced_drops;
    const uint64_t msg_wake_push_delta = msg_stats.wake_pushes - g_prev_msg_stats.wake_pushes;
    const uint64_t msg_wake_skip_delta = msg_stats.wake_coalesced_skips - g_prev_msg_stats.wake_coalesced_skips;
    const uint64_t msg_wake_fail_delta = msg_stats.wake_failures - g_prev_msg_stats.wake_failures;
    const uint64_t msg_latency_samples_delta = msg_stats.drain_latency_samples - g_prev_msg_stats.drain_latency_samples;
    const uint64_t msg_latency_total_delta = msg_stats.drain_latency_total_ns - g_prev_msg_stats.drain_latency_total_ns;
    const uint64_t jobs_executed_delta = jobs_stats.executed - g_prev_job_stats.executed;
    const double msg_avg_latency_ms = msg_latency_samples_delta > 0
        ? (core_time_ns_to_seconds(msg_latency_total_delta) * 1000.0) / (double)msg_latency_samples_delta
        : 0.0;
    const double msg_max_latency_ms = core_time_ns_to_seconds(msg_stats.drain_latency_max_ns) * 1000.0;
    AppState* state = (AppState*)ctx->userData;
    g_last_dirty_pane_count = state ? pane_manager_count_dirty(&state->pane_manager) : 0;

    SDL_Log("[LoopDiag] ticks=%u waits=%u renders=%u internal=%u active=%.2fms blocked=%.2fms active_pct=%.1f "
            "wake_rcv=%u timers=%u jobs=%" PRIu64 " msgs=%" PRIu64 " msg_depth=%u msg_peak=%u kernel_work=%" PRIu64
            " render_wait_ms=%u dirty_panes=%d msg_wake=%" PRIu64 " msg_wake_skip=%" PRIu64 " msg_wake_fail=%" PRIu64
            " msg_coalesced=%" PRIu64 " msg_avg_latency=%.3fms msg_max_latency=%.3fms",
            diag->loop_ticks,
            diag->waits_called,
            diag->renders,
            diag->internal_events,
            active_ms,
            blocked_ms,
            active_pct,
            wake_received_delta,
            timers_fired_delta,
            jobs_executed_delta,
            msg_popped_delta,
            msg_stats.depth,
            msg_stats.high_watermark,
            kernel_stats.last_tick_work_units,
            g_last_render_cadence_wait_ms,
            g_last_dirty_pane_count,
            msg_wake_push_delta,
            msg_wake_skip_delta,
            msg_wake_fail_delta,
            msg_coalesced_delta,
            msg_avg_latency_ms,
            msg_max_latency_ms);

    if (g_gate_policy.enabled) {
        const char* scenario_name = "idle";
        bool gate_pass = true;
        const bool headless_mode = (ctx->window == NULL);
        const bool playback_active = state && state->engine && engine_transport_is_playing(state->engine);
        if (g_gate_policy.scenario == DAW_GATE_SCENARIO_PLAYBACK) {
            scenario_name = "playback";
            gate_pass = playback_active &&
                        (diag->waits_called >= g_gate_policy.min_waits_playback) &&
                        (headless_mode || (diag->renders > 0)) &&
                        (headless_mode || (g_last_render_cadence_wait_ms > 0));
            SDL_Log("[LoopGate] scenario=%s pass=%s playback_active=%s waits=%u(min=%u) renders=%u render_wait_ms=%u headless=%s",
                    scenario_name,
                    gate_pass ? "yes" : "no",
                    playback_active ? "yes" : "no",
                    diag->waits_called,
                    g_gate_policy.min_waits_playback,
                    diag->renders,
                    g_last_render_cadence_wait_ms,
                    headless_mode ? "yes" : "no");
        } else if (g_gate_policy.scenario == DAW_GATE_SCENARIO_INTERACTION) {
            scenario_name = "interaction";
            gate_pass = active_pct <= g_gate_policy.max_active_pct_interaction;
            SDL_Log("[LoopGate] scenario=%s pass=%s active_pct=%.2f(max=%.2f)",
                    scenario_name,
                    gate_pass ? "yes" : "no",
                    active_pct,
                    g_gate_policy.max_active_pct_interaction);
        } else {
            scenario_name = "idle";
            gate_pass = (active_pct <= g_gate_policy.max_active_pct_idle) &&
                        (blocked_pct >= g_gate_policy.min_blocked_pct_idle);
            SDL_Log("[LoopGate] scenario=%s pass=%s active_pct=%.2f(max=%.2f) blocked_pct=%.2f(min=%.2f)",
                    scenario_name,
                    gate_pass ? "yes" : "no",
                    active_pct,
                    g_gate_policy.max_active_pct_idle,
                    blocked_pct,
                    g_gate_policy.min_blocked_pct_idle);
        }
    }

    g_prev_wake_stats = wake_stats;
    g_prev_timer_stats = timer_stats;
    g_prev_msg_stats = msg_stats;
    g_prev_job_stats = jobs_stats;
}

static void handle_render(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    SDL_Renderer* renderer = ctx->renderer;

    ui_render_panes(renderer, state);
    ui_render_controls(renderer, state);
    ui_render_overlays(renderer, state);
    ts_render();

    uint64_t rendered_frame_id = 0;
    if (daw_consume_frame_invalidation(NULL, NULL, &rendered_frame_id)) {
        for (int i = 0; i < state->pane_count; ++i) {
            state->panes[i].last_render_frame_id = rendered_frame_id;
            pane_clear_dirty(&state->panes[i]);
        }
    }
    g_last_render_present_ms = SDL_GetTicks64();
}

static bool path_exists(const char* path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static bool next_bounce_path(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const char* dir = "assets/audio";
    for (int i = 0; i < 10000; ++i) {
        char name[64];
        if (i == 0) {
            snprintf(name, sizeof(name), "bounce.wav");
        } else {
            snprintf(name, sizeof(name), "bounce%d.wav", i);
        }
        snprintf(out, len, "%s/%s", dir, name);
        if (!path_exists(out)) {
            return true;
        }
    }
    return false;
}

static uint64_t find_project_end_frame(const Engine* engine) {
    if (!engine) return 0;
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    uint64_t max_end = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track || track->clip_count <= 0) {
            continue;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            const EngineClip* clip = &track->clips[c];
            if (!clip || !clip->active) {
                continue;
            }
            uint64_t len = engine_clip_get_total_frames(engine, t, c);
            uint64_t end = clip->timeline_start_frames + len;
            if (end > max_end) {
                max_end = end;
            }
        }
    }
    return max_end;
}

typedef struct {
    AppState* state;
    AppContext* ctx;
    Uint32 last_render_ms;
    Uint32 render_interval_ms;
} BounceProgressCtx;

static void bounce_progress_cb(uint64_t done_frames, uint64_t total_frames, void* user) {
    BounceProgressCtx* prog = (BounceProgressCtx*)user;
    if (!prog || !prog->state) {
        return;
    }
    prog->state->bounce_progress_frames = done_frames;
    prog->state->bounce_total_frames = total_frames;
    if (prog->ctx && prog->ctx->renderer) {
        Uint32 now = SDL_GetTicks();
        if (now - prog->last_render_ms >= prog->render_interval_ms) {
            prog->last_render_ms = now;
            App_RenderOnce(prog->ctx, handle_render);
        }
    }
}

static void perform_bounce(AppContext* ctx, AppState* state) {
    if (!ctx || !state || !state->engine) {
        return;
    }

    uint64_t start_frame = 0;
    uint64_t end_frame = 0;
    if (state->loop_enabled && state->loop_end_frame > state->loop_start_frame) {
        start_frame = state->loop_start_frame;
        end_frame = state->loop_end_frame;
    } else {
        start_frame = 0;
        end_frame = find_project_end_frame(state->engine);
        if (end_frame == 0) {
            SDL_Log("Bounce aborted: no clips found.");
            state->bounce_requested = false;
            return;
        }
    }

    char path[512];
    if (!next_bounce_path(path, sizeof(path))) {
        SDL_Log("Bounce aborted: unable to allocate output filename.");
        state->bounce_requested = false;
        return;
    }

    state->bounce_active = true;
    state->bounce_progress_frames = 0;
    state->bounce_total_frames = end_frame > start_frame ? end_frame - start_frame : 0;
    state->bounce_start_frame = start_frame;
    state->bounce_end_frame = end_frame;

    SDL_Log("Bounce started: %s", path);
    BounceProgressCtx prog = {
        .state = state,
        .ctx = ctx,
        .last_render_ms = SDL_GetTicks(),
        .render_interval_ms = 50
    };
    EngineBounceBuffer bounce = {0};
    bool ok = engine_bounce_range_to_buffer(state->engine,
                                            start_frame,
                                            end_frame,
                                            bounce_progress_cb,
                                            &prog,
                                            &bounce);
    if (ok) {
        char float_path[512];
        snprintf(float_path, sizeof(float_path), "%s.f32.wav", path);
        bool ok_float = wav_write_f32(float_path,
                                      bounce.data,
                                      bounce.frame_count,
                                      bounce.channels,
                                      bounce.sample_rate);
        uint32_t dither_seed = (uint32_t)(core_time_now_ns() & 0xffffffffu);
        ok = wav_write_pcm16_dithered(path,
                                      bounce.data,
                                      bounce.frame_count,
                                      bounce.channels,
                                      bounce.sample_rate,
                                      dither_seed);
        (void)ok_float;

        if (ok) {
            uint64_t project_duration_frames = find_project_end_frame(state->engine);
            char pack_path[512];
            if (daw_pack_path_from_wav(path, pack_path, sizeof(pack_path))) {
                if (daw_pack_export_from_bounce(pack_path,
                                                state,
                                                &bounce,
                                                start_frame,
                                                end_frame,
                                                project_duration_frames)) {
                    SDL_Log("Bounce pack exported: %s", pack_path);
                } else {
                    SDL_Log("Bounce pack export warning: failed to write %s", pack_path);
                }
            } else {
                SDL_Log("Bounce pack export warning: failed to build pack path for %s", path);
            }
        }
    }
    engine_bounce_buffer_free(&bounce);

    state->bounce_active = false;
    state->bounce_requested = false;
    state->bounce_progress_frames = 0;
    state->bounce_total_frames = 0;
    state->bounce_start_frame = 0;
    state->bounce_end_frame = 0;

    if (ok) {
        library_browser_scan(&state->library, &state->media_registry);
        SDL_Log("Bounce completed: %s", path);
    } else {
        SDL_Log("Bounce failed");
    }
}

int daw_app_main_legacy(void) {
    const int window_width = 1280;
    const int window_height = 720;
    const char* last_session_path = "config/last_session.json";
    const char* fallback_session_path = "config/templates/public_default_project.json";

    AppState state = {0};
    bool loop_wake_initialized = false;
    bool loop_timer_initialized = false;
    bool loop_messages_initialized = false;
    bool loop_jobs_initialized = false;
    bool loop_kernel_initialized = false;
    state.timeline_snap_enabled = true;
    state.timeline_automation_labels_enabled = false;
    state.timeline_tempo_overlay_enabled = false;
    state.tempo_overlay_ui.event_index = -1;
    state.tempo_overlay_ui.dragging = false;
    state.automation_ui.target = ENGINE_AUTOMATION_TARGET_VOLUME;
    state.automation_ui.track_index = -1;
    state.automation_ui.clip_index = -1;
    state.automation_ui.point_index = -1;
    daw_load_loop_policy_from_env();
    state.reset_meter_history_on_seek = true;
    state.inspector.waveform.use_kit_viz_waveform = true;
    waveform_cache_init(&state.waveform_cache);
    undo_manager_init(&state.undo);
    media_registry_init(&state.media_registry, "config/library_index.json");
    media_registry_load(&state.media_registry);
    if (!config_load_file("config/engine.cfg", &state.runtime_cfg)) {
        config_set_defaults(&state.runtime_cfg);
        SDL_Log("Using default audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    } else {
        SDL_Log("Loaded audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    }
    state.tempo = tempo_state_default(state.runtime_cfg.sample_rate);
    tempo_map_init(&state.tempo_map, state.runtime_cfg.sample_rate);
    time_signature_map_init(&state.time_signature_map);
    tempo_map_upsert_event(&state.tempo_map, 0.0, state.tempo.bpm);
    time_signature_map_upsert_event(&state.time_signature_map, 0.0, state.tempo.ts_num, state.tempo.ts_den);

    state.engine_logging_enabled = state.runtime_cfg.enable_engine_logs;
    state.cache_logging_enabled = state.runtime_cfg.enable_cache_logs;
    state.timing_logging_enabled = state.runtime_cfg.enable_timing_logs;

    daw_shared_theme_load_persisted();

    ui_init_panes(&state);
    daw_render_invalidation_init();
    project_manager_init();

    bool loaded_session = false;
    bool engine_started = false;
    if (project_manager_load_last(&state)) {
        SDL_Log("Project restored from last saved project");
        loaded_session = true;
        engine_started = project_manager_post_load(&state);
    } else if (session_load_from_file(&state, "config/last_session.json")) {
        SDL_Log("Session restored from config/last_session.json");
        loaded_session = true;
        engine_started = project_manager_post_load(&state);
    } else if (session_load_from_file(&state, fallback_session_path)) {
        SDL_Log("Fallback session restored from %s", fallback_session_path);
        loaded_session = true;
        engine_started = project_manager_post_load(&state);
        state.project.has_name = false;
        state.project.name[0] = '\0';
        state.project.path[0] = '\0';
    }

    if (!loaded_session) {
        SDL_Log("No previous session found; starting fresh");
        state.engine = engine_create(&state.runtime_cfg);
        if (!state.engine) {
            SDL_Log("Failed to create audio engine");
        }
        library_browser_init(&state.library, "assets/audio");
        library_browser_scan(&state.library, &state.media_registry);
        state.timeline_visible_seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
        state.timeline_window_start_seconds = 0.0f;
        state.timeline_vertical_scale = 1.0f;
        state.timeline_view_in_beats = false;
        state.timeline_show_all_grid_lines = false;
        state.timeline_snap_enabled = true;
        state.timeline_follow_mode = TIMELINE_FOLLOW_JUMP;
        state.loop_enabled = false;
        state.loop_start_frame = 0;
        state.loop_end_frame = state.runtime_cfg.sample_rate > 0 ? (uint64_t)state.runtime_cfg.sample_rate : 48000;
    }

    state.engine_logging_enabled = state.runtime_cfg.enable_engine_logs;
    state.cache_logging_enabled = state.runtime_cfg.enable_cache_logs;
    state.timing_logging_enabled = state.runtime_cfg.enable_timing_logs;

    if (state.engine) {
        engine_set_logging(state.engine,
                           state.engine_logging_enabled,
                           state.cache_logging_enabled,
                           state.timing_logging_enabled);
    }

    AppContext ctx = {0};
    if (!App_Init(&ctx, "Minimal DAW UI", window_width, window_height, true)) {
        if (state.engine) {
            engine_destroy(state.engine);
            state.engine = NULL;
        }
        return 1;
    }

    loop_wake_initialized = daw_mainthread_wake_init();
    if (!loop_wake_initialized) {
        SDL_Log("[LoopAdapters] Warning: wake adapter init failed");
    }
    daw_mainthread_timer_scheduler_init();
    loop_timer_initialized = true;
    daw_mainthread_message_queue_init();
    loop_messages_initialized = true;
    daw_mainthread_jobs_init();
    loop_jobs_initialized = true;
    loop_kernel_initialized = daw_mainthread_kernel_init();
    if (!loop_kernel_initialized) {
        SDL_Log("[LoopAdapters] Warning: kernel adapter init failed");
    }

    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        if (loop_wake_initialized) {
            daw_mainthread_wake_shutdown();
        }
        if (loop_kernel_initialized) {
            daw_mainthread_kernel_shutdown();
        }
        if (loop_timer_initialized) {
            daw_mainthread_timer_scheduler_shutdown();
        }
        if (loop_messages_initialized) {
            daw_mainthread_message_queue_shutdown();
        }
        if (loop_jobs_initialized) {
            daw_mainthread_jobs_shutdown();
        }
        App_Shutdown(&ctx);
        if (state.engine) {
            engine_destroy(state.engine);
            state.engine = NULL;
        }
        return 1;
    }
    {
        char font_path[256];
        int font_point_size = 9;
        if (daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &font_point_size)) {
            ui_font_set(font_path, font_point_size);
        } else {
            ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9);
        }
    }
    timer_hud_register_backend();
    timer_hud_bind_context(&ctx);
    ts_init();

    ui_layout_panes(&state, window_width, window_height);
    pane_manager_init(&state.pane_manager, state.panes, state.pane_count);
    daw_invalidate_all(state.panes, state.pane_count, DAW_RENDER_INVALIDATION_LAYOUT);
    daw_request_full_redraw(DAW_RENDER_INVALIDATION_LAYOUT);
    state.drag_library_index = -1;
    state.dragging_library = false;
    input_manager_init(&state.input_manager);
    if (!loaded_session) {
        state.active_track_index = -1;
        state.selected_track_index = -1;
        state.selected_clip_index = -1;
    }
    state.timeline_drag.active = false;
    state.timeline_drag.trimming_left = false;
    state.timeline_drag.trimming_right = false;
    if (!loaded_session) {
        inspector_input_init(&state);
    }
    effects_panel_input_init(&state);
    state.timeline_drop_track_index = state.active_track_index >= 0 ? state.active_track_index : 0;

    ctx.userData = &state;

    AppCallbacks callbacks = {
        .handleInput = handle_input,
        .handleUpdate = handle_update,
        .handleRender = handle_render,
        .handleBackgroundTick = daw_loop_background_tick,
        .hasImmediateWork = daw_loop_has_immediate_work,
        .computeWaitTimeoutMs = daw_loop_compute_wait_timeout_ms,
        .waitForEvent = daw_loop_wait_for_event,
        .isInternalEvent = daw_loop_is_internal_event,
        .shouldRenderNow = daw_loop_should_render_now,
        .onLoopDiagnostics = daw_loop_on_diagnostics,
    };

    App_SetRenderMode(&ctx, RENDER_THROTTLED, 1.0f / 60.0f);
    AppLoopPolicy loop_policy = {
        .wake_block_enabled = true,
        .heartbeat_ms = g_loop_policy.heartbeat_ms,
        .max_wait_ms = g_loop_policy.max_wait_ms,
    };
    App_SetLoopPolicy(&ctx, loop_policy);
    App_EnableLoopDiagnostics(&ctx, g_loop_policy.diagnostics, 1000);
    g_last_render_present_ms = SDL_GetTicks64();

    if (!engine_started && state.engine) {
        if (!engine_start(state.engine)) {
            SDL_Log("Audio engine failed to start; continuing without audio.");
        } else {
            engine_started = true;
            session_apply_pending_master_fx(&state);
            session_apply_pending_track_fx(&state);
            effects_panel_sync_from_engine(&state);
        }
    }

    if (engine_started) {
        engine_transport_stop(state.engine);
        engine_transport_seek(state.engine, 0);
    }

    App_Run(&ctx, &callbacks);

    daw_shared_theme_save_persisted();

    if (state.project.has_name) {
        project_manager_save(&state, state.project.name, true);
    } else if (!session_save_to_file(&state, last_session_path)) {
        SDL_Log("Failed to save session to %s", last_session_path);
    }

    ui_font_shutdown();
    ts_shutdown();
    waveform_cache_shutdown(&state.waveform_cache);
    undo_manager_free(&state.undo);
    tempo_map_free(&state.tempo_map);
    time_signature_map_free(&state.time_signature_map);
    media_registry_shutdown(&state.media_registry);
    TTF_Quit();
    App_Shutdown(&ctx);
    if (loop_wake_initialized) {
        daw_mainthread_wake_shutdown();
    }
    if (loop_kernel_initialized) {
        daw_mainthread_kernel_shutdown();
    }
    if (loop_timer_initialized) {
        daw_mainthread_timer_scheduler_shutdown();
    }
    if (loop_messages_initialized) {
        daw_mainthread_message_queue_shutdown();
    }
    if (loop_jobs_initialized) {
        daw_mainthread_jobs_shutdown();
    }

    if (state.engine) {
        engine_destroy(state.engine);
        state.engine = NULL;
    }
    return 0;
}

int main(void) {
    daw_app_set_legacy_entry(daw_app_main_legacy);
    return daw_app_main_run();
}
