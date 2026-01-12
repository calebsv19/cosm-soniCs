#pragma once

#include "audio/media_clip.h"
#include "audio/media_registry.h"

#include <stdbool.h>

#define AUDIO_MEDIA_CACHE_PATH_MAX 512

typedef struct {
    AudioMediaClip** clips;
    char (*ids)[MEDIA_ID_MAX];
    char (*paths)[AUDIO_MEDIA_CACHE_PATH_MAX];
    int* sample_rates;
    int* refcounts;
    int count;
    int capacity;
    bool verbose;
} AudioMediaCache;

void audio_media_cache_init(AudioMediaCache* cache, bool verbose);
void audio_media_cache_shutdown(AudioMediaCache* cache);
bool audio_media_cache_acquire(AudioMediaCache* cache,
                               const char* media_id,
                               const char* path,
                               int target_sample_rate,
                               AudioMediaClip** out_clip);
void audio_media_cache_release(AudioMediaCache* cache, const AudioMediaClip* clip);
void audio_media_cache_set_verbose(AudioMediaCache* cache, bool verbose);
