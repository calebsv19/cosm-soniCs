#include "audio/audio_queue.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
    if (!queue || !interleaved || frames == 0) {
        return 0;
    }
    size_t bytes = frames * (size_t)queue->frame_stride_bytes;
    size_t written = ringbuf_write(&queue->buffer, interleaved, bytes);
    return written / (size_t)queue->frame_stride_bytes;
}

size_t audio_queue_read(AudioQueue* queue, float* interleaved, size_t frames) {
    if (!queue || !interleaved || frames == 0) {
        return 0;
    }
    size_t bytes = frames * (size_t)queue->frame_stride_bytes;
    size_t read = ringbuf_read(&queue->buffer, interleaved, bytes);
    return read / (size_t)queue->frame_stride_bytes;
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

void audio_queue_clear(AudioQueue* queue) {
    if (!queue) {
        return;
    }
    ringbuf_reset(&queue->buffer);
}
