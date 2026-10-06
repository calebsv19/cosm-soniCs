#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct AudioMediaClip {
    float* samples;         // Interleaved float32 samples
    uint64_t frame_count;
    int channels;
    int sample_rate;
} AudioMediaClip;

bool audio_media_clip_load(const char* path, int target_sample_rate, AudioMediaClip* out_clip);
bool audio_media_clip_load_wav(const char* path, int target_sample_rate, AudioMediaClip* out_clip);
void audio_media_clip_free(AudioMediaClip* clip);

// Describes source media without allocating decoded sample storage.
typedef struct AudioMediaInfo {
    uint64_t frame_count;
    int channels;
    int sample_rate;
} AudioMediaInfo;
// Reads validated format/duration metadata; never decodes sample payloads.
bool audio_media_probe(const char* path, AudioMediaInfo* out);

// Bounds private decode storage and offers cooperative cancellation from the owning request.
typedef struct AudioMediaLoadControl {
    bool (*cancelled)(void* user);
    void* user;
    size_t max_sample_bytes; // Each source/output buffer is bounded; zero preserves legacy unlimited behavior.
} AudioMediaLoadControl;
// Decodes privately with bounded buffers and cancellation; failure leaves output untouched.
bool audio_media_clip_load_controlled(const char* path, int rate, AudioMediaClip* out,
                                      const AudioMediaLoadControl* control);

// Probes metadata with the same request cancellation scope as background decoding.
bool audio_media_probe_controlled(const char* path, AudioMediaInfo* out, const AudioMediaLoadControl* control);
