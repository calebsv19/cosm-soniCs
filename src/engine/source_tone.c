#include "engine/sources.h"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void tone_reset_internal(EngineToneSource* tone, int sample_rate, int channels) {
    if (!tone) {
        return;
    }
    tone->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    tone->channels = channels > 0 ? channels : 2;
    tone->phase = 0.0;
}

EngineToneSource* engine_tone_source_create(void) {
    EngineToneSource* tone = (EngineToneSource*)calloc(1, sizeof(EngineToneSource));
    if (!tone) {
        return NULL;
    }
    tone_reset_internal(tone, 48000, 2);
    return tone;
}

void engine_tone_source_destroy(EngineToneSource* tone) {
    free(tone);
}

void engine_tone_source_reset(void* userdata, int sample_rate, int channels) {
    tone_reset_internal((EngineToneSource*)userdata, sample_rate, channels);
}

void engine_tone_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineToneSource* tone = (EngineToneSource*)userdata;
    if (!tone || !interleaved || frames <= 0) {
        return;
    }
    (void)transport_frame;
    double phase = tone->phase;
    const double phase_inc = (2.0 * M_PI * 440.0) / (double)tone->sample_rate;
    const int channels = tone->channels;

    for (int i = 0; i < frames; ++i) {
        float sample = (float)sin(phase);
        phase += phase_inc;
        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
        for (int ch = 0; ch < channels; ++ch) {
            interleaved[i * channels + ch] = sample;
        }
    }
    tone->phase = phase;
}

void engine_tone_source_ops(EngineGraphSourceOps* ops) {
    if (!ops) {
        return;
    }
    ops->render = engine_tone_source_render;
    ops->reset = engine_tone_source_reset;
}
