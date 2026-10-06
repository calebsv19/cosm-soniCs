#pragma once

#include "engine/ringbuf.h"

#include <stdbool.h>

// Carries whole interleaved frames with producer-requested, consumer-owned flushing.
typedef struct {
    RingBuffer buffer;
    int channels;
    int frame_stride_bytes;
    _Atomic size_t flush_head;
} AudioQueue;

bool audio_queue_init(AudioQueue* queue, int channels, size_t capacity_frames);
void audio_queue_free(AudioQueue* queue);
size_t audio_queue_write(AudioQueue* queue, const float* interleaved, size_t frames);
size_t audio_queue_read(AudioQueue* queue, float* interleaved, size_t frames);
size_t audio_queue_available_frames(const AudioQueue* queue);
size_t audio_queue_space_frames(const AudioQueue* queue);
// Requests a flush through the producer's current head without modifying either live index.
void audio_queue_clear(AudioQueue* queue);
