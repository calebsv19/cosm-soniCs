#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "session.h"

#define MEDIA_ID_MAX 33
#define MEDIA_NAME_MAX 128

typedef struct {
    char id[MEDIA_ID_MAX];
    char path[SESSION_PATH_MAX];
    char name[MEDIA_NAME_MAX];
    float duration_seconds;
    int sample_rate;
    int channels;
    uint64_t file_size;
} MediaRegistryEntry;

typedef struct {
    MediaRegistryEntry* entries;
    int count;
    int capacity;
    bool dirty;
    char manifest_path[SESSION_PATH_MAX];
} MediaRegistry;

void media_registry_init(MediaRegistry* registry, const char* manifest_path);
void media_registry_shutdown(MediaRegistry* registry);
bool media_registry_load(MediaRegistry* registry);
bool media_registry_save(MediaRegistry* registry);

const MediaRegistryEntry* media_registry_find_by_id(const MediaRegistry* registry, const char* id);
MediaRegistryEntry* media_registry_find_by_path(MediaRegistry* registry, const char* path);

bool media_registry_ensure_for_path(MediaRegistry* registry,
                                    const char* path,
                                    const char* display_name,
                                    MediaRegistryEntry* out_entry);
bool media_registry_update_path(MediaRegistry* registry,
                                const char* id,
                                const char* new_path,
                                const char* new_name);
