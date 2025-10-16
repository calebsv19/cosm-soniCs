#pragma once

#include <stdint.h>

#include "audio/media_clip.h"
#include "engine/graph.h"

typedef struct EngineSamplerSource EngineSamplerSource;

EngineSamplerSource* engine_sampler_source_create(void);
void engine_sampler_source_destroy(EngineSamplerSource* sampler);
void engine_sampler_source_set_clip(EngineSamplerSource* sampler, const AudioMediaClip* clip,
                                    uint64_t timeline_start_frame,
                                    uint64_t clip_offset_frames,
                                    uint64_t clip_length_frames,
                                    uint64_t fade_in_frames,
                                    uint64_t fade_out_frames);
void engine_sampler_source_reset(void* userdata, int sample_rate, int channels);
void engine_sampler_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_sampler_source_ops(EngineGraphSourceOps* ops);
uint64_t engine_sampler_get_start_frame(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_frame_count(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_offset_frames(const EngineSamplerSource* sampler);
const AudioMediaClip* engine_sampler_get_media(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_media_length(const EngineSamplerSource* sampler);
