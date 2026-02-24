#include "input/inspector_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/audio_source.h"
#include "engine/sampler.h"
#include "input/automation_input.h"
#include "input/input_manager.h"
#include "input/inspector_fade_input.h"
#include "input/timeline_snap.h"
#include "ui/clip_inspector.h"
#include "ui/font.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f

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

static const EngineClip* inspector_get_clip_const(const AppState* state) {
    if (!state || !state->engine) {
        return NULL;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        return NULL;
    }
    const EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[state->inspector.clip_index];
}

static bool clip_state_from_clip(const EngineClip* clip, int track_index, UndoClipState* out_state) {
    if (!clip || !out_state) {
        return false;
    }
    out_state->sampler = clip->sampler;
    out_state->track_index = track_index;
    out_state->start_frame = clip->timeline_start_frames;
    out_state->offset_frames = clip->offset_frames;
    out_state->duration_frames = clip->duration_frames;
    out_state->fade_in_frames = clip->fade_in_frames;
    out_state->fade_out_frames = clip->fade_out_frames;
    out_state->fade_in_curve = clip->fade_in_curve;
    out_state->fade_out_curve = clip->fade_out_curve;
    out_state->gain = clip->gain;
    if (out_state->duration_frames == 0 && clip->sampler) {
        out_state->duration_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return true;
}

static bool clip_state_equal(const UndoClipState* a, const UndoClipState* b) {
    if (!a || !b) {
        return true;
    }
    return a->track_index == b->track_index &&
           a->start_frame == b->start_frame &&
           a->offset_frames == b->offset_frames &&
           a->duration_frames == b->duration_frames &&
           a->fade_in_frames == b->fade_in_frames &&
           a->fade_out_frames == b->fade_out_frames &&
           a->fade_in_curve == b->fade_in_curve &&
           a->fade_out_curve == b->fade_out_curve &&
           fabsf(a->gain - b->gain) < 0.0001f;
}

static void inspector_begin_clip_drag(AppState* state) {
    if (!state) {
        return;
    }
    const EngineClip* clip = inspector_get_clip_const(state);
    if (!clip) {
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    if (!clip_state_from_clip(clip, state->inspector.track_index, &cmd.data.clip_transform.before)) {
        return;
    }
    cmd.data.clip_transform.after = cmd.data.clip_transform.before;
    undo_manager_begin_drag(&state->undo, &cmd);
}

static void inspector_stop_text_input(AppState* state) {
    if (!state) {
        return;
    }
    if (state->inspector.editing_name) {
        SDL_StopTextInput();
    }
    state->inspector.editing_name = false;
}

static int inspector_name_available_width(const AppState* state) {
    if (!state) {
        return 0;
    }
    ClipInspectorLayout layout;
    clip_inspector_compute_layout(state, &layout);
    int w = layout.name_rect.w - 12;
    if (w < 0) w = 0;
    return w;
}

static int inspector_name_visible_end(const char* text, int start, int available_width) {
    if (!text || available_width <= 0) {
        return start;
    }
    int len = (int)strlen(text);
    if (start < 0) start = 0;
    if (start > len) start = len;
    const char* ellipsis = "...";
    int ellipsis_w = ui_measure_text_width(ellipsis, 1.0f);
    int end = len;
    while (end > start) {
        int left_w = start > 0 ? ellipsis_w : 0;
        int right_w = end < len ? ellipsis_w : 0;
        char scratch[ENGINE_CLIP_NAME_MAX];
        int count = end - start;
        if (count >= (int)sizeof(scratch)) count = (int)sizeof(scratch) - 1;
        memcpy(scratch, text + start, (size_t)count);
        scratch[count] = '\0';
        int text_w = ui_measure_text_width(scratch, 1.0f);
        if (left_w + text_w + right_w <= available_width) {
            break;
        }
        end--;
    }
    return end;
}

static void inspector_update_name_scroll(AppState* state) {
    if (!state) {
        return;
    }
    int available_width = inspector_name_available_width(state);
    if (available_width <= 0) {
        state->inspector.name_scroll = 0;
        return;
    }
    const char* text = state->inspector.name;
    int len = (int)strlen(text);
    int cursor = state->inspector.name_cursor;
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;

    int start = state->inspector.name_scroll;
    if (start < 0) start = 0;
    if (start > len) start = len;
    int end = inspector_name_visible_end(text, start, available_width);
    int visible_len = end - start;
    int page_chars = visible_len;
    if (page_chars < CLIP_INSPECTOR_NAME_MIN_VISIBLE_CHARS) {
        page_chars = CLIP_INSPECTOR_NAME_MIN_VISIBLE_CHARS;
    }
    if (page_chars < 1) {
        page_chars = 1;
    }

    if (visible_len <= 0 || cursor < start || cursor > end) {
        if (cursor < start) {
            start = cursor - (page_chars - 1);
        } else if (cursor > end) {
            start = cursor;
        } else {
            start = cursor;
            if (start > 0) start--;
        }
        if (start < 0) start = 0;
        if (start > len) start = len;
    }

    end = inspector_name_visible_end(text, start, available_width);
    while (cursor > end && start > 0) {
        start--;
        end = inspector_name_visible_end(text, start, available_width);
    }
    visible_len = end - start;
    while (start > 0 && visible_len < CLIP_INSPECTOR_NAME_MIN_VISIBLE_CHARS) {
        start--;
        end = inspector_name_visible_end(text, start, available_width);
        visible_len = end - start;
    }
    state->inspector.name_scroll = start;
}

static void inspector_clear_numeric_edit(AppState* state) {
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

static bool inspector_is_numeric_editing(const ClipInspectorEditState* edit) {
    if (!edit) {
        return false;
    }
    return edit->editing_timeline_start || edit->editing_timeline_end || edit->editing_timeline_length ||
           edit->editing_source_start || edit->editing_source_end || edit->editing_playback_rate;
}

static char* inspector_active_numeric_buffer(ClipInspectorEditState* edit) {
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

static double inspector_clip_sample_rate(const AppState* state, const EngineClip* clip) {
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

static uint64_t inspector_clip_total_frames(const AppState* state, const EngineClip* clip) {
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

static uint64_t inspector_clip_duration_frames(const AppState* state, const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    uint64_t frames = clip->duration_frames;
    uint64_t total = inspector_clip_total_frames(state, clip);
    if (frames == 0 && total > clip->offset_frames) {
        frames = total - clip->offset_frames;
    }
    if (frames == 0 && clip->sampler) {
        frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return frames;
}

// Converts a mouse y position into an automation value in -1..1.
static float inspector_automation_value_from_y(const SDL_Rect* rect, int y) {
    if (!rect || rect->h <= 0) {
        return 0.0f;
    }
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    float value = (float)(baseline - y) / (float)range;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

// Finds an automation point under the cursor in inspector coordinates.
static int inspector_automation_hit_point(const EngineAutomationLane* lane,
                                          const SDL_Rect* rect,
                                          uint64_t view_start,
                                          uint64_t view_frames,
                                          uint64_t clip_start,
                                          uint64_t clip_frames,
                                          int x,
                                          int y) {
    if (!lane || !rect || rect->w <= 0 || rect->h <= 0 || view_frames == 0 || clip_frames == 0) {
        return -1;
    }
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    const int radius = 6;
    for (int i = 0; i < lane->point_count; ++i) {
        uint64_t abs_frame = clip_start + lane->points[i].frame;
        if (abs_frame > clip_start + clip_frames) {
            abs_frame = clip_start + clip_frames;
        }
        if (abs_frame < view_start || abs_frame > view_start + view_frames) {
            continue;
        }
        double t = (double)(abs_frame - view_start) / (double)view_frames;
        int px = rect->x + (int)llround(t * (double)rect->w);
        int py = baseline - (int)llround((double)lane->points[i].value * (double)range);
        if (abs(px - x) <= radius && abs(py - y) <= radius) {
            return i;
        }
    }
    return -1;
}

static void inspector_format_numeric_field(const AppState* state,
                                           const EngineClip* clip,
                                           ClipInspectorEditState* edit) {
    if (!state || !clip || !edit) {
        return;
    }
    double sr = inspector_clip_sample_rate(state, clip);
    uint64_t clip_frames = inspector_clip_duration_frames(state, clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }
    double timeline_start_sec = (double)clip->timeline_start_frames / sr;
    double timeline_length_sec = (double)clip_frames / sr;
    double timeline_end_sec = timeline_start_sec + timeline_length_sec;
    double source_start_sec = (double)clip->offset_frames / sr;
    double source_end_sec = source_start_sec + timeline_length_sec;
    uint64_t total_frames = inspector_clip_total_frames(state, clip);
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

static void inspector_begin_numeric_edit(AppState* state, const EngineClip* clip, bool* flag) {
    if (!state || !clip || !flag) {
        return;
    }
    inspector_clear_numeric_edit(state);
    *flag = true;
    inspector_format_numeric_field(state, clip, &state->inspector.edit);
    char* buffer = inspector_active_numeric_buffer(&state->inspector.edit);
    if (buffer) {
        state->inspector.edit.cursor = (int)strlen(buffer);
    }
    SDL_StartTextInput();
}

static bool inspector_commit_numeric_edit(AppState* state) {
    if (!state || !state->engine) {
        return false;
    }
    if (!inspector_is_numeric_editing(&state->inspector.edit)) {
        return false;
    }
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip) {
        inspector_clear_numeric_edit(state);
        SDL_StopTextInput();
        return false;
    }

    char* buffer = inspector_active_numeric_buffer(&state->inspector.edit);
    double value = 0.0;
    if (!buffer || !inspector_parse_number(buffer, &value)) {
        inspector_format_numeric_field(state, clip, &state->inspector.edit);
        inspector_clear_numeric_edit(state);
        SDL_StopTextInput();
        return false;
    }

    double sr = inspector_clip_sample_rate(state, clip);
    uint64_t clip_frames = inspector_clip_duration_frames(state, clip);
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

    inspector_clear_numeric_edit(state);
    SDL_StopTextInput();
    if (ok) {
        inspector_input_set_clip(state, state->inspector.track_index, state->inspector.clip_index);
    }
    return ok;
}

static void inspector_update_gain(AppState* state, float new_gain) {
    if (!state || !state->engine) {
        return;
    }
    if (new_gain < INSPECTOR_GAIN_MIN) new_gain = INSPECTOR_GAIN_MIN;
    if (new_gain > INSPECTOR_GAIN_MAX) new_gain = INSPECTOR_GAIN_MAX;
    state->inspector.gain = new_gain;
    engine_clip_set_gain(state->engine, state->inspector.track_index, state->inspector.clip_index, new_gain);
}

static void inspector_update_gain_from_mouse(AppState* state, int mouse_x) {
    if (!state) {
        return;
    }
    ClipInspectorLayout layout;
    clip_inspector_compute_layout(state, &layout);
    SDL_Rect track = layout.gain_track_rect;
    if (track.w <= 0) {
        return;
    }
    float t = (float)(mouse_x - track.x) / (float)track.w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float gain = INSPECTOR_GAIN_MIN + t * (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN);
    inspector_update_gain(state, gain);
}

void inspector_input_init(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.visible = false;
    state->inspector.track_index = -1;
    state->inspector.clip_index = -1;
    state->inspector.name[0] = '\0';
    state->inspector.name_scroll = 0;
    state->inspector.gain = 1.0f;
    state->inspector.playback_rate = 1.0f;
    state->inspector.editing_name = false;
    state->inspector.name_cursor = 0;
    state->inspector.adjusting_gain = false;
    inspector_fade_input_init(state);
    state->inspector.has_focus = false;
    state->inspector.phase_invert_l = false;
    state->inspector.phase_invert_r = false;
    state->inspector.normalize = false;
    state->inspector.reverse = false;
    SDL_zero(state->inspector.edit);
    state->inspector.waveform.zoom = 1.0f;
    state->inspector.waveform.scroll = 0.0f;
    state->inspector.waveform.dragging_window = false;
    state->inspector.waveform.trimming_left = false;
    state->inspector.waveform.trimming_right = false;
    state->inspector.waveform.view_source = true;
    state->inspector.waveform.use_kit_viz_waveform = true;
}

void inspector_input_show(AppState* state, int track_index, int clip_index, const EngineClip* clip) {
    if (!state || !clip) {
        return;
    }
    state->inspector.visible = true;
    state->inspector.track_index = track_index;
    state->inspector.clip_index = clip_index;
    strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
    state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
    state->inspector.name_scroll = 0;
    state->inspector.gain = clip->gain;
    state->inspector.playback_rate = 1.0f;
    state->inspector.editing_name = false;
    state->inspector.name_cursor = (int)strlen(state->inspector.name);
    state->inspector.adjusting_gain = false;
    inspector_fade_input_show(state, clip);
    state->inspector.has_focus = true;
    state->inspector.phase_invert_l = false;
    state->inspector.phase_invert_r = false;
    state->inspector.normalize = false;
    state->inspector.reverse = false;
    SDL_zero(state->inspector.edit);
    state->inspector.waveform.zoom = 1.0f;
    state->inspector.waveform.scroll = 0.0f;
    state->inspector.waveform.dragging_window = false;
    state->inspector.waveform.trimming_left = false;
    state->inspector.waveform.trimming_right = false;
    state->inspector.waveform.view_source = true;
    state->inspector.waveform.use_kit_viz_waveform = true;
    SDL_StopTextInput();
}

void inspector_input_set_clip(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->inspector.track_index = track_index;
    state->inspector.clip_index = clip_index;
    const EngineClip* clip = inspector_get_clip_const(state);
    if (clip) {
        inspector_fade_input_set_clip(state, clip);
    }
}

void inspector_input_begin_rename(AppState* state) {
    if (!state) {
        return;
    }
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip) {
        return;
    }
    inspector_clear_numeric_edit(state);
    strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
    state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
    state->inspector.name_cursor = (int)strlen(state->inspector.name);
    state->inspector.editing_name = true;
    state->inspector.name_scroll = 0;
    inspector_update_name_scroll(state);
    SDL_StartTextInput();
}

void inspector_input_commit_if_editing(AppState* state) {
    if (!state) {
        return;
    }
    if (state->inspector.editing_name) {
        EngineClip* clip = inspector_get_clip_mutable(state);
        if (clip && state->engine) {
            if (strncmp(clip->name, state->inspector.name, sizeof(clip->name)) != 0) {
                UndoCommand cmd = {0};
                cmd.type = UNDO_CMD_CLIP_RENAME;
                cmd.data.clip_rename.sampler = clip->sampler;
                cmd.data.clip_rename.track_index = state->inspector.track_index;
                strncpy(cmd.data.clip_rename.before_name, clip->name, sizeof(cmd.data.clip_rename.before_name) - 1);
                cmd.data.clip_rename.before_name[sizeof(cmd.data.clip_rename.before_name) - 1] = '\0';
                strncpy(cmd.data.clip_rename.after_name, state->inspector.name, sizeof(cmd.data.clip_rename.after_name) - 1);
                cmd.data.clip_rename.after_name[sizeof(cmd.data.clip_rename.after_name) - 1] = '\0';
                engine_clip_set_name(state->engine, state->inspector.track_index, state->inspector.clip_index, state->inspector.name);
                undo_manager_push(&state->undo, &cmd);
            } else {
                engine_clip_set_name(state->engine, state->inspector.track_index, state->inspector.clip_index, state->inspector.name);
            }
        }
        inspector_stop_text_input(state);
    }
    if (inspector_is_numeric_editing(&state->inspector.edit)) {
        inspector_commit_numeric_edit(state);
    }
}

void inspector_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event) {
        return;
    }

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT && state->inspector.visible) {
            ClipInspectorLayout layout;
            clip_inspector_compute_layout(state, &layout);
            SDL_Point p = {event->button.x, event->button.y};
            SDL_Keymod mods = SDL_GetModState();
            bool shift_held = (mods & KMOD_SHIFT) != 0;
            bool alt_held = (mods & KMOD_ALT) != 0;
            state->inspector.has_focus = SDL_PointInRect(&p, &layout.panel_rect);
            if (state->inspector.has_focus) {
                if (SDL_PointInRect(&p, &layout.right_mode_source_rect)) {
                    state->inspector.waveform.view_source = true;
                    inspector_input_commit_if_editing(state);
                    return;
                }
                if (SDL_PointInRect(&p, &layout.right_mode_clip_rect)) {
                    state->inspector.waveform.view_source = false;
                    inspector_input_commit_if_editing(state);
                    return;
                }

                if (state->timeline_automation_mode && SDL_PointInRect(&p, &layout.right_waveform_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        uint64_t clip_frames = inspector_clip_duration_frames(state, clip);
                        if (clip_frames == 0) {
                            clip_frames = 1;
                        }
                        uint64_t view_start = 0;
                        uint64_t view_frames = 0;
                        if (clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames) && view_frames > 0) {
                            SDL_Rect rect = layout.right_waveform_rect;
                            int clamped_x = p.x;
                            if (clamped_x < rect.x) clamped_x = rect.x;
                            if (clamped_x > rect.x + rect.w) clamped_x = rect.x + rect.w;
                            double t = (double)(clamped_x - rect.x) / (double)rect.w;
                            if (t < 0.0) t = 0.0;
                            if (t > 1.0) t = 1.0;
                            uint64_t frame_at = view_start + (uint64_t)llround(t * (double)view_frames);
                            double sr = inspector_clip_sample_rate(state, clip);
                            float seconds = (float)((double)frame_at / sr);
                            seconds = timeline_snap_seconds_to_grid(state, seconds, state->timeline_visible_seconds);
                            frame_at = (uint64_t)llround((double)seconds * sr);
                            uint64_t clip_start = clip->offset_frames;
                            uint64_t rel_frame = frame_at > clip_start ? frame_at - clip_start : 0;
                            if (rel_frame > clip_frames) {
                                rel_frame = clip_frames;
                            }
                            const EngineAutomationLane* lane = NULL;
                            automation_begin_edit(state, state->inspector.track_index, state->inspector.clip_index);
                            engine_clip_get_automation_lane(state->engine,
                                                            state->inspector.track_index,
                                                            state->inspector.clip_index,
                                                            state->automation_ui.target,
                                                            &lane);
                            int hit_point = inspector_automation_hit_point(lane,
                                                                           &rect,
                                                                           view_start,
                                                                           view_frames,
                                                                           clip_start,
                                                                           clip_frames,
                                                                           p.x,
                                                                           p.y);
                            float value = inspector_automation_value_from_y(&rect, p.y);
                            if (hit_point >= 0) {
                                state->automation_ui.track_index = state->inspector.track_index;
                                state->automation_ui.clip_index = state->inspector.clip_index;
                                state->automation_ui.point_index = hit_point;
                            } else {
                                int new_index = -1;
                                engine_clip_add_automation_point(state->engine,
                                                                 state->inspector.track_index,
                                                                 state->inspector.clip_index,
                                                                 state->automation_ui.target,
                                                                 rel_frame,
                                                                 value,
                                                                 &new_index);
                                state->automation_ui.track_index = state->inspector.track_index;
                                state->automation_ui.clip_index = state->inspector.clip_index;
                                state->automation_ui.point_index = new_index;
                            }
                            state->automation_ui.dragging = true;
                            state->automation_ui.dragging_from_inspector = true;
                            inspector_input_commit_if_editing(state);
                            return;
                        }
                    }
                }

                if (inspector_fade_input_handle_waveform_mouse_down(state, &layout, &p, shift_held, alt_held)) {
                    return;
                }

                if (SDL_PointInRect(&p, &layout.name_rect) ||
                    SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_NAME].label_rect) ||
                    SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_NAME].value_rect)) {
                    inspector_input_begin_rename(state);
                } else {
                    inspector_input_commit_if_editing(state);
                }

                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_TIMELINE_START].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_timeline_start);
                    }
                    return;
                }
                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_TIMELINE_END].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_timeline_end);
                    }
                    return;
                }
                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_timeline_length);
                    }
                    return;
                }
                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_SOURCE_START].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_source_start);
                    }
                    return;
                }
                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_SOURCE_END].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_source_end);
                    }
                    return;
                }
                if (SDL_PointInRect(&p, &layout.rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE].value_rect)) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        inspector_stop_text_input(state);
                        inspector_begin_numeric_edit(state, clip, &state->inspector.edit.editing_playback_rate);
                    }
                    return;
                }

                if (layout.rows[CLIP_INSPECTOR_ROW_PHASE].value_rect.w > 0) {
                    SDL_Rect phase_rect = layout.rows[CLIP_INSPECTOR_ROW_PHASE].value_rect;
                    int button_w = (phase_rect.w - 6) / 2;
                    int button_h = phase_rect.h - 4;
                    if (button_w < 0) button_w = 0;
                    SDL_Rect left_btn = {phase_rect.x, phase_rect.y + 2, button_w, button_h};
                    SDL_Rect right_btn = {phase_rect.x + button_w + 6, phase_rect.y + 2, button_w, button_h};
                    if (SDL_PointInRect(&p, &left_btn)) {
                        state->inspector.phase_invert_l = !state->inspector.phase_invert_l;
                        return;
                    }
                    if (SDL_PointInRect(&p, &right_btn)) {
                        state->inspector.phase_invert_r = !state->inspector.phase_invert_r;
                        return;
                    }
                }
                if (layout.rows[CLIP_INSPECTOR_ROW_NORMALIZE].value_rect.w > 0) {
                    SDL_Rect toggle_rect = layout.rows[CLIP_INSPECTOR_ROW_NORMALIZE].value_rect;
                    toggle_rect.h -= 4;
                    toggle_rect.y += 2;
                    toggle_rect.w = 52;
                    if (SDL_PointInRect(&p, &toggle_rect)) {
                        state->inspector.normalize = !state->inspector.normalize;
                        return;
                    }
                }
                if (layout.rows[CLIP_INSPECTOR_ROW_REVERSE].value_rect.w > 0) {
                    SDL_Rect toggle_rect = layout.rows[CLIP_INSPECTOR_ROW_REVERSE].value_rect;
                    toggle_rect.h -= 4;
                    toggle_rect.y += 2;
                    toggle_rect.w = 52;
                    if (SDL_PointInRect(&p, &toggle_rect)) {
                        state->inspector.reverse = !state->inspector.reverse;
                        return;
                    }
                }

                if (SDL_PointInRect(&p, &layout.gain_track_rect)) {
                    state->inspector.adjusting_gain = true;
                    inspector_begin_clip_drag(state);
                    inspector_update_gain_from_mouse(state, p.x);
                    return;
                }

                if (inspector_fade_input_handle_track_mouse_down(state, &layout, &p, shift_held, true)) {
                    return;
                }
                if (inspector_fade_input_handle_track_mouse_down(state, &layout, &p, shift_held, false)) {
                    return;
                }
            }
            if (!state->inspector.has_focus) {
                inspector_input_commit_if_editing(state);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            state->inspector.adjusting_gain = false;
            inspector_fade_input_handle_mouse_up(state);
            if (state->automation_ui.dragging_from_inspector) {
                state->automation_ui.dragging = false;
                state->automation_ui.dragging_from_inspector = false;
                automation_commit_edit(state);
            }
            if (state->undo.active_drag_valid) {
                UndoCommand* cmd = &state->undo.active_drag;
                if (cmd->type == UNDO_CMD_CLIP_TRANSFORM) {
                    const EngineClip* clip = inspector_get_clip_const(state);
                    if (clip) {
                        UndoClipState after = {0};
                        if (clip_state_from_clip(clip, state->inspector.track_index, &after)) {
                            cmd->data.clip_transform.after = after;
                            cmd->data.clip_transform.before.sampler = after.sampler;
                            if (!clip_state_equal(&cmd->data.clip_transform.before, &after)) {
                                undo_manager_commit_drag(&state->undo, cmd);
                                return;
                            }
                        }
                    }
                }
                undo_manager_cancel_drag(&state->undo);
            }
        }
        break;
    case SDL_MOUSEMOTION:
        if (state->automation_ui.dragging && state->automation_ui.dragging_from_inspector) {
            const EngineClip* clip = inspector_get_clip_const(state);
            if (clip) {
                uint64_t clip_frames = inspector_clip_duration_frames(state, clip);
                if (clip_frames == 0) {
                    clip_frames = 1;
                }
                uint64_t view_start = 0;
                uint64_t view_frames = 0;
                if (clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames) && view_frames > 0) {
                    ClipInspectorLayout layout;
                    clip_inspector_compute_layout(state, &layout);
                    SDL_Rect rect = layout.right_waveform_rect;
                    int clamped_x = event->motion.x;
                    if (clamped_x < rect.x) clamped_x = rect.x;
                    if (clamped_x > rect.x + rect.w) clamped_x = rect.x + rect.w;
                    double t = (double)(clamped_x - rect.x) / (double)rect.w;
                    if (t < 0.0) t = 0.0;
                    if (t > 1.0) t = 1.0;
                    uint64_t frame_at = view_start + (uint64_t)llround(t * (double)view_frames);
                    double sr = inspector_clip_sample_rate(state, clip);
                    float seconds = (float)((double)frame_at / sr);
                    seconds = timeline_snap_seconds_to_grid(state, seconds, state->timeline_visible_seconds);
                    frame_at = (uint64_t)llround((double)seconds * sr);
                    uint64_t clip_start = clip->offset_frames;
                    uint64_t rel_frame = frame_at > clip_start ? frame_at - clip_start : 0;
                    if (rel_frame > clip_frames) {
                        rel_frame = clip_frames;
                    }
                    float value = inspector_automation_value_from_y(&rect, event->motion.y);
                    int new_index = state->automation_ui.point_index;
                    engine_clip_update_automation_point(state->engine,
                                                        state->inspector.track_index,
                                                        state->inspector.clip_index,
                                                        state->automation_ui.target,
                                                        state->automation_ui.point_index,
                                                        rel_frame,
                                                        value,
                                                        &new_index);
                    state->automation_ui.point_index = new_index;
                }
            }
        } else if (inspector_fade_input_handle_pending_drag(state, event->motion.x)) {
            break;
        } else if (state->inspector.adjusting_gain) {
            inspector_update_gain_from_mouse(state, event->motion.x);
        } else if (inspector_fade_input_handle_active_drag(state, event->motion.x)) {
            break;
        }
        break;
    case SDL_TEXTINPUT:
        if (state->inspector.editing_name) {
            size_t current_len = strlen(state->inspector.name);
            size_t incoming = strlen(event->text.text);
            size_t max_len = sizeof(state->inspector.name) - 1;
            if (incoming > 0 && current_len < max_len) {
                size_t copy = incoming;
                if (current_len + copy > max_len) {
                    copy = max_len - current_len;
                }
                int cursor = state->inspector.name_cursor;
                if (cursor < 0) cursor = 0;
                if (cursor > (int)current_len) cursor = (int)current_len;
                memmove(state->inspector.name + cursor + (int)copy,
                        state->inspector.name + cursor,
                        current_len - (size_t)cursor + 1);
                memcpy(state->inspector.name + cursor, event->text.text, copy);
                state->inspector.name_cursor = cursor + (int)copy;
                inspector_update_name_scroll(state);
            }
        } else if (inspector_is_numeric_editing(&state->inspector.edit)) {
            char* buffer = inspector_active_numeric_buffer(&state->inspector.edit);
            if (buffer) {
                size_t current_len = strlen(buffer);
                size_t incoming = strlen(event->text.text);
                size_t max_len = 0;
                if (buffer == state->inspector.edit.playback_rate) {
                    max_len = sizeof(state->inspector.edit.playback_rate) - 1;
                } else if (buffer == state->inspector.edit.timeline_start) {
                    max_len = sizeof(state->inspector.edit.timeline_start) - 1;
                } else if (buffer == state->inspector.edit.timeline_end) {
                    max_len = sizeof(state->inspector.edit.timeline_end) - 1;
                } else if (buffer == state->inspector.edit.timeline_length) {
                    max_len = sizeof(state->inspector.edit.timeline_length) - 1;
                } else if (buffer == state->inspector.edit.source_start) {
                    max_len = sizeof(state->inspector.edit.source_start) - 1;
                } else {
                    max_len = sizeof(state->inspector.edit.source_end) - 1;
                }
                if (incoming > 0 && current_len < max_len) {
                    size_t copy = incoming;
                    if (current_len + copy > max_len) {
                        copy = max_len - current_len;
                    }
                    strncat(buffer, event->text.text, copy);
                    state->inspector.edit.cursor = (int)strlen(buffer);
                }
            }
        }
        break;
    case SDL_KEYDOWN:
        if (state->inspector.editing_name) {
            SDL_Keycode key = event->key.keysym.sym;
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                inspector_input_commit_if_editing(state);
            } else if (key == SDLK_ESCAPE) {
                const EngineClip* clip = inspector_get_clip_const(state);
                if (clip) {
                    strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
                    state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
                } else {
                    state->inspector.name[0] = '\0';
                }
                state->inspector.name_cursor = (int)strlen(state->inspector.name);
                state->inspector.name_scroll = state->inspector.name_cursor;
                inspector_update_name_scroll(state);
                inspector_stop_text_input(state);
            } else if (key == SDLK_BACKSPACE) {
                size_t len = strlen(state->inspector.name);
                int cursor = state->inspector.name_cursor;
                if (cursor > 0 && cursor <= (int)len) {
                    memmove(state->inspector.name + cursor - 1,
                            state->inspector.name + cursor,
                            len - (size_t)cursor + 1);
                    state->inspector.name_cursor = cursor - 1;
                    inspector_update_name_scroll(state);
                }
            } else if (key == SDLK_DELETE) {
                size_t len = strlen(state->inspector.name);
                int cursor = state->inspector.name_cursor;
                if (cursor >= 0 && cursor < (int)len) {
                    memmove(state->inspector.name + cursor,
                            state->inspector.name + cursor + 1,
                            len - (size_t)cursor);
                    inspector_update_name_scroll(state);
                }
            } else if (key == SDLK_LEFT) {
                if (state->inspector.name_cursor > 0) {
                    state->inspector.name_cursor -= 1;
                    inspector_update_name_scroll(state);
                }
            } else if (key == SDLK_RIGHT) {
                int len = (int)strlen(state->inspector.name);
                if (state->inspector.name_cursor < len) {
                    state->inspector.name_cursor += 1;
                    inspector_update_name_scroll(state);
                }
            }
        } else if (inspector_is_numeric_editing(&state->inspector.edit)) {
            SDL_Keycode key = event->key.keysym.sym;
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                inspector_commit_numeric_edit(state);
            } else if (key == SDLK_ESCAPE) {
                inspector_clear_numeric_edit(state);
                SDL_StopTextInput();
            } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
                char* buffer = inspector_active_numeric_buffer(&state->inspector.edit);
                if (buffer) {
                    size_t len = strlen(buffer);
                    if (len > 0) {
                        buffer[len - 1] = '\0';
                        state->inspector.edit.cursor = (int)strlen(buffer);
                    }
                }
            }
        } else if (state->inspector.visible) {
            SDL_Keycode key = event->key.keysym.sym;
            if (inspector_fade_input_handle_keydown(state, key)) {
                break;
            } else if (key == SDLK_UP) {
                inspector_update_gain(state, state->inspector.gain + 0.05f);
            } else if (key == SDLK_DOWN) {
                inspector_update_gain(state, state->inspector.gain - 0.05f);
            }
        }
        break;
    default:
        break;
    }
}

void inspector_input_handle_gain_drag(AppState* state, int mouse_x) {
    inspector_update_gain_from_mouse(state, mouse_x);
}

void inspector_input_stop_gain_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.adjusting_gain = false;
}

void inspector_input_sync(AppState* state) {
    if (!state) {
        return;
    }
    if (!state->inspector.visible) {
        inspector_stop_text_input(state);
        if (inspector_is_numeric_editing(&state->inspector.edit)) {
            inspector_clear_numeric_edit(state);
            SDL_StopTextInput();
        }
        state->inspector.adjusting_gain = false;
        state->inspector.has_focus = false;
        return;
    }
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip) {
        inspector_input_init(state);
        return;
    }
    if (!state->inspector.editing_name) {
        strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
        state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
        state->inspector.name_cursor = (int)strlen(state->inspector.name);
        state->inspector.name_scroll = 0;
    }
    if (!state->inspector.adjusting_gain) {
        state->inspector.gain = clip->gain;
    }
    inspector_fade_input_sync(state, clip);
}

bool inspector_input_has_text_focus(const AppState* state) {
    if (!state) {
        return false;
    }
    if (state->inspector.editing_name) {
        return true;
    }
    return inspector_is_numeric_editing(&state->inspector.edit);
}

bool inspector_input_has_focus(const AppState* state) {
    if (!state) {
        return false;
    }
    return state->inspector.visible && state->inspector.has_focus;
}
