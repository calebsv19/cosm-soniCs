#pragma once

#include "audio/media_clip.h"
#include "audio/media_registry.h"

#include <stdbool.h>
#include <sys/stat.h>

#define AUDIO_MEDIA_CACHE_PATH_MAX 512

typedef struct {
    AudioMediaClip** clips;
    char (*ids)[MEDIA_ID_MAX];
    char (*paths)[AUDIO_MEDIA_CACHE_PATH_MAX];
    int* sample_rates;
    int* refcounts;
    struct stat* identities; // Source versions can coexist while older render plans remain pinned.
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
// Pins an already decoded cache entry on the control thread without opening its source file.
bool audio_media_cache_retain(AudioMediaCache* cache, const AudioMediaClip* clip);
void audio_media_cache_set_verbose(AudioMediaCache* cache, bool verbose);

// Consumes prepared samples only on success and returns one control-owned cache pin.
bool audio_media_cache_adopt(AudioMediaCache* cache, const char* media_id, const char* path,
                             int target_sample_rate, AudioMediaClip* prepared, AudioMediaClip** out_clip);
// Reports decoded resident bytes; all entries remain pinned until immediate zero-reference eviction.
size_t audio_media_cache_resident_bytes(const AudioMediaCache* cache);

// Borrows a matching current source version for the duration of one control-thread operation.
const AudioMediaClip* audio_media_cache_lookup(const AudioMediaCache* cache, const char* id, const char* path, int rate);
