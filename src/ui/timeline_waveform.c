#include "ui/timeline_waveform.h"

#include <SDL2/SDL.h>

#include "core_pack.h"
#include "kit_viz.h"

#include <limits.h>
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
    entry->source = WAVEFORM_CACHE_SOURCE_SAMPLES;
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
    entry->source = WAVEFORM_CACHE_SOURCE_SAMPLES;
    entry->mins = mins;
    entry->maxs = maxs;
    return true;
}

typedef struct DawPackHeaderCanonical {
    uint32_t version;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t samples_per_pixel;
    uint64_t point_count;
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t project_duration_frames;
} DawPackHeaderCanonical;

static bool waveform_pack_path_from_media(const char* media_path,
                                          char* out_pack_path,
                                          size_t out_pack_path_len) {
    if (!media_path || !out_pack_path || out_pack_path_len == 0) {
        return false;
    }

    size_t in_len = strlen(media_path);
    if (in_len == 0 || in_len + 6 > out_pack_path_len) {
        return false;
    }

    const char* ext = strrchr(media_path, '.');
    if (ext && strcmp(ext, ".wav") == 0) {
        size_t stem_len = (size_t)(ext - media_path);
        if (stem_len + 6 > out_pack_path_len) {
            return false;
        }
        memcpy(out_pack_path, media_path, stem_len);
        memcpy(out_pack_path + stem_len, ".pack", 6);
        return true;
    }

    memcpy(out_pack_path, media_path, in_len);
    memcpy(out_pack_path + in_len, ".pack", 6);
    return true;
}

static bool waveform_build_entry_from_pack(WaveformCacheEntry* entry,
                                           const AudioMediaClip* clip,
                                           const char* media_path,
                                           int samples_per_pixel) {
    if (!entry || !clip || !media_path || samples_per_pixel < 1 ||
        clip->frame_count == 0 || clip->channels <= 0 || clip->sample_rate <= 0) {
        return false;
    }

    char pack_path[ENGINE_CLIP_PATH_MAX];
    if (!waveform_pack_path_from_media(media_path, pack_path, sizeof(pack_path))) {
        return false;
    }

    CorePackReader reader = {0};
    CoreResult open_r = core_pack_reader_open(pack_path, &reader);
    if (open_r.code != CORE_OK) {
        return false;
    }

    CorePackChunkInfo dawh = {0};
    CorePackChunkInfo wmin = {0};
    CorePackChunkInfo wmax = {0};
    if (core_pack_reader_find_chunk(&reader, "DAWH", 0, &dawh).code != CORE_OK ||
        core_pack_reader_find_chunk(&reader, "WMIN", 0, &wmin).code != CORE_OK ||
        core_pack_reader_find_chunk(&reader, "WMAX", 0, &wmax).code != CORE_OK) {
        core_pack_reader_close(&reader);
        return false;
    }

    if (dawh.size != sizeof(DawPackHeaderCanonical)) {
        core_pack_reader_close(&reader);
        return false;
    }

    DawPackHeaderCanonical hdr = {0};
    if (core_pack_reader_read_chunk_data(&reader, &dawh, &hdr, sizeof(hdr)).code != CORE_OK) {
        core_pack_reader_close(&reader);
        return false;
    }

    if (hdr.point_count == 0 || hdr.version == 0) {
        core_pack_reader_close(&reader);
        return false;
    }
    if (hdr.sample_rate != (uint32_t)clip->sample_rate || hdr.channels != (uint32_t)clip->channels) {
        core_pack_reader_close(&reader);
        return false;
    }
    if (hdr.samples_per_pixel == 0 || hdr.point_count > (uint64_t)UINT32_MAX) {
        core_pack_reader_close(&reader);
        return false;
    }

    uint64_t expected_bytes = hdr.point_count * sizeof(float);
    if (wmin.size != expected_bytes || wmax.size != expected_bytes) {
        core_pack_reader_close(&reader);
        return false;
    }
    if (hdr.point_count > (uint64_t)INT32_MAX) {
        core_pack_reader_close(&reader);
        return false;
    }

    float* src_mins = (float*)malloc((size_t)expected_bytes);
    float* src_maxs = (float*)malloc((size_t)expected_bytes);
    if (!src_mins || !src_maxs) {
        free(src_mins);
        free(src_maxs);
        core_pack_reader_close(&reader);
        return false;
    }

    if (core_pack_reader_read_chunk_data(&reader, &wmin, src_mins, wmin.size).code != CORE_OK ||
        core_pack_reader_read_chunk_data(&reader, &wmax, src_maxs, wmax.size).code != CORE_OK) {
        free(src_mins);
        free(src_maxs);
        core_pack_reader_close(&reader);
        return false;
    }

    core_pack_reader_close(&reader);

    float* mins = NULL;
    float* maxs = NULL;
    int bucket_count = 0;
    if (hdr.samples_per_pixel == (uint32_t)samples_per_pixel) {
        mins = src_mins;
        maxs = src_maxs;
        bucket_count = (int)hdr.point_count;
    } else {
        uint64_t target_count_u64 =
            (clip->frame_count + (uint64_t)samples_per_pixel - 1u) / (uint64_t)samples_per_pixel;
        if (target_count_u64 == 0 || target_count_u64 > (uint64_t)INT32_MAX ||
            target_count_u64 > (uint64_t)UINT32_MAX) {
            free(src_mins);
            free(src_maxs);
            return false;
        }

        uint32_t target_count = (uint32_t)target_count_u64;
        mins = (float*)malloc((size_t)target_count * sizeof(float));
        maxs = (float*)malloc((size_t)target_count * sizeof(float));
        if (!mins || !maxs) {
            free(mins);
            free(maxs);
            free(src_mins);
            free(src_maxs);
            return false;
        }

        CoreResult sample_r = kit_viz_sample_waveform_envelope(src_mins,
                                                               src_maxs,
                                                               (uint32_t)hdr.point_count,
                                                               0.0,
                                                               (double)samples_per_pixel /
                                                                   (double)hdr.samples_per_pixel,
                                                               target_count,
                                                               mins,
                                                               maxs,
                                                               (size_t)target_count);
        free(src_mins);
        free(src_maxs);
        if (sample_r.code != CORE_OK) {
            free(mins);
            free(maxs);
            return false;
        }
        bucket_count = (int)target_count;
    }

    waveform_entry_free(entry);
    entry->samples_per_pixel = samples_per_pixel;
    entry->bucket_count = bucket_count;
    entry->frame_count = clip->frame_count;
    entry->channels = clip->channels;
    entry->sample_rate = clip->sample_rate;
    entry->source = WAVEFORM_CACHE_SOURCE_PACK;
    entry->mins = mins;
    entry->maxs = maxs;
    return true;
}

const WaveformCacheEntry* waveform_cache_get(WaveformCache* cache,
                                             const AudioMediaClip* clip,
                                             const char* path,
                                             int samples_per_pixel) {
    if (!cache || !clip || !path) {
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
                if (!waveform_build_entry_from_pack(entry, clip, path, samples_per_pixel) &&
                    !waveform_build_entry(entry, clip, samples_per_pixel)) {
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
    if (!waveform_build_entry_from_pack(entry, clip, path, samples_per_pixel) &&
        !waveform_build_entry(entry, clip, samples_per_pixel)) {
        cache->count -= 1;
        return NULL;
    }
    return entry;
}
