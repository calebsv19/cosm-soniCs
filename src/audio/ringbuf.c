#include "ringbuf.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static size_t next_power_of_two(size_t value) {
    if (value == 0) {
        return 1;
    }
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
#if SIZE_MAX > UINT32_MAX
    value |= value >> 32;
#endif
    return value + 1;
}

bool ringbuf_init(RingBuffer* rb, size_t capacity_power_of_two) {
    if (!rb || capacity_power_of_two == 0) {
        return false;
    }

    size_t capacity = next_power_of_two(capacity_power_of_two);
    uint8_t* data = (uint8_t*)malloc(capacity);
    if (!data) {
        return false;
    }

    rb->data = data;
    rb->capacity = capacity;
    rb->mask = capacity - 1;
    atomic_store_explicit(&rb->head, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->tail, 0, memory_order_relaxed);
    return true;
}

void ringbuf_free(RingBuffer* rb) {
    if (!rb) {
        return;
    }
    free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
    rb->mask = 0;
    atomic_store_explicit(&rb->head, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->tail, 0, memory_order_relaxed);
}

void ringbuf_reset(RingBuffer* rb) {
    if (!rb) {
        return;
    }
    atomic_store_explicit(&rb->head, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->tail, 0, memory_order_relaxed);
}

size_t ringbuf_available_write(const RingBuffer* rb) {
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return rb->capacity - (head - tail);
}

size_t ringbuf_available_read(const RingBuffer* rb) {
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return head - tail;
}

static size_t ringbuf_copy_in(RingBuffer* rb, const uint8_t* src, size_t bytes, size_t head) {
    size_t start = head & rb->mask;
    size_t first = rb->capacity - start;
    if (first > bytes) {
        first = bytes;
    }
    memcpy(rb->data + start, src, first);
    size_t remaining = bytes - first;
    if (remaining > 0) {
        memcpy(rb->data, src + first, remaining);
    }
    return bytes;
}

static size_t ringbuf_copy_out(RingBuffer* rb, uint8_t* dst, size_t bytes, size_t tail) {
    size_t start = tail & rb->mask;
    size_t first = rb->capacity - start;
    if (first > bytes) {
        first = bytes;
    }
    memcpy(dst, rb->data + start, first);
    size_t remaining = bytes - first;
    if (remaining > 0) {
        memcpy(dst + first, rb->data, remaining);
    }
    return bytes;
}

size_t ringbuf_write(RingBuffer* rb, const void* src, size_t bytes) {
    if (!rb || !rb->data || !src || bytes == 0) {
        return 0;
    }
    size_t available = ringbuf_available_write(rb);
    if (bytes > available) {
        bytes = available;
    }
    if (bytes == 0) {
        return 0;
    }
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    ringbuf_copy_in(rb, (const uint8_t*)src, bytes, head);
    atomic_store_explicit(&rb->head, head + bytes, memory_order_release);
    return bytes;
}

size_t ringbuf_read(RingBuffer* rb, void* dst, size_t bytes) {
    if (!rb || !rb->data || !dst || bytes == 0) {
        return 0;
    }
    size_t available = ringbuf_available_read(rb);
    if (bytes > available) {
        bytes = available;
    }
    if (bytes == 0) {
        return 0;
    }
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    ringbuf_copy_out(rb, (uint8_t*)dst, bytes, tail);
    atomic_store_explicit(&rb->tail, tail + bytes, memory_order_release);
    return bytes;
}
