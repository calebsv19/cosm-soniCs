#include "engine/sampler.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct EngineSamplerSource {
    const AudioMediaClip* clip;
    uint64_t timeline_start_frame;
    uint64_t clip_offset_frames;
    uint64_t clip_length_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
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
    sampler->timeline_start_frame = 0;
    sampler->clip_offset_frames = 0;
    sampler->clip_length_frames = 0;
    sampler->fade_in_frames = 0;
    sampler->fade_out_frames = 0;
    return sampler;
}

void engine_sampler_source_destroy(EngineSamplerSource* sampler) {
    free(sampler);
}

void engine_sampler_source_set_clip(EngineSamplerSource* sampler, const AudioMediaClip* clip,
                                    uint64_t timeline_start_frame,
                                    uint64_t clip_offset_frames,
                                    uint64_t clip_length_frames,
                                    uint64_t fade_in_frames,
                                    uint64_t fade_out_frames) {
    if (!sampler) {
        return;
    }
    if (!clip) {
        sampler->clip = NULL;
        sampler->timeline_start_frame = 0;
        sampler->clip_offset_frames = 0;
        sampler->clip_length_frames = 0;
        sampler->fade_in_frames = 0;
        sampler->fade_out_frames = 0;
        return;
    }
    sampler->clip = clip;
    sampler->timeline_start_frame = timeline_start_frame;
    uint64_t offset = clip_offset_frames;
    if (offset >= clip->frame_count) {
        offset = clip->frame_count > 0 ? clip->frame_count - 1 : 0;
    }
    sampler->clip_offset_frames = offset;
    uint64_t max_length = clip->frame_count - offset;
    if (clip_length_frames == 0 || clip_length_frames > max_length) {
        sampler->clip_length_frames = max_length;
    } else {
        sampler->clip_length_frames = clip_length_frames;
    }
    uint64_t effective_length = sampler->clip_length_frames;
    if (effective_length == 0) {
        effective_length = max_length;
    }
    if (fade_in_frames > effective_length) {
        fade_in_frames = effective_length;
    }
    if (fade_out_frames > effective_length) {
        fade_out_frames = effective_length;
    }
    sampler->fade_in_frames = fade_in_frames;
    sampler->fade_out_frames = fade_out_frames;
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
        uint64_t rel = 0;
        bool in_range = false;
        if (clip) {
            if (sampler->clip_length_frames > 0 &&
                global_frame >= sampler->timeline_start_frame) {
                rel = global_frame - sampler->timeline_start_frame;
                if (rel < sampler->clip_length_frames) {
                    local_frame = sampler->clip_offset_frames + rel;
                    if (local_frame < clip->frame_count) {
                        in_range = true;
                    }
                }
            }
        }
        for (int ch = 0; ch < sampler->channels; ++ch) {
            float value = 0.0f;
            if (in_range) {
                value = sample_from_clip(clip, local_frame, ch);
                float gain_scale = 1.0f;
                if (sampler->fade_in_frames > 0 && rel < sampler->fade_in_frames) {
                    gain_scale *= (float)rel / (float)sampler->fade_in_frames;
                }
                if (sampler->fade_out_frames > 0 && sampler->clip_length_frames > 0) {
                    uint64_t fade_start = sampler->clip_length_frames > sampler->fade_out_frames
                                              ? sampler->clip_length_frames - sampler->fade_out_frames
                                              : 0;
                    if (rel >= fade_start) {
                        uint64_t remaining = sampler->clip_length_frames - rel;
                        if (remaining > sampler->fade_out_frames) {
                            remaining = sampler->fade_out_frames;
                        }
                        gain_scale *= (float)remaining / (float)sampler->fade_out_frames;
                    }
                }
                value *= gain_scale;
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
    return sampler->timeline_start_frame;
}

uint64_t engine_sampler_get_frame_count(const EngineSamplerSource* sampler) {
    if (!sampler) {
        return 0;
    }
    return sampler->clip_length_frames;
}

uint64_t engine_sampler_get_offset_frames(const EngineSamplerSource* sampler) {
    if (!sampler) {
        return 0;
    }
    return sampler->clip_offset_frames;
}

const AudioMediaClip* engine_sampler_get_media(const EngineSamplerSource* sampler) {
    if (!sampler) {
        return NULL;
    }
    return sampler->clip;
}

uint64_t engine_sampler_get_media_length(const EngineSamplerSource* sampler) {
    if (!sampler || !sampler->clip) {
        return 0;
    }
    return sampler->clip->frame_count;
}
