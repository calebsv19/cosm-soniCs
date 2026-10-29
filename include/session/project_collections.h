#pragma once
#include "session/project_manager.h"
struct AppState;
#define PROJECT_COLLECTION_MAX 66
// Names one explicit project directory without merging neighboring collections.
typedef struct ProjectCollection { char label[SESSION_NAME_MAX]; char directory[SESSION_PATH_MAX]; } ProjectCollection;
// Holds browsing scope independently of the live song, data roots and startup markers.
typedef struct ProjectCollections {
    bool initialized;
    char root[SESSION_PATH_MAX];
    ProjectCollection items[PROJECT_COLLECTION_MAX];
    int count, selected;
    unsigned revision;
} ProjectCollections;
// Refreshes preferred, legacy and existing collection folders while retaining a chosen scope.
bool project_collections_refresh(struct AppState* state, const char* root);
// Selects a scope without loading, saving or replacing the active song.
bool project_collections_select(struct AppState* state, int index);
// Lists only regular JSON project files in the selected directory, sorted by name.
bool project_collections_list(struct AppState* state, ProjectInfo* items, int capacity, int* count);
// Computes the next monotonically numbered checkpoint without overwriting previous versions.
bool project_collections_next_path(struct AppState* state, const char* name, char* path, size_t size);
// Saves a validated new checkpoint atomically and advances identity only after publication.
bool project_collections_save_version(struct AppState* state, const char* name);
