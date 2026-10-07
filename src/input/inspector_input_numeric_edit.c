#include "ui/text_edit.h"
#include "input/inspector_input_numeric_edit.h"

#include "engine/audio_source.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline_selection.h"
#include "undo/undo_manager.h"
#include <float.h>

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
    kit_ui_text_cancel_composition(&state->inspector.edit.text_edit);
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
    if (!isfinite(value)) return false;
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
    state->inspector.edit.target_creation_index = clip->creation_index;
    inspector_format_numeric_field(state, clip, &state->inspector.edit);
    char* buffer = inspector_numeric_active_buffer(&state->inspector.edit);
    if (buffer) {
        state->inspector.edit.cursor = (int)strlen(buffer);
        size_t capacity = buffer == state->inspector.edit.playback_rate
            ? sizeof(state->inspector.edit.playback_rate) : sizeof(state->inspector.edit.timeline_start);
        daw_text_edit_begin(&state->inspector.edit.text_edit, buffer, capacity,
            &state->inspector.edit.cursor, KIT_UI_TEXT_SINGLE_LINE);
    }
    SDL_StartTextInput();
}

// Converts finite seconds without signed rounding overflow or undefined integer casts.
static bool inspector_seconds_to_frames(double seconds, double rate, uint64_t* frames) {
    if (!isfinite(seconds) || !isfinite(rate) || seconds < 0 || rate <= 0) return false;
    long double rounded = roundl((long double)seconds * (long double)rate);
    if (rounded < 0 || rounded >= ldexpl(1.0L, 64)) return false;
    *frames = (uint64_t)rounded;
    return true;
}

// Reserves complete history before applying a numeric edit and preserves input on rejection.
bool inspector_numeric_commit_edit(AppState* state) {
    if (!state || !state->engine || !inspector_numeric_is_editing(&state->inspector.edit)) return false;
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip || state->undo.active_drag_valid ||
        clip->creation_index != state->inspector.edit.target_creation_index) return false;
    char* buffer = inspector_numeric_active_buffer(&state->inspector.edit);
    double value = 0;
    if (!buffer || !inspector_parse_number(buffer, &value)) return false;
    if (state->inspector.edit.editing_playback_rate) {
        if (value <= 0.01 || value > FLT_MAX) return false;
        state->inspector.playback_rate = (float)value;
        inspector_numeric_clear_edit(state);
        SDL_StopTextInput();
        return true;
    }

    double sr = inspector_numeric_clip_sample_rate(state, clip);
    uint64_t frames = 0, offset = clip->offset_frames;
    bool position = state->inspector.edit.editing_timeline_start;
    if (position || state->inspector.edit.editing_source_start) {
        if (value < 0) value = 0;
    } else if (state->inspector.edit.editing_timeline_end) {
        value -= (double)clip->timeline_start_frames / sr;
    } else if (state->inspector.edit.editing_source_end) {
        value -= (double)clip->offset_frames / sr;
    }
    if (!inspector_seconds_to_frames(value, sr, &frames)) return false;
    uint64_t duration = frames;
    if (state->inspector.edit.editing_source_start) {
        offset = frames;
        duration = inspector_numeric_clip_duration_frames(state, clip);
        if (!duration) duration = 1;
    } else if (!position && (!frames || value <= 0)) return false;

    UndoCommand command = {.type = UNDO_CMD_CLIP_TRANSFORM};
    if (!undo_clip_state_capture(state->engine, clip, state->inspector.track_index,
                                 &command.data.clip_transform.before)) return false;
    bool prepared = undo_clip_state_clone(&command.data.clip_transform.after, &command.data.clip_transform.before) &&
                    undo_manager_begin_drag(&state->undo, &command);
    undo_clip_state_clear(&command.data.clip_transform.before);
    undo_clip_state_clear(&command.data.clip_transform.after);
    if (!prepared) return false;
    int track = state->inspector.track_index, old_index = state->inspector.clip_index, new_index = old_index;
    bool ok = position
        ? engine_clip_set_timeline_start(state->engine, track, old_index, frames, &new_index)
        : engine_clip_set_region(state->engine, track, old_index, offset, duration);
    if (!ok) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    // These setters preserve notes/instruments; update only their scalar readback without allocation.
    clip = (EngineClip*)&engine_get_tracks(state->engine)[track].clips[new_index];
    UndoClipState* after = &state->undo.active_drag.data.clip_transform.after;
    after->start_frame = clip->timeline_start_frames;
    after->offset_frames = clip->offset_frames;
    after->duration_frames = clip->duration_frames;
    after->fade_in_frames = clip->fade_in_frames;
    after->fade_out_frames = clip->fade_out_frames;
    (void)undo_manager_commit_drag(&state->undo, &state->undo.active_drag);
    timeline_selection_update_index(state, track, old_index, new_index);
    inspector_numeric_clear_edit(state);
    SDL_StopTextInput();
    inspector_input_set_clip(state, track, new_index);
    return true;
}
