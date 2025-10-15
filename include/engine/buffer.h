#pragma once

typedef struct EngineBuffer {
    float** channel_data;
    int num_channels;
    int num_frames;
} EngineBuffer;
