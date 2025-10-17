#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t mask;
    _Atomic size_t head;
    _Atomic size_t tail;
} RingBuffer;

bool ringbuf_init(RingBuffer* rb, size_t capacity_power_of_two);
void ringbuf_free(RingBuffer* rb);
size_t ringbuf_write(RingBuffer* rb, const void* src, size_t bytes);
size_t ringbuf_read(RingBuffer* rb, void* dst, size_t bytes);
size_t ringbuf_available_write(const RingBuffer* rb);
size_t ringbuf_available_read(const RingBuffer* rb);
void ringbuf_reset(RingBuffer* rb);
