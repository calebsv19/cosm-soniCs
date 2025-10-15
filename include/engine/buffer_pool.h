#pragma once

#include <stdbool.h>

#include "engine/buffer.h"

typedef struct EngineBufferPool {
    float* storage;
    float** channel_views;
    int max_channels;
    int capacity_frames;
    bool in_use;
} EngineBufferPool;

bool engine_buffer_pool_init(EngineBufferPool* pool, int max_channels, int capacity_frames);
void engine_buffer_pool_free(EngineBufferPool* pool);
EngineBuffer engine_buffer_pool_acquire(EngineBufferPool* pool, int channels, int frames);
void engine_buffer_pool_release(EngineBufferPool* pool);
