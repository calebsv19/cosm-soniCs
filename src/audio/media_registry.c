#include "audio/media_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_REGISTRY_MAGIC "media_registry"

static uint64_t fnv1a64_stream(FILE* fp) {
    const uint64_t prime = 1099511628211ULL;
    uint64_t hash = 1469598103934665603ULL;
    unsigned char buffer[4096];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        for (size_t i = 0; i < read; ++i) {
            hash ^= (uint64_t)buffer[i];
            hash *= prime;
        }
    }
    return hash;
}

static bool media_registry_hash_file(const char* path, char* out_id, size_t out_len, uint64_t* out_size) {
    if (!path || !out_id || out_len == 0) {
        return false;
    }
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) size = 0;
    fseek(fp, 0, SEEK_SET);
    uint64_t hash = fnv1a64_stream(fp);
    fclose(fp);
    snprintf(out_id, out_len, "%016llx", (unsigned long long)hash);
    if (out_size) {
        *out_size = (uint64_t)size;
    }
    return true;
}

static bool registry_ensure_capacity(MediaRegistry* registry, int count_needed) {
    if (!registry) return false;
    if (registry->capacity >= count_needed) return true;
    int next = registry->capacity > 0 ? registry->capacity : 32;
    while (next < count_needed) next *= 2;
    MediaRegistryEntry* resized = (MediaRegistryEntry*)realloc(registry->entries, sizeof(MediaRegistryEntry) * (size_t)next);
    if (!resized) return false;
    registry->entries = resized;
    registry->capacity = next;
    return true;
}

static void registry_write_json(FILE* file, const MediaRegistry* registry) {
    fprintf(file, "{\n");
    fprintf(file, "  \"type\": \"%s\",\n", MEDIA_REGISTRY_MAGIC);
    fprintf(file, "  \"entries\": [\n");
    for (int i = 0; i < registry->count; ++i) {
        const MediaRegistryEntry* entry = &registry->entries[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"id\": \"%s\",\n", entry->id);
        fprintf(file, "      \"path\": \"%s\",\n", entry->path);
        fprintf(file, "      \"name\": \"%s\",\n", entry->name);
        fprintf(file, "      \"duration\": %.6f,\n", entry->duration_seconds);
        fprintf(file, "      \"sample_rate\": %d,\n", entry->sample_rate);
        fprintf(file, "      \"channels\": %d,\n", entry->channels);
        fprintf(file, "      \"file_size\": %llu\n", (unsigned long long)entry->file_size);
        fprintf(file, "    }%s\n", (i + 1 < registry->count) ? "," : "");
    }
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
}

static void json_skip_ws(const char* data, size_t len, size_t* pos) {
    while (*pos < len) {
        char ch = data[*pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            (*pos)++;
        } else {
            break;
        }
    }
}

static bool json_match(const char* data, size_t len, size_t* pos, char expected) {
    json_skip_ws(data, len, pos);
    if (*pos >= len || data[*pos] != expected) {
        return false;
    }
    (*pos)++;
    return true;
}

static bool json_parse_string_value(const char* data, size_t len, size_t* pos, char* dst, size_t dst_len) {
    if (!json_match(data, len, pos, '"')) {
        return false;
    }
    size_t out = 0;
    while (*pos < len) {
        char ch = data[(*pos)++];
        if (ch == '"') {
            break;
        }
        if (ch == '\\' && *pos < len) {
            ch = data[(*pos)++];
            if (ch == '"' || ch == '\\' || ch == '/') {
                // ok
            } else if (ch == 'n') {
                ch = '\n';
            } else if (ch == 't') {
                ch = '\t';
            } else if (ch == 'r') {
                ch = '\r';
            }
        }
        if (dst && out + 1 < dst_len) {
            dst[out++] = ch;
        }
    }
    if (dst && dst_len > 0) {
        dst[out] = '\0';
    }
    return true;
}

static bool json_parse_number_value(const char* data, size_t len, size_t* pos, double* out_value) {
    json_skip_ws(data, len, pos);
    size_t start = *pos;
    if (start >= len) return false;
    if (data[*pos] == '-' || data[*pos] == '+') (*pos)++;
    bool has_digits = false;
    while (*pos < len && data[*pos] >= '0' && data[*pos] <= '9') {
        has_digits = true;
        (*pos)++;
    }
    if (*pos < len && data[*pos] == '.') {
        (*pos)++;
        while (*pos < len && data[*pos] >= '0' && data[*pos] <= '9') {
            has_digits = true;
            (*pos)++;
        }
    }
    if (!has_digits) return false;
    char temp[64];
    size_t span = (*pos) - start;
    if (span >= sizeof(temp)) span = sizeof(temp) - 1;
    memcpy(temp, data + start, span);
    temp[span] = '\0';
    if (out_value) {
        *out_value = strtod(temp, NULL);
    }
    return true;
}

static bool registry_parse_json(MediaRegistry* registry, const char* data, size_t len) {
    size_t pos = 0;
    if (!json_match(data, len, &pos, '{')) {
        return false;
    }
    while (pos < len) {
        json_skip_ws(data, len, &pos);
        if (pos < len && data[pos] == '}') {
            pos++;
            break;
        }
        char key[64] = {0};
        if (!json_parse_string_value(data, len, &pos, key, sizeof(key))) {
            return false;
        }
        if (!json_match(data, len, &pos, ':')) {
            return false;
        }
        if (strcmp(key, "entries") == 0) {
            if (!json_match(data, len, &pos, '[')) {
                return false;
            }
            json_skip_ws(data, len, &pos);
            while (pos < len && data[pos] != ']') {
                if (!json_match(data, len, &pos, '{')) {
                    return false;
                }
                MediaRegistryEntry entry = {0};
                while (pos < len) {
                    json_skip_ws(data, len, &pos);
                    if (pos < len && data[pos] == '}') {
                        pos++;
                        break;
                    }
                    char ekey[64] = {0};
                    if (!json_parse_string_value(data, len, &pos, ekey, sizeof(ekey))) {
                        return false;
                    }
                    if (!json_match(data, len, &pos, ':')) {
                        return false;
                    }
                    if (strcmp(ekey, "id") == 0) {
                        if (!json_parse_string_value(data, len, &pos, entry.id, sizeof(entry.id))) return false;
                    } else if (strcmp(ekey, "path") == 0) {
                        if (!json_parse_string_value(data, len, &pos, entry.path, sizeof(entry.path))) return false;
                    } else if (strcmp(ekey, "name") == 0) {
                        if (!json_parse_string_value(data, len, &pos, entry.name, sizeof(entry.name))) return false;
                    } else if (strcmp(ekey, "duration") == 0) {
                        double v = 0.0;
                        if (!json_parse_number_value(data, len, &pos, &v)) return false;
                        entry.duration_seconds = (float)v;
                    } else if (strcmp(ekey, "sample_rate") == 0) {
                        double v = 0.0;
                        if (!json_parse_number_value(data, len, &pos, &v)) return false;
                        entry.sample_rate = (int)v;
                    } else if (strcmp(ekey, "channels") == 0) {
                        double v = 0.0;
                        if (!json_parse_number_value(data, len, &pos, &v)) return false;
                        entry.channels = (int)v;
                    } else if (strcmp(ekey, "file_size") == 0) {
                        double v = 0.0;
                        if (!json_parse_number_value(data, len, &pos, &v)) return false;
                        entry.file_size = (uint64_t)v;
                    } else {
                        if (!json_parse_string_value(data, len, &pos, NULL, 0)) {
                            double v = 0.0;
                            json_parse_number_value(data, len, &pos, &v);
                        }
                    }
                    json_skip_ws(data, len, &pos);
                    if (pos < len && data[pos] == ',') {
                        pos++;
                        continue;
                    }
                }
                if (!registry_ensure_capacity(registry, registry->count + 1)) {
                    return false;
                }
                registry->entries[registry->count++] = entry;
                json_skip_ws(data, len, &pos);
                if (pos < len && data[pos] == ',') {
                    pos++;
                    json_skip_ws(data, len, &pos);
                }
            }
            if (!json_match(data, len, &pos, ']')) {
                return false;
            }
        } else {
            if (!json_parse_string_value(data, len, &pos, NULL, 0)) {
                double v = 0.0;
                json_parse_number_value(data, len, &pos, &v);
            }
        }
        json_skip_ws(data, len, &pos);
        if (pos < len && data[pos] == ',') {
            pos++;
            continue;
        }
    }
    return true;
}

void media_registry_init(MediaRegistry* registry, const char* manifest_path) {
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
    if (manifest_path) {
        strncpy(registry->manifest_path, manifest_path, sizeof(registry->manifest_path) - 1);
        registry->manifest_path[sizeof(registry->manifest_path) - 1] = '\0';
    }
}

void media_registry_shutdown(MediaRegistry* registry) {
    if (!registry) return;
    if (registry->dirty) {
        media_registry_save(registry);
    }
    free(registry->entries);
    registry->entries = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

bool media_registry_load(MediaRegistry* registry) {
    if (!registry || registry->manifest_path[0] == '\0') return false;
    FILE* file = fopen(registry->manifest_path, "rb");
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return false;
    }
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    registry->count = 0;
    bool ok = registry_parse_json(registry, buffer, read);
    free(buffer);
    registry->dirty = false;
    return ok;
}

bool media_registry_save(MediaRegistry* registry) {
    if (!registry || registry->manifest_path[0] == '\0') return false;
    FILE* file = fopen(registry->manifest_path, "wb");
    if (!file) return false;
    registry_write_json(file, registry);
    fclose(file);
    registry->dirty = false;
    return true;
}

const MediaRegistryEntry* media_registry_find_by_id(const MediaRegistry* registry, const char* id) {
    if (!registry || !id || id[0] == '\0') return NULL;
    for (int i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].id, id) == 0) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

MediaRegistryEntry* media_registry_find_by_path(MediaRegistry* registry, const char* path) {
    if (!registry || !path || path[0] == '\0') return NULL;
    for (int i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].path, path) == 0) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

bool media_registry_ensure_for_path(MediaRegistry* registry,
                                    const char* path,
                                    const char* display_name,
                                    MediaRegistryEntry* out_entry) {
    if (!registry || !path) return false;
    MediaRegistryEntry* existing = media_registry_find_by_path(registry, path);
    if (existing) {
        if (display_name && display_name[0] != '\0' && strcmp(existing->name, display_name) != 0) {
            strncpy(existing->name, display_name, sizeof(existing->name) - 1);
            existing->name[sizeof(existing->name) - 1] = '\0';
            registry->dirty = true;
        }
        if (out_entry) {
            *out_entry = *existing;
        }
        return true;
    }
    MediaRegistryEntry entry = {0};
    if (!media_registry_hash_file(path, entry.id, sizeof(entry.id), &entry.file_size)) {
        return false;
    }
    MediaRegistryEntry* by_id = NULL;
    if (entry.id[0] != '\0') {
        for (int i = 0; i < registry->count; ++i) {
            if (strcmp(registry->entries[i].id, entry.id) == 0) {
                by_id = &registry->entries[i];
                break;
            }
        }
    }
    if (by_id) {
        strncpy(by_id->path, path, sizeof(by_id->path) - 1);
        by_id->path[sizeof(by_id->path) - 1] = '\0';
        if (display_name && display_name[0] != '\0') {
            strncpy(by_id->name, display_name, sizeof(by_id->name) - 1);
            by_id->name[sizeof(by_id->name) - 1] = '\0';
        }
        registry->dirty = true;
        if (out_entry) {
            *out_entry = *by_id;
        }
        return true;
    }
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.path[sizeof(entry.path) - 1] = '\0';
    if (display_name) {
        strncpy(entry.name, display_name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
    }
    if (!registry_ensure_capacity(registry, registry->count + 1)) {
        return false;
    }
    registry->entries[registry->count++] = entry;
    registry->dirty = true;
    if (out_entry) {
        *out_entry = entry;
    }
    return true;
}

bool media_registry_update_path(MediaRegistry* registry,
                                const char* id,
                                const char* new_path,
                                const char* new_name) {
    if (!registry || !id || !new_path) return false;
    for (int i = 0; i < registry->count; ++i) {
        MediaRegistryEntry* entry = &registry->entries[i];
        if (strcmp(entry->id, id) == 0) {
            strncpy(entry->path, new_path, sizeof(entry->path) - 1);
            entry->path[sizeof(entry->path) - 1] = '\0';
            if (new_name && new_name[0] != '\0') {
                strncpy(entry->name, new_name, sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
            }
            registry->dirty = true;
            return true;
        }
    }
    return false;
}
