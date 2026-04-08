#pragma once

#include "session.h"

#include <stdbool.h>

#define DAW_DATA_PATH_PRODUCT_NAME "DAW"
#define DAW_DATA_PATH_DEFAULT_INPUT_ROOT "assets/audio"
#define DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT "config"
#define DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT "assets/audio"
#define DAW_DATA_PATH_RUNTIME_CONFIG_PATH "config/runtime/data_paths.cfg"

typedef struct DawDataPaths {
    char input_root[SESSION_PATH_MAX];
    char output_root[SESSION_PATH_MAX];
    char library_copy_root[SESSION_PATH_MAX];
} DawDataPaths;

void daw_data_paths_set_defaults(DawDataPaths* paths);
const char* daw_data_paths_library_root(const DawDataPaths* paths);
bool daw_data_paths_valid(const DawDataPaths* paths);
void daw_data_paths_apply_runtime_policy(DawDataPaths* paths);
bool daw_data_paths_load_file(const char* path, DawDataPaths* out_paths);
bool daw_data_paths_save_file(const char* path, const DawDataPaths* paths);
bool daw_data_paths_load_runtime(DawDataPaths* out_paths);
bool daw_data_paths_save_runtime(const DawDataPaths* paths);
