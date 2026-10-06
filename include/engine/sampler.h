#pragma once

#include <stdint.h>

#include "audio/media_clip.h"
#include "engine/automation.h"
#include "engine/graph.h"
#include "engine/fade_curve.h"

typedef struct EngineSamplerSource EngineSamplerSource;

EngineSamplerSource* engine_sampler_source_create(void);
// Copies sampler bounds and automation while borrowing the caller-pinned immutable media.
EngineSamplerSource* engine_sampler_source_clone(const EngineSamplerSource* source);
void engine_sampler_source_destroy(EngineSamplerSource* sampler);
void engine_sampler_source_set_clip(EngineSamplerSource* sampler, const AudioMediaClip* clip,
                                    uint64_t timeline_start_frame,
                                    uint64_t clip_offset_frames,
                                    uint64_t clip_length_frames,
                                    uint64_t fade_in_frames,
                                    uint64_t fade_out_frames);
// Updates the automation lanes for sampler evaluation.
// Replaces all automation atomically and reports allocation failure without changing the sampler.
bool engine_sampler_source_set_automation(EngineSamplerSource* sampler,
                                          const EngineAutomationLane* lanes,
                                          int lane_count);
void engine_sampler_source_reset(void* userdata, int sample_rate, int channels);
void engine_sampler_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_sampler_source_ops(EngineGraphSourceOps* ops);
uint64_t engine_sampler_get_start_frame(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_frame_count(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_offset_frames(const EngineSamplerSource* sampler);
const AudioMediaClip* engine_sampler_get_media(const EngineSamplerSource* sampler);
uint64_t engine_sampler_get_media_length(const EngineSamplerSource* sampler);

// Sets existing fade shapes on a control-owned or not-yet-published sampler.
void engine_sampler_source_set_fade_curves(EngineSamplerSource* sampler, EngineFadeCurve in_curve, EngineFadeCurve out_curve);
