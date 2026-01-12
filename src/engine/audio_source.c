#include "engine/audio_source.h"

#include "audio/media_cache.h"
#include "engine/engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fallback_id_from_path(const char* path, char* out_id, size_t out_len) {
    if (!out_id || out_len == 0) {
        return;
    }
    out_id[0] = '\0';
    if (!path || path[0] == '\0') {
        return;
    }
    const uint64_t prime = 1099511628211ULL;
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
        hash ^= (uint64_t)(*p);
        hash *= prime;
    }
    snprintf(out_id, out_len, "%016llx", (unsigned long long)hash);
}

static EngineAudioSource* engine_audio_source_find(Engine* engine, const char* media_id) {
    if (!engine || !media_id || media_id[0] == '\0') {
        return NULL;
    }
    for (int i = 0; i < engine->audio_source_count; ++i) {
        if (strncmp(engine->audio_sources[i].media_id, media_id, MEDIA_ID_MAX) == 0) {
            return &engine->audio_sources[i];
        }
    }
    return NULL;
}

EngineAudioSource* engine_audio_source_get(Engine* engine, const char* media_id) {
    return engine_audio_source_find(engine, media_id);
}

EngineAudioSource* engine_audio_source_get_or_create(Engine* engine, const char* media_id, const char* path) {
    if (!engine) {
        return NULL;
    }

    char fallback_id[MEDIA_ID_MAX];
    const char* resolved_id = media_id;
    if (!resolved_id || resolved_id[0] == '\0') {
        fallback_id_from_path(path, fallback_id, sizeof(fallback_id));
        resolved_id = fallback_id;
    }
    if (!resolved_id || resolved_id[0] == '\0') {
        return NULL;
    }

    EngineAudioSource* existing = engine_audio_source_find(engine, resolved_id);
    if (existing) {
        if (path && path[0] != '\0' && strncmp(existing->path, path, ENGINE_AUDIO_SOURCE_PATH_MAX) != 0) {
            SDL_strlcpy(existing->path, path, ENGINE_AUDIO_SOURCE_PATH_MAX);
        }
        return existing;
    }

    if (engine->audio_source_count >= engine->audio_source_capacity) {
        int new_capacity = engine->audio_source_capacity > 0 ? engine->audio_source_capacity * 2 : 16;
        EngineAudioSource* resized = (EngineAudioSource*)realloc(engine->audio_sources,
                                                                 sizeof(EngineAudioSource) * (size_t)new_capacity);
        if (!resized) {
            return NULL;
        }
        engine->audio_sources = resized;
        engine->audio_source_capacity = new_capacity;
    }

    EngineAudioSource* source = &engine->audio_sources[engine->audio_source_count++];
    memset(source, 0, sizeof(*source));
    SDL_strlcpy(source->media_id, resolved_id, MEDIA_ID_MAX);
    if (path && path[0] != '\0') {
        SDL_strlcpy(source->path, path, ENGINE_AUDIO_SOURCE_PATH_MAX);
    }
    return source;
}

void engine_audio_source_clear_all(Engine* engine) {
    if (!engine || !engine->audio_sources) {
        return;
    }
    engine->audio_source_count = 0;
}
