#pragma once

#include <stddef.h>

typedef struct {
    int sample_rate;
    int channels;
    double test_phase;
} EngineRenderState;

void engine_render_init(EngineRenderState* state, int sample_rate, int channels);
void engine_render_reset(EngineRenderState* state);
void engine_render_render(EngineRenderState* state, float* interleaved, size_t frames);
