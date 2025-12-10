#pragma once

#include <stdbool.h>
#include "session.h"

struct AppState;

typedef struct {
    char name[SESSION_NAME_MAX];
    char path[SESSION_PATH_MAX];
    long long modified_ms;
    long long file_size;
    int track_count;
    int clip_count;
    float duration_seconds;
} ProjectInfo;

// Ensure project directory exists and read/write last-used path.
bool project_manager_init(void);
bool project_manager_save(struct AppState* state, const char* name_override, bool overwrite_current);
bool project_manager_load(struct AppState* state, const char* path_optional);
bool project_manager_load_last(struct AppState* state);
bool project_manager_remember_last(const char* path);
bool project_manager_new(struct AppState* state);
bool project_manager_post_load(struct AppState* state);
bool project_manager_list(ProjectInfo* out_items, int max_items, int* out_count);
bool project_manager_get_info(const char* path, ProjectInfo* out_info);
