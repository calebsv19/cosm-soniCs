#pragma once

#include <stdint.h>

#include "audio/media_clip.h"
#include "engine/engine.h"

typedef enum {
    WAVEFORM_CACHE_SOURCE_SAMPLES = 0,
    WAVEFORM_CACHE_SOURCE_PACK = 1
} WaveformCacheSource;

typedef struct {
    char path[ENGINE_CLIP_PATH_MAX];
    int samples_per_pixel;
    int bucket_count;
    uint64_t frame_count;
    int channels;
    int sample_rate;
    WaveformCacheSource source;
    float* mins;
    float* maxs;
} WaveformCacheEntry;

typedef struct {
    WaveformCacheEntry* entries;
    int count;
    int capacity;
} WaveformCache;

void waveform_cache_init(WaveformCache* cache);
void waveform_cache_shutdown(WaveformCache* cache);
const WaveformCacheEntry* waveform_cache_get(WaveformCache* cache,
                                             const AudioMediaClip* clip,
                                             const char* path,
                                             int samples_per_pixel);
