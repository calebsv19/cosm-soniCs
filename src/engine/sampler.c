#include "engine/sampler.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Owns clip-local sample playback bounds, fade shapes, and automation.
struct EngineSamplerSource {
    const AudioMediaClip* clip;
    uint64_t timeline_start_frame;
    uint64_t clip_offset_frames;
    uint64_t clip_length_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve, fade_out_curve;
    int channels;
    SDL_mutex* automation_mutex;
    EngineAutomationLane* automation_lanes;
    int automation_lane_count;
    int automation_lane_capacity;
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
    sampler->automation_mutex = SDL_CreateMutex();
    sampler->automation_lanes = NULL;
    sampler->automation_lane_count = 0;
    sampler->automation_lane_capacity = 0;
    return sampler;
}

// Prepares an independent sampler so edits cannot mutate the worker's region or automation.
EngineSamplerSource* engine_sampler_source_clone(const EngineSamplerSource* source) {
    if (!source) return NULL;
    EngineSamplerSource* copy = engine_sampler_source_create();
    if (!copy) return NULL;
    copy->clip = source->clip;
    copy->timeline_start_frame = source->timeline_start_frame;
    copy->clip_offset_frames = source->clip_offset_frames;
    copy->clip_length_frames = source->clip_length_frames;
    copy->fade_in_frames = source->fade_in_frames;
    copy->fade_out_frames = source->fade_out_frames;
    copy->fade_in_curve = source->fade_in_curve;
    copy->fade_out_curve = source->fade_out_curve;
    copy->channels = source->channels;
    if (source->automation_lane_count > 0) {
        copy->automation_lanes = calloc((size_t)source->automation_lane_count, sizeof(EngineAutomationLane));
        if (!copy->automation_lanes) {
            engine_sampler_source_destroy(copy);
            return NULL;
        }
        copy->automation_lane_count = source->automation_lane_count;
        copy->automation_lane_capacity = source->automation_lane_count;
        for (int i = 0; i < source->automation_lane_count; ++i) {
            if (!engine_automation_lane_copy(&source->automation_lanes[i], &copy->automation_lanes[i])) {
                engine_sampler_source_destroy(copy);
                return NULL;
            }
        }
    }
    return copy;
}

void engine_sampler_source_destroy(EngineSamplerSource* sampler) {
    if (!sampler) {
        return;
    }
    if (sampler->automation_mutex) {
        SDL_LockMutex(sampler->automation_mutex);
    }
    if (sampler->automation_lanes) {
        for (int i = 0; i < sampler->automation_lane_count; ++i) {
            engine_automation_lane_free(&sampler->automation_lanes[i]);
        }
        free(sampler->automation_lanes);
        sampler->automation_lanes = NULL;
    }
    sampler->automation_lane_count = 0;
    sampler->automation_lane_capacity = 0;
    if (sampler->automation_mutex) {
        SDL_UnlockMutex(sampler->automation_mutex);
        SDL_DestroyMutex(sampler->automation_mutex);
        sampler->automation_mutex = NULL;
    }
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

// Replaces automation only after every lane has been copied successfully.
bool engine_sampler_source_set_automation(EngineSamplerSource* sampler,
                                          const EngineAutomationLane* lanes,
                                          int lane_count) {
    if (!sampler || lane_count < 0 || (lane_count > 0 && !lanes)) return false;
    EngineAutomationLane* replacement = NULL;
    if (lane_count > 0) {
        replacement = calloc((size_t)lane_count, sizeof(*replacement));
        if (!replacement) return false;
        for (int i = 0; i < lane_count; ++i) {
            if (!engine_automation_lane_copy(&lanes[i], &replacement[i])) {
                for (int j = 0; j < lane_count; ++j) engine_automation_lane_free(&replacement[j]);
                free(replacement);
                return false;
            }
        }
    }
    if (sampler->automation_mutex) SDL_LockMutex(sampler->automation_mutex);
    for (int i = 0; i < sampler->automation_lane_count; ++i)
        engine_automation_lane_free(&sampler->automation_lanes[i]);
    free(sampler->automation_lanes);
    sampler->automation_lanes = replacement;
    sampler->automation_lane_count = sampler->automation_lane_capacity = lane_count;
    if (sampler->automation_mutex) SDL_UnlockMutex(sampler->automation_mutex);
    return true;
}

static float sampler_eval_automation(const EngineSamplerSource* sampler,
                                     EngineAutomationTarget target,
                                     uint64_t rel_frame) {
    if (!sampler || !sampler->automation_lanes) {
        return 0.0f;
    }
    for (int i = 0; i < sampler->automation_lane_count; ++i) {
        const EngineAutomationLane* lane = &sampler->automation_lanes[i];
        if (lane->target == target) {
            return engine_automation_lane_eval(lane, rel_frame, sampler->clip_length_frames);
        }
    }
    return 0.0f;
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
    if (sampler->automation_mutex) {
        SDL_LockMutex(sampler->automation_mutex);
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
        float gain_scale = 1.0f;
        if (in_range) {
            if (sampler->fade_in_frames > 0 && rel < sampler->fade_in_frames) {
                gain_scale *= engine_fade_curve_eval(sampler->fade_in_curve, (float)rel / (float)sampler->fade_in_frames);
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
                    // Keep the historical linear sample endpoint convention exactly.
                    gain_scale *= sampler->fade_out_curve == ENGINE_FADE_CURVE_LINEAR
                        ? (float)remaining / (float)sampler->fade_out_frames
                        : 1.0f - engine_fade_curve_eval(sampler->fade_out_curve,
                            (float)(sampler->fade_out_frames - remaining) / (float)sampler->fade_out_frames);
                }
            }
        }
        for (int ch = 0; ch < sampler->channels; ++ch) {
            float value = 0.0f;
            if (in_range) {
                value = sample_from_clip(clip, local_frame, ch);
                float automation_gain = sampler_eval_automation(sampler,
                                                                ENGINE_AUTOMATION_TARGET_VOLUME,
                                                                rel);
                float gain_multiplier = 1.0f + automation_gain;
                if (gain_multiplier < 0.0f) {
                    gain_multiplier = 0.0f;
                }
                value *= gain_scale * gain_multiplier;
            }
            interleaved[i * sampler->channels + ch] = value;
        }
        if (in_range && sampler->channels >= 2) {
            float pan = sampler_eval_automation(sampler, ENGINE_AUTOMATION_TARGET_PAN, rel);
            if (pan < -1.0f) pan = -1.0f;
            if (pan > 1.0f) pan = 1.0f;
            if (pan != 0.0f) {
                int base = i * sampler->channels;
                float left = 1.0f;
                float right = 1.0f;
                if (pan < 0.0f) {
                    right = 1.0f + pan;
                } else {
                    left = 1.0f - pan;
                }
                interleaved[base] *= left;
                interleaved[base + 1] *= right;
            }
        }
    }
    if (sampler->automation_mutex) {
        SDL_UnlockMutex(sampler->automation_mutex);
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

// Copies accepted curve metadata without allocation or changes to region timing.
void engine_sampler_source_set_fade_curves(EngineSamplerSource* sampler, EngineFadeCurve in_curve, EngineFadeCurve out_curve) {
    if (!sampler) return;
    sampler->fade_in_curve = in_curve;
    sampler->fade_out_curve = out_curve;
}
