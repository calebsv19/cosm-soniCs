#include "input/timeline_selection.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline_drag.h"
#include "undo/undo_manager.h"

#include <stddef.h>
#include <string.h>

bool timeline_selection_contains(const AppState* state, int track_index, int clip_index, int* out_index) {
    if (!state) {
        return false;
    }
    for (int i = 0; i < state->selection_count; ++i) {
        if (state->selection[i].track_index == track_index && state->selection[i].clip_index == clip_index) {
            if (out_index) {
                *out_index = i;
            }
            return true;
        }
    }
    return false;
}

void timeline_selection_clear(AppState* state) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    state->active_track_index = -1;
    inspector_input_init(state);
    state->timeline_drop_track_index = 0;
}

void timeline_selection_add(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    if (clip_index < 0) {
        state->selected_track_index = track_index;
        state->selected_clip_index = -1;
        state->active_track_index = track_index;
        state->timeline_drop_track_index = track_index;
        state->selection_count = 0;
        return;
    }
    if (timeline_selection_contains(state, track_index, clip_index, NULL)) {
        state->selected_track_index = track_index;
        state->selected_clip_index = clip_index;
        return;
    }
    if (state->selection_count >= TIMELINE_MAX_SELECTION) {
        return;
    }
    state->selection[state->selection_count].track_index = track_index;
    state->selection[state->selection_count].clip_index = clip_index;
    state->selection_count++;
    state->selected_track_index = track_index;
    state->selected_clip_index = clip_index;
    state->active_track_index = track_index;
    state->timeline_drop_track_index = track_index;
}

void timeline_selection_remove(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    int index = -1;
    if (!timeline_selection_contains(state, track_index, clip_index, &index)) {
        return;
    }
    for (int i = index; i < state->selection_count - 1; ++i) {
        state->selection[i] = state->selection[i + 1];
    }
    if (state->selection_count > 0) {
        state->selection_count--;
    }
    if (state->selection_count <= 0) {
        timeline_selection_clear(state);
    } else {
        TimelineSelectionEntry last = state->selection[state->selection_count - 1];
        state->selected_track_index = last.track_index;
        state->selected_clip_index = last.clip_index;
    }
}

void timeline_selection_set_single(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->selection_count = 0;
    timeline_selection_add(state, track_index, clip_index);
}

void timeline_selection_update_index(AppState* state, int track_index, int old_clip_index, int new_clip_index) {
    if (!state) {
        return;
    }
    for (int i = 0; i < state->selection_count; ++i) {
        if (state->selection[i].track_index == track_index && state->selection[i].clip_index == old_clip_index) {
            state->selection[i].clip_index = new_clip_index;
        }
    }
    if (state->selected_track_index == track_index && state->selected_clip_index == old_clip_index) {
        state->selected_clip_index = new_clip_index;
    }
}

void timeline_selection_delete(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (state->selection_count <= 0) {
        if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks &&
                state->selected_track_index < engine_get_track_count(state->engine)) {
                const EngineTrack* track = &tracks[state->selected_track_index];
                if (track &&
                    state->selected_clip_index < track->clip_count) {
                    const EngineClip* clip = &track->clips[state->selected_clip_index];
                    UndoCommand cmd = {0};
                    cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
                    cmd.data.clip_add_remove.added = false;
                    cmd.data.clip_add_remove.track_index = state->selected_track_index;
                    cmd.data.clip_add_remove.sampler = clip->sampler;
                    memset(&cmd.data.clip_add_remove.clip, 0, sizeof(cmd.data.clip_add_remove.clip));
                    const char* media_id = engine_clip_get_media_id(clip);
                    const char* media_path = engine_clip_get_media_path(clip);
                    strncpy(cmd.data.clip_add_remove.clip.media_id, media_id ? media_id : "",
                            sizeof(cmd.data.clip_add_remove.clip.media_id) - 1);
                    cmd.data.clip_add_remove.clip.media_id[sizeof(cmd.data.clip_add_remove.clip.media_id) - 1] = '\0';
                    strncpy(cmd.data.clip_add_remove.clip.media_path, media_path ? media_path : "",
                            sizeof(cmd.data.clip_add_remove.clip.media_path) - 1);
                    cmd.data.clip_add_remove.clip.media_path[sizeof(cmd.data.clip_add_remove.clip.media_path) - 1] = '\0';
                    strncpy(cmd.data.clip_add_remove.clip.name, clip->name,
                            sizeof(cmd.data.clip_add_remove.clip.name) - 1);
                    cmd.data.clip_add_remove.clip.name[sizeof(cmd.data.clip_add_remove.clip.name) - 1] = '\0';
                    cmd.data.clip_add_remove.clip.start_frame = clip->timeline_start_frames;
                    cmd.data.clip_add_remove.clip.duration_frames = clip->duration_frames;
                    cmd.data.clip_add_remove.clip.offset_frames = clip->offset_frames;
                    cmd.data.clip_add_remove.clip.fade_in_frames = clip->fade_in_frames;
                    cmd.data.clip_add_remove.clip.fade_out_frames = clip->fade_out_frames;
                    cmd.data.clip_add_remove.clip.gain = clip->gain;
                    cmd.data.clip_add_remove.clip.selected = false;
                    if (cmd.data.clip_add_remove.clip.duration_frames == 0 && clip->sampler) {
                        cmd.data.clip_add_remove.clip.duration_frames = engine_sampler_get_frame_count(clip->sampler);
                    }
                    undo_manager_push(&state->undo, &cmd);
                }
            }
            engine_remove_clip(state->engine, state->selected_track_index, state->selected_clip_index);
            timeline_selection_clear(state);
        }
        return;
    }
    for (int i = state->selection_count - 1; i >= 0; --i) {
        int track_index = state->selection[i].track_index;
        int clip_index = state->selection[i].clip_index;
        if (track_index >= 0 && clip_index >= 0) {
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks && track_index < engine_get_track_count(state->engine)) {
                const EngineTrack* track = &tracks[track_index];
                if (track && clip_index < track->clip_count) {
                    const EngineClip* clip = &track->clips[clip_index];
                    UndoCommand cmd = {0};
                    cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
                    cmd.data.clip_add_remove.added = false;
                    cmd.data.clip_add_remove.track_index = track_index;
                    cmd.data.clip_add_remove.sampler = clip->sampler;
                    memset(&cmd.data.clip_add_remove.clip, 0, sizeof(cmd.data.clip_add_remove.clip));
                    const char* media_id = engine_clip_get_media_id(clip);
                    const char* media_path = engine_clip_get_media_path(clip);
                    strncpy(cmd.data.clip_add_remove.clip.media_id, media_id ? media_id : "",
                            sizeof(cmd.data.clip_add_remove.clip.media_id) - 1);
                    cmd.data.clip_add_remove.clip.media_id[sizeof(cmd.data.clip_add_remove.clip.media_id) - 1] = '\0';
                    strncpy(cmd.data.clip_add_remove.clip.media_path, media_path ? media_path : "",
                            sizeof(cmd.data.clip_add_remove.clip.media_path) - 1);
                    cmd.data.clip_add_remove.clip.media_path[sizeof(cmd.data.clip_add_remove.clip.media_path) - 1] = '\0';
                    strncpy(cmd.data.clip_add_remove.clip.name, clip->name,
                            sizeof(cmd.data.clip_add_remove.clip.name) - 1);
                    cmd.data.clip_add_remove.clip.name[sizeof(cmd.data.clip_add_remove.clip.name) - 1] = '\0';
                    cmd.data.clip_add_remove.clip.start_frame = clip->timeline_start_frames;
                    cmd.data.clip_add_remove.clip.duration_frames = clip->duration_frames;
                    cmd.data.clip_add_remove.clip.offset_frames = clip->offset_frames;
                    cmd.data.clip_add_remove.clip.fade_in_frames = clip->fade_in_frames;
                    cmd.data.clip_add_remove.clip.fade_out_frames = clip->fade_out_frames;
                    cmd.data.clip_add_remove.clip.gain = clip->gain;
                    cmd.data.clip_add_remove.clip.selected = false;
                    if (cmd.data.clip_add_remove.clip.duration_frames == 0 && clip->sampler) {
                        cmd.data.clip_add_remove.clip.duration_frames = engine_sampler_get_frame_count(clip->sampler);
                    }
                    undo_manager_push(&state->undo, &cmd);
                }
            }
            if (engine_remove_clip(state->engine, track_index, clip_index)) {
                for (int j = 0; j < i; ++j) {
                    if (state->selection[j].track_index == track_index &&
                        state->selection[j].clip_index > clip_index) {
                        state->selection[j].clip_index -= 1;
                    }
                }
            }
        }
    }
    timeline_selection_clear(state);
}
