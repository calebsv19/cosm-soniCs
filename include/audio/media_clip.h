#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct AudioMediaClip {
    float* samples;         // Interleaved float32 samples
    uint64_t frame_count;
    int channels;
    int sample_rate;
} AudioMediaClip;

bool audio_media_clip_load_wav(const char* path, int target_sample_rate, AudioMediaClip* out_clip);
void audio_media_clip_free(AudioMediaClip* clip);
