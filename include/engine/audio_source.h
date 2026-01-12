#pragma once

#include "audio/media_registry.h"

#include <stdint.h>

#define ENGINE_AUDIO_SOURCE_PATH_MAX 512

typedef struct AudioMediaClip AudioMediaClip;
typedef struct Engine Engine;

typedef struct EngineAudioSource {
    char media_id[MEDIA_ID_MAX];
    char path[ENGINE_AUDIO_SOURCE_PATH_MAX];
    int sample_rate;
    int channels;
    uint64_t frame_count;
    AudioMediaClip* clip; // Weak ref to cache-owned clip
} EngineAudioSource;

EngineAudioSource* engine_audio_source_get(Engine* engine, const char* media_id);
EngineAudioSource* engine_audio_source_get_or_create(Engine* engine, const char* media_id, const char* path);
void engine_audio_source_clear_all(Engine* engine);
