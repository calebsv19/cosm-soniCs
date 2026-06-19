#include "app/main_loop_policy.h"
#include "config.h"

#include <SDL2/SDL.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct CapturedLogs {
    char text[8192];
} CapturedLogs;

static void capture_log(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)category;
    (void)priority;
    CapturedLogs* logs = (CapturedLogs*)userdata;
    if (!logs || !message) {
        return;
    }
    size_t used = strlen(logs->text);
    size_t remaining = sizeof(logs->text) - used;
    if (remaining <= 1u) {
        return;
    }
    snprintf(logs->text + used, remaining, "%s\n", message);
}

static bool logs_contain(const CapturedLogs* logs, const char* needle) {
    return logs && needle && strstr(logs->text, needle) != NULL;
}

static void write_config_file(const char* path) {
    FILE* file = fopen(path, "wb");
    assert(file);
    fputs("sample_rate=0\n", file);
    fputs("block_size=256\n", file);
    fputs("fade_default_in_ms=-5\n", file);
    fputs("fade_default_out_ms=abc\n", file);
    fputs("fade_presets_ms=1, -2, nope\n", file);
    fputs("enable_engine_logs=maybe\n", file);
    fputs("unknown_key=42\n", file);
    fputs("malformed line\n", file);
    assert(fclose(file) == 0);
}

static void test_config_diagnostics(void) {
    CapturedLogs logs = {0};
    SDL_LogOutputFunction old_fn = NULL;
    void* old_userdata = NULL;
    char template_path[] = "/tmp/daw_config_diag_XXXXXX";
    int fd = mkstemp(template_path);
    EngineRuntimeConfig cfg;

    assert(fd >= 0);
    close(fd);
    write_config_file(template_path);

    SDL_LogGetOutputFunction(&old_fn, &old_userdata);
    SDL_LogSetOutputFunction(capture_log, &logs);
    assert(config_load_file(template_path, &cfg));
    SDL_LogSetOutputFunction(old_fn, old_userdata);

    assert(cfg.sample_rate == 48000);
    assert(cfg.block_size == 256);
    assert(cfg.default_fade_in_ms == 0.0f);
    assert(cfg.default_fade_out_ms == 0.0f);
    assert(cfg.fade_preset_count == 2);
    assert(cfg.fade_preset_ms[0] == 1.0f);
    assert(cfg.fade_preset_ms[1] == 0.0f);
    assert(!cfg.enable_engine_logs);

    assert(logs_contain(&logs, "config_load_file:"));
    assert(logs_contain(&logs, "key=sample_rate"));
    assert(logs_contain(&logs, "ignored invalid positive integer"));
    assert(logs_contain(&logs, "key=fade_default_in_ms"));
    assert(logs_contain(&logs, "clamped negative value to 0.0"));
    assert(logs_contain(&logs, "key=fade_default_out_ms"));
    assert(logs_contain(&logs, "ignored invalid float"));
    assert(logs_contain(&logs, "key=fade_presets_ms"));
    assert(logs_contain(&logs, "stopped parsing at invalid fade preset token"));
    assert(logs_contain(&logs, "key=enable_engine_logs"));
    assert(logs_contain(&logs, "ignored invalid boolean"));
    assert(logs_contain(&logs, "key=unknown_key"));
    assert(logs_contain(&logs, "ignored unknown key"));
    assert(logs_contain(&logs, "ignored malformed line without '='"));

    remove(template_path);
}

static void clear_loop_env(void) {
    unsetenv("DAW_LOOP_MAX_WAIT_MS");
    unsetenv("DAW_LOOP_HEARTBEAT_MS");
    unsetenv("DAW_LOOP_JOBS_BUDGET_MS");
    unsetenv("DAW_LOOP_MESSAGE_BUDGET");
    unsetenv("DAW_LOOP_DIAG_LOG");
    unsetenv("DAW_LOOP_DIAG_JSON");
    unsetenv("DAW_LOOP_DIAG_FORMAT");
    unsetenv("DAW_LOOP_GATE_EVAL");
    unsetenv("DAW_SCENARIO");
    unsetenv("DAW_GATE_MIN_WAITS_PLAYBACK");
    unsetenv("DAW_GATE_MAX_ACTIVE_PCT_IDLE");
    unsetenv("DAW_GATE_MIN_BLOCKED_PCT_IDLE");
    unsetenv("DAW_GATE_MAX_ACTIVE_PCT_INTERACTION");
}

static void test_loop_policy_diagnostics(void) {
    CapturedLogs logs = {0};
    SDL_LogOutputFunction old_fn = NULL;
    void* old_userdata = NULL;
    DawLoopRuntimePolicy loop_policy = {
        .max_wait_ms = 5,
        .heartbeat_ms = 100,
        .jobs_budget_ms = 3,
        .message_drain_budget = 8,
        .diagnostics = false,
        .diagnostics_json = false,
    };
    DawLoopGatePolicy gate_policy = {
        .enabled = false,
        .scenario = DAW_GATE_SCENARIO_PLAYBACK,
        .min_waits_playback = 4,
        .max_active_pct_idle = 40.0,
        .min_blocked_pct_idle = 60.0,
        .max_active_pct_interaction = 80.0,
    };

    clear_loop_env();
    setenv("DAW_LOOP_MAX_WAIT_MS", "oops", 1);
    setenv("DAW_LOOP_HEARTBEAT_MS", "4", 1);
    setenv("DAW_LOOP_JOBS_BUDGET_MS", "999", 1);
    setenv("DAW_LOOP_MESSAGE_BUDGET", "bad", 1);
    setenv("DAW_LOOP_DIAG_LOG", "maybe", 1);
    setenv("DAW_LOOP_DIAG_FORMAT", "text", 1);
    setenv("DAW_LOOP_GATE_EVAL", "sure", 1);
    setenv("DAW_SCENARIO", "recording", 1);
    setenv("DAW_GATE_MAX_ACTIVE_PCT_IDLE", "200", 1);
    setenv("DAW_GATE_MIN_BLOCKED_PCT_IDLE", "nanx", 1);

    SDL_LogGetOutputFunction(&old_fn, &old_userdata);
    SDL_LogSetOutputFunction(capture_log, &logs);
    daw_load_loop_policy_from_env(&loop_policy, &gate_policy);
    SDL_LogSetOutputFunction(old_fn, old_userdata);
    clear_loop_env();

    assert(loop_policy.max_wait_ms == 5);
    assert(loop_policy.heartbeat_ms == 16);
    assert(loop_policy.jobs_budget_ms == 50);
    assert(loop_policy.message_drain_budget == 8);
    assert(!loop_policy.diagnostics);
    assert(!gate_policy.enabled);
    assert(gate_policy.scenario == DAW_GATE_SCENARIO_IDLE);
    assert(gate_policy.max_active_pct_idle == 100.0);
    assert(gate_policy.min_blocked_pct_idle == 60.0);

    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_MAX_WAIT_MS=oops ignored invalid unsigned integer"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_HEARTBEAT_MS=4 clamped below minimum"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_JOBS_BUDGET_MS=999 clamped above maximum"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_MESSAGE_BUDGET=bad ignored invalid unsigned integer"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_DIAG_LOG=maybe ignored invalid boolean"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_DIAG_FORMAT=text ignored unsupported format"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_LOOP_GATE_EVAL=sure ignored invalid boolean"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_SCENARIO=recording ignored unknown scenario"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_GATE_MAX_ACTIVE_PCT_IDLE=200 clamped above maximum"));
    assert(logs_contain(&logs, "main_loop_policy: env DAW_GATE_MIN_BLOCKED_PCT_IDLE=nanx ignored invalid floating point value"));
}

int main(void) {
    test_config_diagnostics();
    test_loop_policy_diagnostics();
    return 0;
}
