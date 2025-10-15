#pragma once

#include <stdbool.h>

typedef struct {
    int sample_rate;
    int block_size;
} EngineRuntimeConfig;

void config_set_defaults(EngineRuntimeConfig* cfg);
bool config_load_file(const char* path, EngineRuntimeConfig* cfg);
