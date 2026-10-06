#include "audio/audio_queue.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

bool audio_queue_init(AudioQueue* queue, int channels, size_t capacity_frames) {
    if (!queue || channels <= 0 || capacity_frames == 0) {
        return false;
    }
    size_t capacity_bytes = capacity_frames * (size_t)channels * sizeof(float);
    if (!ringbuf_init(&queue->buffer, capacity_bytes)) {
        return false;
    }
    queue->channels = channels;
    queue->frame_stride_bytes = channels * (int)sizeof(float);
    atomic_init(&queue->flush_head, 0);
    return true;
}

void audio_queue_free(AudioQueue* queue) {
    if (!queue) {
        return;
    }
    ringbuf_free(&queue->buffer);
    queue->channels = 0;
    queue->frame_stride_bytes = 0;
}

size_t audio_queue_write(AudioQueue* queue, const float* interleaved, size_t frames) {
    if (!queue || !interleaved || frames == 0 || queue->frame_stride_bytes <= 0) {
        return 0;
    }
    size_t available = audio_queue_space_frames(queue);
    if (frames > available) frames = available;
    size_t bytes = frames * (size_t)queue->frame_stride_bytes;
    return ringbuf_write_exact(&queue->buffer, interleaved, bytes) ? frames : 0;
}

size_t audio_queue_read(AudioQueue* queue, float* interleaved, size_t frames) {
    if (!queue || !interleaved || frames == 0 || queue->frame_stride_bytes <= 0) {
        return 0;
    }
    size_t target = atomic_load_explicit(&queue->flush_head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&queue->buffer.tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&queue->buffer.head, memory_order_acquire);
    // A consumer may already have passed an older flush request; never rewind it.
    if (target - tail <= head - tail) {
        atomic_store_explicit(&queue->buffer.tail, target, memory_order_release);
    }
    size_t available = audio_queue_available_frames(queue);
    if (frames > available) frames = available;
    size_t bytes = frames * (size_t)queue->frame_stride_bytes;
    return ringbuf_read_exact(&queue->buffer, interleaved, bytes) ? frames : 0;
}

size_t audio_queue_available_frames(const AudioQueue* queue) {
    if (!queue || queue->frame_stride_bytes <= 0) {
        return 0;
    }
    size_t bytes = ringbuf_available_read(&queue->buffer);
    return bytes / (size_t)queue->frame_stride_bytes;
}

size_t audio_queue_space_frames(const AudioQueue* queue) {
    if (!queue || queue->frame_stride_bytes <= 0) {
        return 0;
    }
    size_t bytes = ringbuf_available_write(&queue->buffer);
    return bytes / (size_t)queue->frame_stride_bytes;
}

// Publishes a boundary for the sole consumer to discard at its next read.
void audio_queue_clear(AudioQueue* queue) {
    if (!queue) {
        return;
    }
    atomic_store_explicit(&queue->flush_head,
                          atomic_load_explicit(&queue->buffer.head, memory_order_acquire),
                          memory_order_release);
}
