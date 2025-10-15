#include "engine/buffer_pool.h"

#include <stdlib.h>
#include <string.h>

bool engine_buffer_pool_init(EngineBufferPool* pool, int max_channels, int capacity_frames) {
    if (!pool || max_channels <= 0 || capacity_frames <= 0) {
        return false;
    }
    size_t samples = (size_t)max_channels * (size_t)capacity_frames;
    pool->storage = (float*)calloc(samples, sizeof(float));
    if (!pool->storage) {
        return false;
    }
    pool->channel_views = (float**)calloc((size_t)max_channels, sizeof(float*));
    if (!pool->channel_views) {
        free(pool->storage);
        pool->storage = NULL;
        return false;
    }
    pool->max_channels = max_channels;
    pool->capacity_frames = capacity_frames;
    pool->in_use = false;
    return true;
}

void engine_buffer_pool_free(EngineBufferPool* pool) {
    if (!pool) {
        return;
    }
    free(pool->storage);
    free(pool->channel_views);
    pool->storage = NULL;
    pool->channel_views = NULL;
    pool->max_channels = 0;
    pool->capacity_frames = 0;
    pool->in_use = false;
}

EngineBuffer engine_buffer_pool_acquire(EngineBufferPool* pool, int channels, int frames) {
    EngineBuffer buffer = {0};
    if (!pool || !pool->storage || !pool->channel_views) {
        return buffer;
    }
    if (pool->in_use) {
        return buffer;
    }
    if (channels > pool->max_channels) {
        channels = pool->max_channels;
    }
    if (frames > pool->capacity_frames) {
        frames = pool->capacity_frames;
    }
    for (int ch = 0; ch < channels; ++ch) {
        pool->channel_views[ch] = pool->storage + (size_t)ch * (size_t)pool->capacity_frames;
    }
    buffer.channel_data = pool->channel_views;
    buffer.num_channels = channels;
    buffer.num_frames = frames;
    pool->in_use = true;
    return buffer;
}

void engine_buffer_pool_release(EngineBufferPool* pool) {
    if (!pool) {
        return;
    }
    pool->in_use = false;
}
