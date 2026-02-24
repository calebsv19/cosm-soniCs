#include "ui/timeline_waveform.h"
#include "core_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void fail(const char* msg) {
    fprintf(stderr, "waveform_cache_pack_warmstart_test: %s\n", msg);
    exit(1);
}

static void expect(int cond, const char* msg) {
    if (!cond) fail(msg);
}

int main(void) {
    const char* media_path = "/tmp/waveform_cache_pack_warmstart.wav";
    const char* pack_path = "/tmp/waveform_cache_pack_warmstart.pack";

    DawPackHeaderCanonical h = {0};
    h.version = 1;
    h.sample_rate = 48000;
    h.channels = 2;
    h.samples_per_pixel = 256;
    h.point_count = 4;
    h.start_frame = 0;
    h.end_frame = 1024;
    h.project_duration_frames = 1024;

    float mins[4] = {-1.0f, -0.5f, -0.25f, -0.1f};
    float maxs[4] = {1.0f, 0.5f, 0.25f, 0.1f};

    CorePackWriter w = {0};
    expect(core_pack_writer_open(pack_path, &w).code == CORE_OK, "open pack writer failed");
    expect(core_pack_writer_add_chunk(&w, "DAWH", &h, sizeof(h)).code == CORE_OK, "write DAWH failed");
    expect(core_pack_writer_add_chunk(&w, "WMIN", mins, sizeof(mins)).code == CORE_OK, "write WMIN failed");
    expect(core_pack_writer_add_chunk(&w, "WMAX", maxs, sizeof(maxs)).code == CORE_OK, "write WMAX failed");
    expect(core_pack_writer_close(&w).code == CORE_OK, "close pack writer failed");

    float dummy_samples[8] = {0};
    AudioMediaClip clip = {
        .samples = dummy_samples,
        .frame_count = 1024,
        .channels = 2,
        .sample_rate = 48000,
    };

    WaveformCache cache;
    waveform_cache_init(&cache);

    const WaveformCacheEntry* entry = waveform_cache_get(&cache, &clip, media_path, 256);
    expect(entry != NULL, "waveform_cache_get returned null");
    expect(entry->source == WAVEFORM_CACHE_SOURCE_PACK, "expected pack warm-start source");
    expect(entry->bucket_count == 4, "unexpected bucket_count");
    expect(entry->mins && entry->maxs, "missing envelope arrays");
    expect(entry->mins[0] == -1.0f && entry->maxs[0] == 1.0f, "unexpected envelope data");

    // Different requested resolution should still warm-start from pack by resampling.
    const WaveformCacheEntry* resampled = waveform_cache_get(&cache, &clip, media_path, 128);
    expect(resampled != NULL, "resampled waveform_cache_get returned null");
    expect(resampled->source == WAVEFORM_CACHE_SOURCE_PACK, "expected pack source for resampled warm-start");
    expect(resampled->bucket_count == 8, "unexpected resampled bucket_count");
    expect(resampled->mins && resampled->maxs, "missing resampled envelope arrays");

    // Pack warm-start must also work when raw samples are unavailable.
    AudioMediaClip pack_only_clip = {
        .samples = NULL,
        .frame_count = 1024,
        .channels = 2,
        .sample_rate = 48000,
    };
    const WaveformCacheEntry* pack_only = waveform_cache_get(&cache, &pack_only_clip, media_path, 256);
    expect(pack_only != NULL, "pack-only waveform_cache_get returned null");
    expect(pack_only->source == WAVEFORM_CACHE_SOURCE_PACK, "expected pack source for pack-only clip");

    waveform_cache_shutdown(&cache);
    remove(pack_path);
    puts("waveform_cache_pack_warmstart_test: success");
    return 0;
}
