#include "input/timeline_drag.h"

#include "engine/engine.h"
#include "input/timeline_selection.h"
#include "input/inspector_input.h"
#include "undo/undo_manager.h"
#include "engine/sampler.h"
#include "input/timeline/timeline_clip_helpers.h"

#include <SDL2/SDL.h>
#include <limits.h>
#include <string.h>

bool timeline_find_clip_by_sampler(const AppState* state, EngineSamplerSource* sampler, int* out_track, int* out_clip) {
    if (!state || !state->engine || !sampler) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks) {
        return false;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) {
            continue;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            if (track->clips[c].sampler == sampler) {
                if (out_track) *out_track = t;
                if (out_clip) *out_clip = c;
                return true;
            }
        }
    }
    return false;
}

// Delegates a complete timeline transfer to the engine's transactional move operation.
int timeline_move_clip_to_track(AppState* state, int src_track, int clip_index, int dst_track, uint64_t start_frame) {
    if (!state || !state->engine) return -1;
    int result = -1;
    return engine_move_clip_to_track(state->engine, src_track, clip_index, dst_track, start_frame, &result) ? result : -1;
}

// Finds a clip by its project identity after sorting or track movement.
static bool compound_find_clip(const Engine* engine, uint64_t identity, int* track, int* clip) {
    const EngineTrack* tracks = engine_get_tracks(engine);
    for (int t = 0; t < engine_get_track_count(engine); ++t)
        for (int c = 0; c < tracks[t].clip_count; ++c)
            if (tracks[t].clips[c].creation_index == identity) { *track = t; *clip = c; return true; }
    return false;
}

// Applies one absolute gesture preview from its complete initial history, then updates selection readback.
static bool timeline_apply_compound_placement(AppState* state, int64_t delta_frames, bool slip, int track_offset, bool resolve_overlap) {
    if (!state || !state->engine || !state->undo.active_drag_valid) return false;
    UndoCommand* command = &state->undo.active_drag;
    UndoMultiClipTransform single = {0};
    UndoMultiClipTransform* history = NULL;
    if (command->type == UNDO_CMD_MULTI_CLIP_TRANSFORM) history = &command->data.multi_clip_transform;
    else if (resolve_overlap && command->type == UNDO_CMD_CLIP_TRANSFORM) {
        single.count = 1; single.before = &command->data.clip_transform.before; single.after = &command->data.clip_transform.after;
        history = &single;
    } else return false;
    if (history->count <= 0 || !history->before || !history->after) return false;
    EngineClipBatchTransform* edits = SDL_calloc((size_t)history->count, sizeof(*edits));
    if (!edits) return false;
    uint64_t selected[TIMELINE_MAX_SELECTION] = {0};
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    int selected_count = state->selection_count;
    if (selected_count < 0 || selected_count > TIMELINE_MAX_SELECTION) { SDL_free(edits); return false; }
    for (int i = 0; i < selected_count; ++i) {
        TimelineSelectionEntry entry = state->selection[i];
        if (entry.track_index < 0 || entry.track_index >= track_count || entry.clip_index < 0 ||
            entry.clip_index >= tracks[entry.track_index].clip_count) { SDL_free(edits); return false; }
        selected[i] = tracks[entry.track_index].clips[entry.clip_index].creation_index;
    }
    TimelineDragState* drag = &state->timeline_drag;
    if (drag->track_index < 0 || drag->track_index >= track_count || drag->clip_index < 0 ||
        drag->clip_index >= tracks[drag->track_index].clip_count) { SDL_free(edits); return false; }
    uint64_t anchor = tracks[drag->track_index].clips[drag->clip_index].creation_index;
    for (int i = 0; i < history->count; ++i) {
        const UndoClipState* before = &history->before[i];
        int track, clip;
        if (!compound_find_clip(state->engine, before->creation_index, &track, &clip)) { SDL_free(edits); return false; }
        uint64_t base = slip ? before->offset_frames : before->start_frame;
        uint64_t amount = delta_frames < 0 ? (uint64_t)(-(delta_frames + 1)) + 1 : (uint64_t)delta_frames;
        uint64_t target = delta_frames < 0 ? (base > amount ? base - amount : 0) :
            (amount <= UINT64_MAX - base ? base + amount : UINT64_MAX);
        if (slip) {
            uint64_t total = engine_clip_get_total_frames(state->engine, track, clip);
            if (before->kind != ENGINE_CLIP_KIND_AUDIO || total < before->duration_frames) { SDL_free(edits); return false; }
            if (target > total - before->duration_frames) target = total - before->duration_frames;
        }
        edits[i].creation_index = before->creation_index;
        int destination = track;
        if (!slip && track_offset) {
            int origin = -1;
            for (int t = 0; t < track_count; ++t)
                if (tracks[t].runtime_id == before->track_runtime_id) { origin = t; break; }
            if (origin < 0 || (track_offset > 0 && origin > INT_MAX - track_offset)) { SDL_free(edits); return false; }
            destination = origin + track_offset;
            if (destination < 0) destination = 0;
        }
        edits[i].destination_track = destination;
        edits[i].transform = (EngineClipTransform){
            .start_frame = slip ? before->start_frame : target,
            .offset_frames = slip ? target : before->offset_frames,
            .duration_frames = before->duration_frames, .gain = before->gain,
            .fade_in_frames = before->fade_in_frames, .fade_out_frames = before->fade_out_frames,
            .fade_in_curve = before->fade_in_curve, .fade_out_curve = before->fade_out_curve,
            .instrument_preset = before->instrument_preset, .instrument_params = before->instrument_params,
            .instrument_inherits_track = before->instrument_inherits_track,
            .midi_notes = before->midi_notes, .midi_note_count = before->midi_note_count};
    }
    int required_tracks = track_count;
    for (int i = 0; i < history->count; ++i)
        if (edits[i].destination_track >= required_tracks) {
            if (edits[i].destination_track == INT_MAX) { SDL_free(edits); return false; }
            required_tracks = edits[i].destination_track + 1;
        }
    if (history == &single && required_tracks > track_count) { SDL_free(edits); return false; }
    int created_start = history->created_count ? history->created_start : track_count;
    UndoCreatedTrack* guards = NULL;
    if (required_tracks > track_count) {
        guards = SDL_calloc((size_t)(required_tracks - created_start), sizeof(*guards));
        if (!guards) { SDL_free(edits); return false; }
        if (history->created_count) memcpy(guards, history->created_tracks, (size_t)history->created_count * sizeof(*guards));
    }
    bool applied = false;
    if (resolve_overlap && !history->created_count) {
        EngineClipBatchTransform* initial = SDL_calloc((size_t)history->count, sizeof(*initial));
        if (!initial) { SDL_free(guards); SDL_free(edits); return false; }
        memcpy(initial, edits, (size_t)history->count * sizeof(*initial));
        for (int i = 0; i < history->count; ++i) {
            initial[i].destination_track = -1;
            for (int t = 0; t < track_count; ++t)
                if (tracks[t].runtime_id == history->before[i].track_runtime_id) initial[i].destination_track = t;
            initial[i].transform.start_frame = history->before[i].start_frame;
            initial[i].transform.offset_frames = history->before[i].offset_frames;
        }
        EngineClipContentSnapshot* before = NULL; EngineClipContentSnapshot* after = NULL;
        applied = engine_clip_content_drop(state->engine, initial, edits, history->count, &before, &after);
        SDL_free(initial);
        if (applied) {
            engine_clip_content_release(state->undo.active_drag.clip_content_before);
            engine_clip_content_release(state->undo.active_drag.clip_content_after);
            state->undo.active_drag.clip_content_before = before;
            state->undo.active_drag.clip_content_after = after;
        }
    } else applied = engine_transform_clips(state->engine, edits, history->count);
    if (applied) {
        if (guards) {
            for (int t = track_count; t < required_tracks; ++t)
                undo_created_track_capture(&engine_get_tracks(state->engine)[t], &guards[t - created_start]);
            SDL_free(history->created_tracks); history->created_tracks = guards; guards = NULL;
            history->created_start = created_start; history->created_count = required_tracks - created_start;
        }
        for (int i = 0; i < history->count; ++i) {
            history->after[i].track_index = edits[i].destination_track;
            history->after[i].track_runtime_id = engine_get_tracks(state->engine)[edits[i].destination_track].runtime_id;
            history->after[i].start_frame = edits[i].transform.start_frame;
            history->after[i].offset_frames = edits[i].transform.offset_frames;
        }
        timeline_selection_clear(state);
        for (int i = 0; i < selected_count; ++i) {
            int t, c;
            if (compound_find_clip(state->engine, selected[i], &t, &c)) timeline_selection_add(state, t, c);
        }
        if (!compound_find_clip(state->engine, anchor, &drag->track_index, &drag->clip_index)) {
            drag->track_index = state->selection_count ? state->selection[0].track_index : -1;
            drag->clip_index = state->selection_count ? state->selection[0].clip_index : -1;
        }
        if (drag->track_index >= 0) inspector_input_set_clip(state, drag->track_index, drag->clip_index);
        else inspector_input_init(state);
    }
    SDL_free(guards);
    SDL_free(edits);
    return applied;
}

// Applies an absolute move or slip preview without changing track destinations.
bool timeline_apply_compound_preview(AppState* state, int64_t delta_frames, bool slip) {
    return timeline_apply_compound_placement(state, delta_frames, slip, 0, false);
}

// Publishes every selected destination and required new track before updating selection or history.
bool timeline_apply_compound_drop(AppState* state, int64_t delta_frames, int track_offset) {
    return timeline_apply_compound_placement(state, delta_frames, false, track_offset, true);
}

// Applies an audio edge edit without exposing an intermediate source region or timeline position.
bool timeline_apply_audio_trim(AppState* state, uint64_t start, uint64_t offset, uint64_t duration) {
    if (!state || !state->engine) return false;
    TimelineDragState* drag = &state->timeline_drag;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    if (drag->track_index < 0 || drag->track_index >= engine_get_track_count(state->engine) ||
        drag->clip_index < 0 || drag->clip_index >= tracks[drag->track_index].clip_count) return false;
    const EngineClip* clip = &tracks[drag->track_index].clips[drag->clip_index];
    if (clip->kind != ENGINE_CLIP_KIND_AUDIO) return false;
    uint64_t selected[TIMELINE_MAX_SELECTION] = {0};
    int selection_count = state->selection_count;
    if (selection_count < 0 || selection_count > TIMELINE_MAX_SELECTION) return false;
    for (int i = 0; i < selection_count; ++i) {
        TimelineSelectionEntry entry = state->selection[i];
        if (entry.track_index < 0 || entry.track_index >= engine_get_track_count(state->engine) ||
            entry.clip_index < 0 || entry.clip_index >= tracks[entry.track_index].clip_count) return false;
        selected[i] = tracks[entry.track_index].clips[entry.clip_index].creation_index;
    }
    EngineClipTransform transform = {
        .start_frame = start, .offset_frames = offset, .duration_frames = duration, .gain = clip->gain,
        .fade_in_frames = clip->fade_in_frames, .fade_out_frames = clip->fade_out_frames,
        .fade_in_curve = clip->fade_in_curve, .fade_out_curve = clip->fade_out_curve};
    int old_index = drag->clip_index, new_index = old_index;
    if (!engine_transform_clip(state->engine, drag->track_index, old_index, drag->track_index, &transform, &new_index)) return false;
    drag->clip_index = new_index;
    timeline_selection_clear(state);
    for (int i = 0; i < selection_count; ++i) {
        int t, c;
        if (compound_find_clip(state->engine, selected[i], &t, &c)) timeline_selection_add(state, t, c);
    }
    inspector_input_set_clip(state, drag->track_index, new_index);
    return true;
}
