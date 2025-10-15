#include "engine_render.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void engine_render_init(EngineRenderState* state, int sample_rate, int channels) {
    if (!state) {
        return;
    }
    state->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    state->channels = channels > 0 ? channels : 2;
    state->test_phase = 0.0;
}

void engine_render_reset(EngineRenderState* state) {
    if (!state) {
        return;
    }
    state->test_phase = 0.0;
}

void engine_render_render(EngineRenderState* state, float* interleaved, size_t frames) {
    if (!state || !interleaved || frames == 0) {
        return;
    }
    double phase = state->test_phase;
    const double sample_rate = (double)state->sample_rate;
    const double frequency = 440.0;
    const double phase_inc = (2.0 * M_PI * frequency) / sample_rate;
    const int channels = state->channels;

    for (size_t i = 0; i < frames; ++i) {
        float sample = (float)sin(phase);
        phase += phase_inc;
        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
        for (int ch = 0; ch < channels; ++ch) {
            interleaved[i * (size_t)channels + (size_t)ch] = sample;
        }
    }
    state->test_phase = phase;
}
