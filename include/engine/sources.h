#pragma once

#include "engine/graph.h"

typedef struct EngineToneSource {
    double phase;
    int sample_rate;
    int channels;
} EngineToneSource;

EngineToneSource* engine_tone_source_create(void);
void engine_tone_source_destroy(EngineToneSource* tone);
void engine_tone_source_reset(void* userdata, int sample_rate, int channels);
void engine_tone_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_tone_source_ops(EngineGraphSourceOps* ops);
