#include "audio/media_cache.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <string.h>

void audio_media_cache_init(AudioMediaCache* cache, bool verbose) {
    if (!cache) {
        return;
    }
    cache->clips = NULL;
    cache->ids = NULL;
    cache->paths = NULL;
    cache->sample_rates = NULL;
    cache->refcounts = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->verbose = verbose;
}

void audio_media_cache_shutdown(AudioMediaCache* cache) {
    if (!cache) {
        return;
    }
    for (int i = 0; i < cache->count; ++i) {
        if (cache->clips[i]) {
            audio_media_clip_free(cache->clips[i]);
            free(cache->clips[i]);
        }
    }
    free(cache->clips);
    free(cache->ids);
    free(cache->paths);
    free(cache->sample_rates);
    free(cache->refcounts);
    cache->clips = NULL;
    cache->ids = NULL;
    cache->paths = NULL;
    cache->sample_rates = NULL;
    cache->refcounts = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->verbose = false;
}

void audio_media_cache_set_verbose(AudioMediaCache* cache, bool verbose) {
    if (!cache) {
        return;
    }
    cache->verbose = verbose;
}

static int audio_media_cache_find(const AudioMediaCache* cache, const char* media_id, const char* path, int sample_rate) {
    if (!cache) {
        return -1;
    }
    for (int i = 0; i < cache->count; ++i) {
        if (cache->sample_rates[i] != sample_rate) {
            continue;
        }
        if (media_id && media_id[0] != '\0') {
            if (strcmp(cache->ids[i], media_id) == 0) {
                return i;
            }
        } else if (path && path[0] != '\0') {
            if (strcmp(cache->paths[i], path) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static bool audio_media_cache_grow(AudioMediaCache* cache) {
    int new_capacity = cache->capacity == 0 ? 8 : cache->capacity * 2;

    AudioMediaClip** new_clips = (AudioMediaClip**)malloc(sizeof(AudioMediaClip*) * (size_t)new_capacity);
    char (*new_ids)[MEDIA_ID_MAX] = (char (*)[MEDIA_ID_MAX])malloc(sizeof(*new_ids) * (size_t)new_capacity);
    char (*new_paths)[AUDIO_MEDIA_CACHE_PATH_MAX] = (char (*)[AUDIO_MEDIA_CACHE_PATH_MAX])malloc(sizeof(*new_paths) * (size_t)new_capacity);
    int* new_rates = (int*)malloc(sizeof(int) * (size_t)new_capacity);
    int* new_refs = (int*)malloc(sizeof(int) * (size_t)new_capacity);
    if (!new_clips || !new_ids || !new_paths || !new_rates || !new_refs) {
        free(new_clips);
        free(new_ids);
        free(new_paths);
        free(new_rates);
        free(new_refs);
        return false;
    }

    if (cache->count > 0) {
        memcpy(new_clips, cache->clips, sizeof(AudioMediaClip*) * (size_t)cache->count);
        memcpy(new_ids, cache->ids, sizeof(*new_ids) * (size_t)cache->count);
        memcpy(new_paths, cache->paths, sizeof(*new_paths) * (size_t)cache->count);
        memcpy(new_rates, cache->sample_rates, sizeof(int) * (size_t)cache->count);
        memcpy(new_refs, cache->refcounts, sizeof(int) * (size_t)cache->count);
    }

    free(cache->clips);
    free(cache->ids);
    free(cache->paths);
    free(cache->sample_rates);
    free(cache->refcounts);

    cache->clips = new_clips;
    cache->ids = new_ids;
    cache->paths = new_paths;
    cache->sample_rates = new_rates;
    cache->refcounts = new_refs;
    cache->capacity = new_capacity;
    return true;
}

bool audio_media_cache_acquire(AudioMediaCache* cache,
                               const char* media_id,
                               const char* path,
                               int target_sample_rate,
                               AudioMediaClip** out_clip) {
    if (!cache || !path || !out_clip) {
        return false;
    }
    int existing = audio_media_cache_find(cache, media_id, path, target_sample_rate);
    if (existing >= 0) {
        cache->refcounts[existing] += 1;
        *out_clip = cache->clips[existing];
        if (path && path[0] != '\0' && strcmp(cache->paths[existing], path) != 0) {
            strncpy(cache->paths[existing], path, sizeof(cache->paths[existing]) - 1);
            cache->paths[existing][sizeof(cache->paths[existing]) - 1] = '\0';
        }
        if (cache->verbose) {
            SDL_Log("media_cache: reuse %s @ %dHz (refcount=%d)",
                    cache->paths[existing], cache->sample_rates[existing], cache->refcounts[existing]);
        }
        return true;
    }

    AudioMediaClip temp = {0};
    if (!audio_media_clip_load(path, target_sample_rate, &temp)) {
        return false;
    }

    if (cache->count == cache->capacity) {
        if (!audio_media_cache_grow(cache)) {
            audio_media_clip_free(&temp);
            return false;
        }
    }

    int slot = cache->count++;
    AudioMediaClip* stored = (AudioMediaClip*)malloc(sizeof(AudioMediaClip));
    if (!stored) {
        audio_media_clip_free(&temp);
        cache->count--;
        return false;
    }
    *stored = temp;
    cache->clips[slot] = stored;
    if (media_id && media_id[0] != '\0') {
        strncpy(cache->ids[slot], media_id, sizeof(cache->ids[slot]) - 1);
        cache->ids[slot][sizeof(cache->ids[slot]) - 1] = '\0';
    } else {
        cache->ids[slot][0] = '\0';
    }
    strncpy(cache->paths[slot], path, sizeof(cache->paths[slot]) - 1);
    cache->paths[slot][sizeof(cache->paths[slot]) - 1] = '\0';
    cache->sample_rates[slot] = target_sample_rate;
    cache->refcounts[slot] = 1;
    if (cache->verbose) {
        SDL_Log("media_cache: load %s @ %dHz", cache->paths[slot], cache->sample_rates[slot]);
    }
    *out_clip = cache->clips[slot];
    return true;
}

void audio_media_cache_release(AudioMediaCache* cache, const AudioMediaClip* clip) {
    if (!cache || !clip) {
        return;
    }
    for (int i = 0; i < cache->count; ++i) {
        if (cache->clips[i] == clip) {
            if (cache->refcounts[i] > 0) {
                cache->refcounts[i] -= 1;
            }
            if (cache->verbose) {
                SDL_Log("media_cache: release %s (refcount=%d)", cache->paths[i], cache->refcounts[i]);
            }
            if (cache->refcounts[i] == 0) {
                if (cache->clips[i]) {
                    audio_media_clip_free(cache->clips[i]);
                    free(cache->clips[i]);
                    cache->clips[i] = NULL;
                }
                if (i != cache->count - 1) {
                    cache->clips[i] = cache->clips[cache->count - 1];
                    memcpy(cache->ids[i], cache->ids[cache->count - 1], sizeof(cache->ids[i]));
                    memcpy(cache->paths[i], cache->paths[cache->count - 1], sizeof(cache->paths[i]));
                    cache->sample_rates[i] = cache->sample_rates[cache->count - 1];
                    cache->refcounts[i] = cache->refcounts[cache->count - 1];
                }
                cache->count -= 1;
                if (cache->verbose) {
                    SDL_Log("media_cache: evict entry, remaining=%d", cache->count);
                }
            }
            return;
        }
    }
    audio_media_clip_free((AudioMediaClip*)clip);
    free((AudioMediaClip*)clip);
}
