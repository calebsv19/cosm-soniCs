#include "audio/media_cache.h"

#include <SDL2/SDL.h>

#include <limits.h>
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
    cache->identities = NULL;
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
    free(cache->identities);
    cache->clips = NULL;
    cache->ids = NULL;
    cache->paths = NULL;
    cache->sample_rates = NULL;
    cache->refcounts = NULL;
    cache->identities = NULL;
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

// Matches observable file versions while keeping prior decoded versions pinned independently.
static bool cache_same_version(const struct stat* a, const struct stat* b) {
#if defined(__APPLE__)
    bool times = a->st_mtimespec.tv_sec==b->st_mtimespec.tv_sec && a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec &&
        a->st_ctimespec.tv_sec==b->st_ctimespec.tv_sec && a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
    bool times = a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
    return times && a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size;
}

static int audio_media_cache_find(const AudioMediaCache* cache, const char* media_id, const char* path, int sample_rate) {
    if (!cache) {
        return -1;
    }
    struct stat current;
    bool have_current = path && stat(path, &current)==0;
    for (int i = cache->count - 1; i >= 0; --i) {
        if (have_current && !cache_same_version(&cache->identities[i], &current)) continue;
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
    if (cache->capacity > INT_MAX / 2) return false;
    int new_capacity = cache->capacity == 0 ? 8 : cache->capacity * 2;

    AudioMediaClip** new_clips = (AudioMediaClip**)malloc(sizeof(AudioMediaClip*) * (size_t)new_capacity);
    char (*new_ids)[MEDIA_ID_MAX] = (char (*)[MEDIA_ID_MAX])malloc(sizeof(*new_ids) * (size_t)new_capacity);
    char (*new_paths)[AUDIO_MEDIA_CACHE_PATH_MAX] = (char (*)[AUDIO_MEDIA_CACHE_PATH_MAX])malloc(sizeof(*new_paths) * (size_t)new_capacity);
    int* new_rates = (int*)malloc(sizeof(int) * (size_t)new_capacity);
    int* new_refs = (int*)malloc(sizeof(int) * (size_t)new_capacity);
    struct stat* new_identities = malloc(sizeof(*new_identities) * (size_t)new_capacity);
    if (!new_clips || !new_ids || !new_paths || !new_rates || !new_refs || !new_identities) {
        free(new_identities);
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
        memcpy(new_identities, cache->identities, sizeof(*new_identities) * (size_t)cache->count);
    }

    free(cache->clips);
    free(cache->ids);
    free(cache->paths);
    free(cache->sample_rates);
    free(cache->refcounts);
    free(cache->identities);

    cache->clips = new_clips;
    cache->ids = new_ids;
    cache->paths = new_paths;
    cache->sample_rates = new_rates;
    cache->refcounts = new_refs;
    cache->identities = new_identities;
    cache->capacity = new_capacity;
    return true;
}

// Retains existing decoded bytes for a prepared render plan owned by the control thread.
bool audio_media_cache_retain(AudioMediaCache* cache, const AudioMediaClip* clip) {
    if (!cache || !clip) return false;
    for (int i = 0; i < cache->count; ++i) {
        if (cache->clips[i] == clip) {
            if (cache->refcounts[i] == INT_MAX) return false;
            cache->refcounts[i] += 1;
            return true;
        }
    }
    return false;
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
        if (!audio_media_cache_retain(cache, cache->clips[existing])) return false;
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
    if (!audio_media_clip_load(path, target_sample_rate, &temp)) return false;
    bool ok = audio_media_cache_adopt(cache, media_id, path, target_sample_rate, &temp, out_clip);
    audio_media_clip_free(&temp);
    return ok;
}

// Adopts private decoded storage only on success, converging existing keys without reopening files.
bool audio_media_cache_adopt(AudioMediaCache* cache, const char* media_id, const char* path,
                             int target_sample_rate, AudioMediaClip* prepared, AudioMediaClip** out_clip) {
    if (!cache || !path || !*path || strlen(path) >= AUDIO_MEDIA_CACHE_PATH_MAX || !out_clip ||
        (media_id && strlen(media_id) >= MEDIA_ID_MAX) || !prepared || !prepared->samples ||
        prepared->channels <= 0 || !prepared->frame_count || prepared->sample_rate <= 0 ||
        prepared->frame_count > SIZE_MAX / sizeof(float) / (size_t)prepared->channels ||
        (target_sample_rate > 0 && prepared->sample_rate != target_sample_rate)) return false;
    int existing = audio_media_cache_find(cache, media_id, path, target_sample_rate);
    if (existing >= 0) {
        if (!audio_media_cache_retain(cache, cache->clips[existing])) return false;
        *out_clip = cache->clips[existing];
        audio_media_clip_free(prepared);
        return true;
    }
    if (cache->count == cache->capacity) {
        if (!audio_media_cache_grow(cache)) {
            return false;
        }
    }

    int slot = cache->count++;
    AudioMediaClip* stored = (AudioMediaClip*)malloc(sizeof(AudioMediaClip));
    if (!stored) {
        cache->count--;
        return false;
    }
    *stored = *prepared;
    *prepared = (AudioMediaClip){0};
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
    memset(&cache->identities[slot], 0, sizeof(cache->identities[slot]));
    (void)stat(path, &cache->identities[slot]);
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
                    cache->identities[i] = cache->identities[cache->count - 1];
                }
                cache->count -= 1;
                if (cache->verbose) {
                    SDL_Log("media_cache: evict entry, remaining=%d", cache->count);
                }
            }
            return;
        }
    }
    // Unknown storage remains owned by its caller.
}

// Accounts full decoded residency separately from background preparation budgets.
size_t audio_media_cache_resident_bytes(const AudioMediaCache* cache) {
    size_t bytes = 0;
    if (cache) for (int i = 0; i < cache->count; ++i) {
        size_t n = cache->clips[i]->frame_count * cache->clips[i]->channels * sizeof(float);
        if (n > SIZE_MAX - bytes) return SIZE_MAX;
        bytes += n;
    }
    return bytes;
}

// Looks up existing decoded media without decoding or adding a reference.
const AudioMediaClip* audio_media_cache_lookup(const AudioMediaCache* cache, const char* id, const char* path, int rate) {
    int index=audio_media_cache_find(cache,id,path,rate);
    return index>=0 ? cache->clips[index] : NULL;
}
