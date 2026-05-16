#include "audio/media_cache.h"
#include "audio/media_clip.h"

#include <assert.h>
#include <stdio.h>

static const char* kTestPathA = "assets/audio/kamhunt-run-130bpm-190419.mp3";
static const char* kTestPathB = "assets/audio/kamhunt-sunflower-street-drumloop-85bpm-163900.mp3";
static const char* kTestPathC = "assets/audio/kamhunt-timbo-drumline-loop-103bpm-171091.mp3";
static const char* kTestMediaIdA = "media-cache-test-a";
static const char* kTestMediaIdB = "media-cache-test-b";
static const char* kTestMediaIdC = "media-cache-test-c";

static void expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "media_cache_stress_test: %s\n", message);
        fflush(stderr);
        assert(condition);
    }
}

static void exercise_duplicate_acquire(AudioMediaCache* cache) {
    const int kDuplicates = 32;
    AudioMediaClip* clips[kDuplicates];
    for (int i = 0; i < kDuplicates; ++i) {
        expect(audio_media_cache_acquire(cache, kTestMediaIdA, kTestPathA, 48000, &clips[i]),
               "failed to acquire duplicate clip");
        if (i > 0) {
            expect(clips[i] == clips[0], "duplicate acquire did not share cached clip");
        }
    }
    expect(cache->count == 1, "duplicate acquire should produce a single cache entry");

    for (int i = kDuplicates - 1; i >= 0; --i) {
        audio_media_cache_release(cache, clips[i]);
    }
    expect(cache->count == 0, "cache should release entry after refcount reaches zero");
}

static void exercise_sample_rate_variants(AudioMediaCache* cache) {
    AudioMediaClip* clip_48k = NULL;
    AudioMediaClip* clip_44k = NULL;
    expect(audio_media_cache_acquire(cache, kTestMediaIdA, kTestPathA, 48000, &clip_48k), "acquire 48k failed");
    expect(audio_media_cache_acquire(cache, kTestMediaIdA, kTestPathA, 44100, &clip_44k), "acquire 44.1k failed");
    expect(cache->count == 2, "sample rate variants should occupy distinct cache slots");
    expect(clip_48k != clip_44k, "sample rate variants should produce different clip pointers");
    audio_media_cache_release(cache, clip_48k);
    audio_media_cache_release(cache, clip_44k);
    expect(cache->count == 0, "releasing sample rate variants should empty cache");
}

static void exercise_capacity_growth(AudioMediaCache* cache) {
    AudioMediaClip* clip_a = NULL;
    AudioMediaClip* clip_b = NULL;
    AudioMediaClip* clip_c = NULL;
    expect(audio_media_cache_acquire(cache, kTestMediaIdA, kTestPathA, 48000, &clip_a), "acquire clip A failed");
    expect(audio_media_cache_acquire(cache, kTestMediaIdB, kTestPathB, 48000, &clip_b), "acquire clip B failed");
    expect(audio_media_cache_acquire(cache, kTestMediaIdC, kTestPathC, 48000, &clip_c), "acquire clip C failed");
    expect(cache->count == 3, "three unique clips should be cached");
    audio_media_cache_release(cache, clip_b);
    audio_media_cache_release(cache, clip_a);
    expect(cache->count == 1, "releasing subset should leave remaining entries");
    audio_media_cache_release(cache, clip_c);
    expect(cache->count == 0, "cache should be empty after final release");
}

static void exercise_churn(AudioMediaCache* cache) {
    const int kCycles = 64;
    for (int i = 0; i < kCycles; ++i) {
        AudioMediaClip* clip = NULL;
        expect(audio_media_cache_acquire(cache, kTestMediaIdB, kTestPathB, 48000, &clip), "churn acquire failed");
        audio_media_cache_release(cache, clip);
    }
    expect(cache->count == 0, "cache should return to empty after churn cycle");
}

int main(void) {
    AudioMediaCache cache;
    audio_media_cache_init(&cache, false);

    exercise_duplicate_acquire(&cache);
    exercise_sample_rate_variants(&cache);
    exercise_capacity_growth(&cache);
    exercise_churn(&cache);

    audio_media_cache_shutdown(&cache);
    fprintf(stdout, "media_cache_stress_test: success (entries=%d)\n", cache.count);
    return 0;
}
