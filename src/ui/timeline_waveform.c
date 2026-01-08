#include "ui/timeline_waveform.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clamp_unit(float v) {
    if (v < -1.0f) return -1.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void waveform_entry_free(WaveformCacheEntry* entry) {
    if (!entry) {
        return;
    }
    free(entry->mins);
    free(entry->maxs);
    entry->mins = NULL;
    entry->maxs = NULL;
    entry->bucket_count = 0;
    entry->frame_count = 0;
    entry->channels = 0;
    entry->sample_rate = 0;
}

static bool waveform_cache_grow(WaveformCache* cache) {
    int new_cap = cache->capacity == 0 ? 8 : cache->capacity * 2;
    WaveformCacheEntry* next = (WaveformCacheEntry*)realloc(cache->entries,
                                                            (size_t)new_cap * sizeof(WaveformCacheEntry));
    if (!next) {
        return false;
    }
    cache->entries = next;
    cache->capacity = new_cap;
    return true;
}

void waveform_cache_init(WaveformCache* cache) {
    if (!cache) {
        return;
    }
    cache->entries = NULL;
    cache->count = 0;
    cache->capacity = 0;
}

void waveform_cache_shutdown(WaveformCache* cache) {
    if (!cache) {
        return;
    }
    for (int i = 0; i < cache->count; ++i) {
        waveform_entry_free(&cache->entries[i]);
    }
    free(cache->entries);
    cache->entries = NULL;
    cache->count = 0;
    cache->capacity = 0;
}

static bool waveform_build_entry(WaveformCacheEntry* entry,
                                 const AudioMediaClip* clip,
                                 int samples_per_pixel) {
    if (!entry || !clip || !clip->samples || clip->frame_count == 0 || clip->channels <= 0) {
        return false;
    }
    uint64_t frame_count = clip->frame_count;
    int channels = clip->channels;
    if (samples_per_pixel < 1) {
        samples_per_pixel = 1;
    }
    int bucket_count = (int)((frame_count + (uint64_t)samples_per_pixel - 1) / (uint64_t)samples_per_pixel);
    if (bucket_count < 1) {
        bucket_count = 1;
    }
    float* mins = (float*)malloc((size_t)bucket_count * sizeof(float));
    float* maxs = (float*)malloc((size_t)bucket_count * sizeof(float));
    if (!mins || !maxs) {
        free(mins);
        free(maxs);
        return false;
    }

    for (int b = 0; b < bucket_count; ++b) {
        uint64_t start = (uint64_t)b * (uint64_t)samples_per_pixel;
        uint64_t end = start + (uint64_t)samples_per_pixel;
        if (end > frame_count) {
            end = frame_count;
        }
        float min_v = 1.0f;
        float max_v = -1.0f;
        if (start >= end) {
            min_v = 0.0f;
            max_v = 0.0f;
        } else {
            for (uint64_t f = start; f < end; ++f) {
                float sum = 0.0f;
                uint64_t base = f * (uint64_t)channels;
                for (int ch = 0; ch < channels; ++ch) {
                    sum += clip->samples[base + (uint64_t)ch];
                }
                float v = sum / (float)channels;
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
            }
        }
        mins[b] = clamp_unit(min_v);
        maxs[b] = clamp_unit(max_v);
    }

    waveform_entry_free(entry);
    entry->samples_per_pixel = samples_per_pixel;
    entry->bucket_count = bucket_count;
    entry->frame_count = frame_count;
    entry->channels = channels;
    entry->sample_rate = clip->sample_rate;
    entry->mins = mins;
    entry->maxs = maxs;
    return true;
}

const WaveformCacheEntry* waveform_cache_get(WaveformCache* cache,
                                             const AudioMediaClip* clip,
                                             const char* path,
                                             int samples_per_pixel) {
    if (!cache || !clip || !path || !clip->samples) {
        return NULL;
    }
    if (samples_per_pixel < 1) {
        samples_per_pixel = 1;
    }

    for (int i = 0; i < cache->count; ++i) {
        WaveformCacheEntry* entry = &cache->entries[i];
        if (entry->samples_per_pixel == samples_per_pixel && strcmp(entry->path, path) == 0) {
            if (entry->frame_count != clip->frame_count || entry->channels != clip->channels ||
                entry->sample_rate != clip->sample_rate) {
                if (!waveform_build_entry(entry, clip, samples_per_pixel)) {
                    return NULL;
                }
            }
            return entry;
        }
    }

    if (cache->count == cache->capacity) {
        if (!waveform_cache_grow(cache)) {
            return NULL;
        }
    }

    WaveformCacheEntry* entry = &cache->entries[cache->count++];
    SDL_zero(*entry);
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    if (!waveform_build_entry(entry, clip, samples_per_pixel)) {
        cache->count -= 1;
        return NULL;
    }
    return entry;
}
