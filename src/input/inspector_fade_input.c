#include "input/inspector_fade_input.h"

#include "app_state.h"
#include "engine/audio_source.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "undo/undo_manager.h"

#include <math.h>
#include <stdlib.h>

// Looks up the mutable engine clip for the current inspector selection.
static EngineClip* inspector_fade_get_clip_mutable(AppState* state) {
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

// Looks up the const engine clip for the current inspector selection.
static const EngineClip* inspector_fade_get_clip_const(const AppState* state) {
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

// Captures clip state for undo when adjusting inspector fades.
static bool inspector_fade_state_from_clip(const EngineClip* clip, int track_index, UndoClipState* out_state) {
    if (!clip || !out_state) {
        return false;
    }
    out_state->kind = engine_clip_get_kind(clip);
    out_state->sampler = clip->sampler;
    out_state->creation_index = clip->creation_index;
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

// Compares clip states to avoid no-op undo entries.
static bool inspector_fade_state_equal(const UndoClipState* a, const UndoClipState* b) {
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

// Begins a clip transform drag for fade adjustments.
static void inspector_fade_begin_clip_drag(AppState* state) {
    if (!state) {
        return;
    }
    const EngineClip* clip = inspector_fade_get_clip_const(state);
    if (!clip) {
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    if (!inspector_fade_state_from_clip(clip, state->inspector.track_index, &cmd.data.clip_transform.before)) {
        return;
    }
    cmd.data.clip_transform.after = cmd.data.clip_transform.before;
    undo_manager_begin_drag(&state->undo, &cmd);
}

// Returns the full clip length in frames for inspector fade calculations.
static uint64_t inspector_fade_clip_length_frames(const EngineClip* clip) {
    if (!clip) {
        return 0;
    }
    uint64_t frames = clip->duration_frames;
    if (frames == 0 && clip->sampler) {
        frames = engine_sampler_get_frame_count(clip->sampler);
    }
    return frames;
}

// Cycles to the next/previous fade curve enum value.
static EngineFadeCurve inspector_fade_cycle_curve(EngineFadeCurve curve, int step) {
    int count = (int)ENGINE_FADE_CURVE_COUNT;
    if (count <= 0) {
        return ENGINE_FADE_CURVE_LINEAR;
    }
    int value = (int)curve;
    if (value < 0 || value >= count) {
        value = 0;
    }
    int next = (value + step) % count;
    if (next < 0) {
        next += count;
    }
    return (EngineFadeCurve)next;
}

// Updates the inspector fade selection state with optional additive toggle behavior.
static void inspector_fade_update_selection(AppState* state, bool select_in, bool select_out, bool additive) {
    if (!state) {
        return;
    }
    if (!additive) {
        state->inspector.fade_in_selected = select_in;
        state->inspector.fade_out_selected = select_out;
        return;
    }
    if (select_in) {
        state->inspector.fade_in_selected = !state->inspector.fade_in_selected;
    }
    if (select_out) {
        state->inspector.fade_out_selected = !state->inspector.fade_out_selected;
    }
}

// Computes the visible clip edge positions in inspector waveform coordinates.
static bool inspector_fade_waveform_clip_edges(const AppState* state,
                                               const EngineClip* clip,
                                               const SDL_Rect* rect,
                                               int* out_left,
                                               int* out_right) {
    if (!state || !clip || !rect || rect->w <= 0) {
        return false;
    }
    uint64_t clip_frames = inspector_fade_clip_length_frames(clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }
    uint64_t view_start = 0;
    uint64_t view_frames = 0;
    if (!clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames) || view_frames == 0) {
        return false;
    }
    uint64_t clip_start = clip->offset_frames;
    uint64_t clip_end = clip_start + clip_frames;
    uint64_t view_end = view_start + view_frames;
    if (clip_end <= view_start || clip_start >= view_end) {
        return false;
    }
    double start_t = (double)(clip_start > view_start ? clip_start - view_start : 0) / (double)view_frames;
    double end_t = (double)(clip_end > view_start ? clip_end - view_start : 0) / (double)view_frames;
    if (start_t < 0.0) start_t = 0.0;
    if (end_t > 1.0) end_t = 1.0;
    if (end_t <= start_t) {
        return false;
    }
    int left = rect->x + (int)llround(start_t * (double)rect->w);
    int right = rect->x + (int)llround(end_t * (double)rect->w);
    if (left < rect->x) left = rect->x;
    if (right > rect->x + rect->w) right = rect->x + rect->w;
    if (out_left) *out_left = left;
    if (out_right) *out_right = right;
    return true;
}

// Tests if a mouse x-position hits either fade region in the inspector waveform.
static bool inspector_fade_waveform_hit(const AppState* state,
                                        const EngineClip* clip,
                                        const SDL_Rect* rect,
                                        int mouse_x,
                                        bool* hit_fade_in,
                                        bool* hit_fade_out) {
    if (hit_fade_in) *hit_fade_in = false;
    if (hit_fade_out) *hit_fade_out = false;
    if (!state || !clip || !rect || rect->w <= 0) {
        return false;
    }
    if (!clip->media || !clip->media->samples || clip->media->frame_count == 0) {
        return false;
    }
    uint64_t clip_frames = inspector_fade_clip_length_frames(clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }
    uint64_t fade_in_frames = clip->fade_in_frames > clip_frames ? clip_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = clip->fade_out_frames > clip_frames ? clip_frames : clip->fade_out_frames;
    uint64_t view_start = 0;
    uint64_t view_frames = 0;
    if (!clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames) || view_frames == 0) {
        return false;
    }

    uint64_t clip_start = clip->offset_frames;
    uint64_t clip_end = clip_start + clip_frames;
    uint64_t view_end = view_start + view_frames;
    bool hit_in = false;
    bool hit_out = false;

    if (fade_in_frames > 0) {
        uint64_t fade_start = clip_start;
        uint64_t fade_end = clip_start + fade_in_frames;
        if (fade_end > view_start && fade_start < view_end) {
            double start_t = (double)(fade_start > view_start ? fade_start - view_start : 0) / (double)view_frames;
            double end_t = (double)(fade_end > view_start ? fade_end - view_start : 0) / (double)view_frames;
            if (start_t < 0.0) start_t = 0.0;
            if (end_t > 1.0) end_t = 1.0;
            int x0 = rect->x + (int)floor(start_t * (double)rect->w);
            int x1 = rect->x + (int)ceil(end_t * (double)rect->w);
            if (x1 < x0) x1 = x0;
            if (mouse_x >= x0 && mouse_x <= x1) {
                hit_in = true;
            }
        }
    }

    if (fade_out_frames > 0) {
        uint64_t fade_start = clip_end > fade_out_frames ? clip_end - fade_out_frames : clip_start;
        uint64_t fade_end = clip_end;
        if (fade_end > view_start && fade_start < view_end) {
            double start_t = (double)(fade_start > view_start ? fade_start - view_start : 0) / (double)view_frames;
            double end_t = (double)(fade_end > view_start ? fade_end - view_start : 0) / (double)view_frames;
            if (start_t < 0.0) start_t = 0.0;
            if (end_t > 1.0) end_t = 1.0;
            int x0 = rect->x + (int)floor(start_t * (double)rect->w);
            int x1 = rect->x + (int)ceil(end_t * (double)rect->w);
            if (x1 < x0) x1 = x0;
            if (mouse_x >= x0 && mouse_x <= x1) {
                hit_out = true;
            }
        }
    }

    if (hit_fade_in) *hit_fade_in = hit_in;
    if (hit_fade_out) *hit_fade_out = hit_out;
    return hit_in || hit_out;
}

// Updates fade length by dragging within the inspector fade tracks.
static void inspector_fade_update_from_track(AppState* state, bool is_fade_in, int mouse_x) {
    if (!state || !state->engine) {
        return;
    }
    EngineClip* clip = inspector_fade_get_clip_mutable(state);
    if (!clip) {
        return;
    }
    ClipInspectorLayout layout;
    clip_inspector_compute_layout(state, &layout);
    SDL_Rect track = is_fade_in ? layout.fade_in_track_rect : layout.fade_out_track_rect;
    if (track.w <= 0) {
        return;
    }
    float t = (float)(mouse_x - track.x) / (float)track.w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    uint64_t max_frames = inspector_fade_clip_length_frames(clip);
    if (max_frames == 0) {
        max_frames = 1;
    }
    uint64_t new_frames = (uint64_t)llround((double)max_frames * (double)t);

    uint64_t fade_in_frames = is_fade_in ? new_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = is_fade_in ? clip->fade_out_frames : new_frames;
    engine_clip_set_fades(state->engine, state->inspector.track_index, state->inspector.clip_index,
                          fade_in_frames, fade_out_frames);

    clip = inspector_fade_get_clip_mutable(state);
    if (clip) {
        state->inspector.fade_in_frames = clip->fade_in_frames;
        state->inspector.fade_out_frames = clip->fade_out_frames;
    }
}

// Updates fade length by dragging within the inspector waveform view.
static void inspector_fade_update_from_waveform(AppState* state, bool is_fade_in, int mouse_x) {
    if (!state || !state->engine) {
        return;
    }
    EngineClip* clip = inspector_fade_get_clip_mutable(state);
    if (!clip) {
        return;
    }
    ClipInspectorLayout layout;
    clip_inspector_compute_layout(state, &layout);
    SDL_Rect rect = layout.right_waveform_rect;
    if (rect.w <= 0) {
        return;
    }
    uint64_t clip_frames = inspector_fade_clip_length_frames(clip);
    if (clip_frames == 0) {
        clip_frames = 1;
    }
    uint64_t view_start = 0;
    uint64_t view_frames = 0;
    if (!clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames) || view_frames == 0) {
        return;
    }

    if (mouse_x < rect.x) mouse_x = rect.x;
    if (mouse_x > rect.x + rect.w) mouse_x = rect.x + rect.w;
    double t = (double)(mouse_x - rect.x) / (double)rect.w;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    uint64_t frame_at = view_start + (uint64_t)llround(t * (double)view_frames);

    uint64_t clip_start = clip->offset_frames;
    uint64_t clip_end = clip_start + clip_frames;
    if (frame_at < clip_start) frame_at = clip_start;
    if (frame_at > clip_end) frame_at = clip_end;

    uint64_t new_frames = is_fade_in ? (frame_at - clip_start) : (clip_end - frame_at);
    if (new_frames > clip_frames) new_frames = clip_frames;

    uint64_t fade_in_frames = is_fade_in ? new_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = is_fade_in ? clip->fade_out_frames : new_frames;
    engine_clip_set_fades(state->engine, state->inspector.track_index, state->inspector.clip_index,
                          fade_in_frames, fade_out_frames);

    clip = inspector_fade_get_clip_mutable(state);
    if (clip) {
        state->inspector.fade_in_frames = clip->fade_in_frames;
        state->inspector.fade_out_frames = clip->fade_out_frames;
    }
}

// Applies a curve step change to the selected clip fade sides and records undo.
static void inspector_fade_update_curves(AppState* state, bool update_in, bool update_out, int step) {
    if (!state || !state->engine) {
        return;
    }
    EngineClip* clip = inspector_fade_get_clip_mutable(state);
    if (!clip) {
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    if (!inspector_fade_state_from_clip(clip, state->inspector.track_index, &cmd.data.clip_transform.before)) {
        return;
    }
    EngineFadeCurve next_in = clip->fade_in_curve;
    EngineFadeCurve next_out = clip->fade_out_curve;
    if (update_in) {
        next_in = inspector_fade_cycle_curve(next_in, step);
    }
    if (update_out) {
        next_out = inspector_fade_cycle_curve(next_out, step);
    }
    if (!engine_clip_set_fade_curves(state->engine,
                                     state->inspector.track_index,
                                     state->inspector.clip_index,
                                     next_in,
                                     next_out)) {
        return;
    }
    clip = inspector_fade_get_clip_mutable(state);
    if (!inspector_fade_state_from_clip(clip, state->inspector.track_index, &cmd.data.clip_transform.after)) {
        return;
    }
    cmd.data.clip_transform.before.sampler = cmd.data.clip_transform.after.sampler;
    if (!inspector_fade_state_equal(&cmd.data.clip_transform.before, &cmd.data.clip_transform.after)) {
        undo_manager_push(&state->undo, &cmd);
    }
}

void inspector_fade_input_init(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.adjusting_fade_in = false;
    state->inspector.adjusting_fade_out = false;
    state->inspector.fade_drag_from_waveform = false;
    state->inspector.pending_fade_drag = false;
    state->inspector.pending_fade_in = false;
    state->inspector.pending_fade_from_waveform = false;
    state->inspector.pending_fade_start_x = 0;
    state->inspector.fade_in_selected = false;
    state->inspector.fade_out_selected = false;
    state->inspector.fade_in_frames = 0;
    state->inspector.fade_out_frames = 0;
}

void inspector_fade_input_show(AppState* state, const EngineClip* clip) {
    if (!state || !clip) {
        return;
    }
    state->inspector.adjusting_fade_in = false;
    state->inspector.adjusting_fade_out = false;
    state->inspector.fade_drag_from_waveform = false;
    state->inspector.pending_fade_drag = false;
    state->inspector.pending_fade_in = false;
    state->inspector.pending_fade_from_waveform = false;
    state->inspector.pending_fade_start_x = 0;
    state->inspector.fade_in_selected = false;
    state->inspector.fade_out_selected = false;
    state->inspector.fade_in_frames = clip->fade_in_frames;
    state->inspector.fade_out_frames = clip->fade_out_frames;
}

void inspector_fade_input_set_clip(AppState* state, const EngineClip* clip) {
    if (!state || !clip) {
        return;
    }
    state->inspector.fade_in_selected = false;
    state->inspector.fade_out_selected = false;
    state->inspector.pending_fade_drag = false;
    state->inspector.pending_fade_in = false;
    state->inspector.pending_fade_from_waveform = false;
    state->inspector.pending_fade_start_x = 0;
    if (!state->inspector.adjusting_fade_in) {
        state->inspector.fade_in_frames = clip->fade_in_frames;
    }
    if (!state->inspector.adjusting_fade_out) {
        state->inspector.fade_out_frames = clip->fade_out_frames;
    }
}

void inspector_fade_input_sync(AppState* state, const EngineClip* clip) {
    if (!state || !clip) {
        return;
    }
    if (!state->inspector.adjusting_fade_in) {
        state->inspector.fade_in_frames = clip->fade_in_frames;
    }
    if (!state->inspector.adjusting_fade_out) {
        state->inspector.fade_out_frames = clip->fade_out_frames;
    }
}

bool inspector_fade_input_handle_waveform_mouse_down(AppState* state,
                                                     const ClipInspectorLayout* layout,
                                                     const SDL_Point* point,
                                                     bool shift_held,
                                                     bool alt_held) {
    if (!state || !layout || !point) {
        return false;
    }
    if (!SDL_PointInRect(point, &layout->right_waveform_rect)) {
        return false;
    }

    const EngineClip* clip = inspector_fade_get_clip_const(state);
    bool hit_fade_in = false;
    bool hit_fade_out = false;
    bool hit_any = clip && inspector_fade_waveform_hit(state, clip, &layout->right_waveform_rect, point->x,
                                                       &hit_fade_in, &hit_fade_out);
    if (clip && alt_held) {
        int edge_left = 0;
        int edge_right = 0;
        const int edge_hit_px = 6;
        if (inspector_fade_waveform_clip_edges(state, clip, &layout->right_waveform_rect, &edge_left, &edge_right)) {
            int dist_left = abs(point->x - edge_left);
            int dist_right = abs(point->x - edge_right);
            bool hit_left = dist_left <= edge_hit_px;
            bool hit_right = dist_right <= edge_hit_px;
            if (hit_left || hit_right) {
                inspector_input_commit_if_editing(state);
                if (hit_left && (!hit_right || dist_left <= dist_right)) {
                    inspector_fade_update_selection(state, true, false, shift_held);
                    state->inspector.adjusting_fade_in = true;
                    state->inspector.adjusting_fade_out = false;
                    state->inspector.fade_drag_from_waveform = true;
                    inspector_fade_begin_clip_drag(state);
                    inspector_fade_update_from_waveform(state, true, point->x);
                    return true;
                }
                inspector_fade_update_selection(state, false, true, shift_held);
                state->inspector.adjusting_fade_out = true;
                state->inspector.adjusting_fade_in = false;
                state->inspector.fade_drag_from_waveform = true;
                inspector_fade_begin_clip_drag(state);
                inspector_fade_update_from_waveform(state, false, point->x);
                return true;
            }
        }
    }
    if (hit_any) {
        inspector_input_commit_if_editing(state);
        if (shift_held) {
            inspector_fade_update_selection(state, hit_fade_in, hit_fade_out, true);
            return true;
        }
        inspector_fade_update_selection(state, hit_fade_in, hit_fade_out, false);
        if (hit_fade_in && !hit_fade_out) {
            state->inspector.pending_fade_drag = true;
            state->inspector.pending_fade_in = true;
            state->inspector.pending_fade_from_waveform = true;
            state->inspector.pending_fade_start_x = point->x;
            return true;
        }
        if (hit_fade_out && !hit_fade_in) {
            state->inspector.pending_fade_drag = true;
            state->inspector.pending_fade_in = false;
            state->inspector.pending_fade_from_waveform = true;
            state->inspector.pending_fade_start_x = point->x;
            return true;
        }
        return true;
    }
    if (!shift_held) {
        inspector_fade_update_selection(state, false, false, false);
    }
    return false;
}

bool inspector_fade_input_handle_track_mouse_down(AppState* state,
                                                  const ClipInspectorLayout* layout,
                                                  const SDL_Point* point,
                                                  bool shift_held,
                                                  bool is_fade_in) {
    if (!state || !layout || !point) {
        return false;
    }
    SDL_Rect track = is_fade_in ? layout->fade_in_track_rect : layout->fade_out_track_rect;
    if (!SDL_PointInRect(point, &track)) {
        return false;
    }
    inspector_input_commit_if_editing(state);
    inspector_fade_update_selection(state, is_fade_in, !is_fade_in, shift_held);
    state->inspector.adjusting_fade_in = is_fade_in;
    state->inspector.adjusting_fade_out = !is_fade_in;
    state->inspector.fade_drag_from_waveform = false;
    inspector_fade_begin_clip_drag(state);
    inspector_fade_update_from_track(state, is_fade_in, point->x);
    return true;
}

void inspector_fade_input_handle_mouse_up(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.adjusting_fade_in = false;
    state->inspector.adjusting_fade_out = false;
    state->inspector.fade_drag_from_waveform = false;
    state->inspector.pending_fade_drag = false;
    state->inspector.pending_fade_in = false;
    state->inspector.pending_fade_from_waveform = false;
    state->inspector.pending_fade_start_x = 0;
}

bool inspector_fade_input_handle_pending_drag(AppState* state, int mouse_x) {
    if (!state || !state->inspector.pending_fade_drag) {
        return false;
    }
    int delta = abs(mouse_x - state->inspector.pending_fade_start_x);
    if (delta >= 3) {
        state->inspector.adjusting_fade_in = state->inspector.pending_fade_in;
        state->inspector.adjusting_fade_out = !state->inspector.pending_fade_in;
        state->inspector.fade_drag_from_waveform = state->inspector.pending_fade_from_waveform;
        state->inspector.pending_fade_drag = false;
        inspector_fade_begin_clip_drag(state);
        if (state->inspector.fade_drag_from_waveform) {
            inspector_fade_update_from_waveform(state, state->inspector.adjusting_fade_in, mouse_x);
        } else {
            inspector_fade_update_from_track(state, state->inspector.adjusting_fade_in, mouse_x);
        }
    }
    return true;
}

bool inspector_fade_input_handle_active_drag(AppState* state, int mouse_x) {
    if (!state) {
        return false;
    }
    if (state->inspector.adjusting_fade_in) {
        if (state->inspector.fade_drag_from_waveform) {
            inspector_fade_update_from_waveform(state, true, mouse_x);
        } else {
            inspector_fade_update_from_track(state, true, mouse_x);
        }
        return true;
    }
    if (state->inspector.adjusting_fade_out) {
        if (state->inspector.fade_drag_from_waveform) {
            inspector_fade_update_from_waveform(state, false, mouse_x);
        } else {
            inspector_fade_update_from_track(state, false, mouse_x);
        }
        return true;
    }
    return false;
}

bool inspector_fade_input_handle_keydown(AppState* state, SDL_Keycode key) {
    if (!state) {
        return false;
    }
    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
        if (state->inspector.fade_in_selected || state->inspector.fade_out_selected) {
            int step = (key == SDLK_RIGHT) ? 1 : -1;
            inspector_fade_update_curves(state,
                                         state->inspector.fade_in_selected,
                                         state->inspector.fade_out_selected,
                                         step);
            return true;
        }
    }
    return false;
}
