#pragma once

#include <stdbool.h>

#define CONFIG_FADE_PRESET_MAX 4

// Defines project audio format, output buffering policy, fades, and runtime diagnostics.
typedef struct {
    int sample_rate;
    int block_size;
    int output_queue_blocks; // Requested render-ahead target, 2..32 DSP blocks; zero uses compatibility default.
    float default_fade_in_ms;
    float default_fade_out_ms;
    int fade_preset_count;
    float fade_preset_ms[CONFIG_FADE_PRESET_MAX];
    bool enable_engine_logs;
    bool enable_cache_logs;
    bool enable_timing_logs;
} EngineRuntimeConfig;

void config_set_defaults(EngineRuntimeConfig* cfg);
bool config_load_file(const char* path, EngineRuntimeConfig* cfg);
