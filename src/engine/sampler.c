#include "engine/sampler.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct EngineSamplerSource {
    const AudioMediaClip* clip;
    uint64_t start_frame;
    int channels;
};

static void sampler_reset_internal(EngineSamplerSource* sampler, int sample_rate, int channels) {
    if (!sampler) {
        return;
    }
    (void)sample_rate;
    sampler->channels = channels;
}

EngineSamplerSource* engine_sampler_source_create(void) {
    EngineSamplerSource* sampler = (EngineSamplerSource*)calloc(1, sizeof(EngineSamplerSource));
    if (!sampler) {
        return NULL;
    }
    sampler_reset_internal(sampler, 48000, 2);
    sampler->start_frame = 0;
    return sampler;
}

void engine_sampler_source_destroy(EngineSamplerSource* sampler) {
    free(sampler);
}

void engine_sampler_source_set_clip(EngineSamplerSource* sampler, const AudioMediaClip* clip, uint64_t start_frame) {
    if (!sampler) {
        return;
    }
    if (!clip) {
        sampler->clip = NULL;
        sampler->start_frame = 0;
        return;
    }
    sampler->clip = clip;
    sampler->start_frame = start_frame;
}

void engine_sampler_source_reset(void* userdata, int sample_rate, int channels) {
    sampler_reset_internal((EngineSamplerSource*)userdata, sample_rate, channels);
}

static float sample_from_clip(const AudioMediaClip* clip, uint64_t frame_index, int channel) {
    if (!clip) {
        return 0.0f;
    }
    int src_channel = channel;
    if (src_channel >= clip->channels) {
        src_channel = clip->channels - 1;
        if (src_channel < 0) {
            return 0.0f;
        }
    }
    if (frame_index >= clip->frame_count) {
        return 0.0f;
    }
    return clip->samples[frame_index * (uint64_t)clip->channels + (uint64_t)src_channel];
}

void engine_sampler_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineSamplerSource* sampler = (EngineSamplerSource*)userdata;
    if (!sampler || !interleaved || frames <= 0) {
        return;
    }
    const AudioMediaClip* clip = sampler->clip;
    for (int i = 0; i < frames; ++i) {
        uint64_t global_frame = transport_frame + (uint64_t)i;
        uint64_t local_frame = 0;
        bool in_range = false;
        if (clip) {
            if (global_frame >= sampler->start_frame) {
                local_frame = global_frame - sampler->start_frame;
                if (local_frame < clip->frame_count) {
                    in_range = true;
                }
            }
        }
        for (int ch = 0; ch < sampler->channels; ++ch) {
            float value = 0.0f;
            if (in_range) {
                value = sample_from_clip(clip, local_frame, ch);
            }
            interleaved[i * sampler->channels + ch] = value;
        }
    }
}

void engine_sampler_source_ops(EngineGraphSourceOps* ops) {
    if (!ops) {
        return;
    }
    ops->render = engine_sampler_source_render;
    ops->reset = engine_sampler_source_reset;
}

uint64_t engine_sampler_get_start_frame(const EngineSamplerSource* sampler) {
    if (!sampler) {
        return 0;
    }
    return sampler->start_frame;
}

uint64_t engine_sampler_get_frame_count(const EngineSamplerSource* sampler) {
    if (!sampler || !sampler->clip) {
        return 0;
    }
    return sampler->clip->frame_count;
}
