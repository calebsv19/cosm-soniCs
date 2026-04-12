#include "input/inspector_input_numeric_edit.h"

#include "engine/audio_source.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const EngineRuntimeConfig* inspector_get_runtime_cfg(const AppState* state) {
    if (!state) {
        return NULL;
    }
    if (state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        if (cfg) {
            return cfg;
        }
    }
    return &state->runtime_cfg;
}

static EngineClip* inspector_get_clip_mutable(AppState* state) {
    if (!state || !state->engine) {
        return NULL;
    }
    EngineTrack* tracks = (EngineTrack*)engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        return NULL;
    }
    EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[state->inspector.clip_index];
}

bool inspector_numeric_is_editing(const ClipInspectorEditState* edit) {
    if (!edit) {
        return false;
    }
    return edit->editing_timeline_start || edit->editing_timeline_end || edit->editing_timeline_length ||
           edit->editing_source_start || edit->editing_source_end || edit->editing_playback_rate;
}

char* inspector_numeric_active_buffer(ClipInspectorEditState* edit) {
    if (!edit) {
        return NULL;
    }
    if (edit->editing_timeline_start) return edit->timeline_start;
    if (edit->editing_timeline_end) return edit->timeline_end;
    if (edit->editing_timeline_length) return edit->timeline_length;
    if (edit->editing_source_start) return edit->source_start;
    if (edit->editing_source_end) return edit->source_end;
    if (edit->editing_playback_rate) return edit->playback_rate;
    return NULL;
}

void inspector_numeric_clear_edit(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.edit.editing_timeline_start = false;
    state->inspector.edit.editing_timeline_end = false;
    state->inspector.edit.editing_timeline_length = false;
    state->inspector.edit.editing_source_start = false;
    state->inspector.edit.editing_source_end = false;
    state->inspector.edit.editing_playback_rate = false;
    state->inspector.edit.cursor = 0;
}

static bool inspector_parse_number(const char* text, double* out_value) {
    if (!text || !out_value) {
        return false;
    }
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0') {
        return false;
    }
    char* end = NULL;
    double value = strtod(text, &end);
    if (end == text) {
        return false;
    }
    while (end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end != '\0') {
        return false;
    }
    *out_value = value;
    return true;
}

double inspector_numeric_clip_sample_rate(const AppState* state, const EngineClip* clip) {
    if (clip && clip->media && clip->media->sample_rate > 0) {
        return (double)clip->media->sample_rate;
    }
    if (clip && clip->source && clip->source->sample_rate > 0) {
        return (double)clip->source->sample_rate;
    }
    const EngineRuntimeConfig* cfg = inspector_get_runtime_cfg(state);
    if (cfg && cfg->sample_rate > 0) {
        return (double)cfg->sample_rate;
    }
    return 48000.0;
}

uint64_t inspector_numeric_clip_total_frames(const AppState* state, const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    if (clip->media && clip->media->frame_count > 0) {
        return clip->media->frame_count;
    }
    if (state && state->engine) {
        return engine_clip_get_total_frames(state->engine, state->inspector.track_index, state->inspector.clip_index);
    }
    return 0;
}

uint64_t inspector_numeric_clip_duration_frames(const AppState* state, const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    uint64_t frames = clip->duration_frames;
    uint64_t total = inspector_numeric_clip_total_frames(state, clip);
    if (frames == 0 && total > clip->offset_frames) {
        frames = total - clip->offset_frames;
    }
    if (frames == 0 && clip->sampler) {
        frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return frames;
}

static void inspector_format_numeric_field(const AppState* state,
                                           const EngineClip* clip,
                                           ClipInspectorEditState* edit) {
    if (!state || !clip || !edit) {
        return;
    }
    double sr = inspector_numeric_clip_sample_rate(state, clip);
    uint64_t clip_frames = inspector_numeric_clip_duration_frames(state, clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }
    double timeline_start_sec = (double)clip->timeline_start_frames / sr;
    double timeline_length_sec = (double)clip_frames / sr;
    double timeline_end_sec = timeline_start_sec + timeline_length_sec;
    double source_start_sec = (double)clip->offset_frames / sr;
    double source_end_sec = source_start_sec + timeline_length_sec;
    uint64_t total_frames = inspector_numeric_clip_total_frames(state, clip);
    if (total_frames > 0) {
        double total_sec = (double)total_frames / sr;
        if (source_end_sec > total_sec) {
            source_end_sec = total_sec;
        }
    }

    snprintf(edit->timeline_start, sizeof(edit->timeline_start), "%.3f", timeline_start_sec);
    snprintf(edit->timeline_end, sizeof(edit->timeline_end), "%.3f", timeline_end_sec);
    snprintf(edit->timeline_length, sizeof(edit->timeline_length), "%.3f", timeline_length_sec);
    snprintf(edit->source_start, sizeof(edit->source_start), "%.3f", source_start_sec);
    snprintf(edit->source_end, sizeof(edit->source_end), "%.3f", source_end_sec);
    snprintf(edit->playback_rate, sizeof(edit->playback_rate), "%.2f",
             state->inspector.playback_rate > 0.0f ? state->inspector.playback_rate : 1.0f);
}

void inspector_numeric_begin_edit(AppState* state, const EngineClip* clip, bool* flag) {
    if (!state || !clip || !flag) {
        return;
    }
    inspector_numeric_clear_edit(state);
    *flag = true;
    inspector_format_numeric_field(state, clip, &state->inspector.edit);
    char* buffer = inspector_numeric_active_buffer(&state->inspector.edit);
    if (buffer) {
        state->inspector.edit.cursor = (int)strlen(buffer);
    }
    SDL_StartTextInput();
}

bool inspector_numeric_commit_edit(AppState* state) {
    if (!state || !state->engine) {
        return false;
    }
    if (!inspector_numeric_is_editing(&state->inspector.edit)) {
        return false;
    }
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip) {
        inspector_numeric_clear_edit(state);
        SDL_StopTextInput();
        return false;
    }

    char* buffer = inspector_numeric_active_buffer(&state->inspector.edit);
    double value = 0.0;
    if (!buffer || !inspector_parse_number(buffer, &value)) {
        inspector_format_numeric_field(state, clip, &state->inspector.edit);
        inspector_numeric_clear_edit(state);
        SDL_StopTextInput();
        return false;
    }

    double sr = inspector_numeric_clip_sample_rate(state, clip);
    uint64_t clip_frames = inspector_numeric_clip_duration_frames(state, clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }

    bool ok = false;
    if (state->inspector.edit.editing_timeline_start) {
        if (value < 0.0) value = 0.0;
        uint64_t frames = (uint64_t)llround(value * sr);
        ok = engine_clip_set_timeline_start(state->engine,
                                            state->inspector.track_index,
                                            state->inspector.clip_index,
                                            frames,
                                            NULL);
    } else if (state->inspector.edit.editing_timeline_end) {
        double start_sec = (double)clip->timeline_start_frames / sr;
        double length_sec = value - start_sec;
        if (length_sec > 0.0) {
            uint64_t duration_frames = (uint64_t)llround(length_sec * sr);
            ok = engine_clip_set_region(state->engine,
                                        state->inspector.track_index,
                                        state->inspector.clip_index,
                                        clip->offset_frames,
                                        duration_frames);
        }
    } else if (state->inspector.edit.editing_timeline_length) {
        if (value > 0.0) {
            uint64_t duration_frames = (uint64_t)llround(value * sr);
            ok = engine_clip_set_region(state->engine,
                                        state->inspector.track_index,
                                        state->inspector.clip_index,
                                        clip->offset_frames,
                                        duration_frames);
        }
    } else if (state->inspector.edit.editing_source_start) {
        if (value < 0.0) value = 0.0;
        uint64_t offset_frames = (uint64_t)llround(value * sr);
        ok = engine_clip_set_region(state->engine,
                                    state->inspector.track_index,
                                    state->inspector.clip_index,
                                    offset_frames,
                                    clip_frames);
    } else if (state->inspector.edit.editing_source_end) {
        double source_start_sec = (double)clip->offset_frames / sr;
        double length_sec = value - source_start_sec;
        if (length_sec > 0.0) {
            uint64_t duration_frames = (uint64_t)llround(length_sec * sr);
            ok = engine_clip_set_region(state->engine,
                                        state->inspector.track_index,
                                        state->inspector.clip_index,
                                        clip->offset_frames,
                                        duration_frames);
        }
    } else if (state->inspector.edit.editing_playback_rate) {
        if (value > 0.01) {
            state->inspector.playback_rate = (float)value;
            ok = true;
        }
    }

    inspector_numeric_clear_edit(state);
    SDL_StopTextInput();
    if (ok) {
        inspector_input_set_clip(state, state->inspector.track_index, state->inspector.clip_index);
    }
    return ok;
}
