#include "input/timeline/timeline_clipboard.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline_drag.h"
#include "input/timeline_selection.h"
#include "input/inspector_input.h"
#include "ui/effects_panel.h"
#include "undo/undo_manager.h"

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

// Owns one copied region independently of subsequent source edits.
typedef struct {
    SessionClip clip;
    int track_index;
    uint64_t start_frame;
} TimelineClipboardEntry;

// Owns the complete clipboard selection and its placement anchor.
typedef struct {
    TimelineClipboardEntry entries[TIMELINE_MAX_SELECTION];
    int count;
    uint64_t anchor_start_frame;
} TimelineClipboard;

static TimelineClipboard g_timeline_clipboard = {0};

// Releases a prepared or previously published clipboard without touching the current project.
static void timeline_clipboard_clear(TimelineClipboard* clipboard) {
    for (int i = 0; i < clipboard->count; ++i) timeline_session_clip_clear(&clipboard->entries[i].clip);
    memset(clipboard, 0, sizeof(*clipboard));
}

void timeline_clipboard_copy(AppState* state) {
    if (!state || !state->engine || state->selection_count < 0 || state->selection_count > TIMELINE_MAX_SELECTION) {
        return;
    }

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_count <= 0) {
        return;
    }

    TimelineSelectionEntry temp_entries[TIMELINE_MAX_SELECTION];
    int temp_count = 0;
    bool selected_valid = false;
    if (state->selected_track_index >= 0 &&
        state->selected_track_index < track_count &&
        state->selected_clip_index >= 0) {
        const EngineTrack* selected_track = &tracks[state->selected_track_index];
        selected_valid = selected_track &&
                         state->selected_clip_index < selected_track->clip_count;
    }
    bool selected_in_selection = selected_valid &&
                                 timeline_selection_contains(state,
                                                             state->selected_track_index,
                                                             state->selected_clip_index,
                                                             NULL);

    if (state->selection_count > 0 && (!selected_valid || selected_in_selection)) {
        int count = state->selection_count;
        if (count > TIMELINE_MAX_SELECTION) return;
        for (int i = 0; i < count; ++i) {
            temp_entries[temp_count++] = state->selection[i];
        }
    } else if (selected_valid) {
        temp_entries[temp_count++] = (TimelineSelectionEntry){
            .track_index = state->selected_track_index,
            .clip_index = state->selected_clip_index
        };
    }

    if (temp_count <= 0) {
        return;
    }

    TimelineClipboard* candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return;
    uint64_t anchor = UINT64_MAX;
    uint64_t selected_anchor = UINT64_MAX;
    for (int i = 0; i < temp_count; ++i) {
        TimelineSelectionEntry entry = temp_entries[i];
        if (entry.track_index < 0 || entry.track_index >= track_count) {
            goto failed;
        }
        const EngineTrack* track = &tracks[entry.track_index];
        if (!track || entry.clip_index < 0 || entry.clip_index >= track->clip_count) {
            goto failed;
        }
        const EngineClip* clip = &track->clips[entry.clip_index];
        if (!timeline_clip_is_timeline_region(clip)) {
            goto failed;
        }
        for (int j = 0; j < i; ++j)
            if (temp_entries[j].track_index == entry.track_index && temp_entries[j].clip_index == entry.clip_index) goto failed;
        TimelineClipboardEntry* dst = &candidate->entries[candidate->count++];
        if (!timeline_session_clip_from_engine(clip, &dst->clip)) {
            goto failed;
        }
        dst->track_index = entry.track_index;
        dst->start_frame = clip->timeline_start_frames;
        if (dst->start_frame < anchor) {
            anchor = dst->start_frame;
        }
        if (entry.track_index == state->selected_track_index &&
            entry.clip_index == state->selected_clip_index) {
            selected_anchor = dst->start_frame;
        }
    }

    if (anchor == UINT64_MAX) goto failed;
    if (selected_anchor != UINT64_MAX) {
        candidate->anchor_start_frame = selected_anchor;
    } else {
        candidate->anchor_start_frame = anchor;
    }
    timeline_clipboard_clear(&g_timeline_clipboard);
    g_timeline_clipboard = *candidate;
    free(candidate);
    return;
failed:
    timeline_clipboard_clear(candidate);
    free(candidate);
}

// Releases descriptor-owned automation adapters after insertion has cloned them.
static void timeline_clipboard_free_insertions(EngineClipInsert* entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; ++i) {
        EngineAutomationLane* lanes = (EngineAutomationLane*)entries[i].automation_lanes;
        if (lanes) for (int l = 0; l < entries[i].automation_lane_count; ++l) free(lanes[l].points);
        free(lanes);
    }
    free(entries);
}

// Publishes all pasted content and required topology with one pre-reserved history entry.
void timeline_clipboard_paste(AppState* state) {
    if (!state || !state->engine || state->undo.active_drag_valid || g_timeline_clipboard.count <= 0 ||
        state->selection_count < 0 || state->selection_count > TIMELINE_MAX_SELECTION) return;
    int count = g_timeline_clipboard.count;
    int previous_tracks = engine_get_track_count(state->engine);
    int destination = state->selected_track_index;
    if (destination < 0 || destination >= previous_tracks) destination = g_timeline_clipboard.entries[0].track_index;
    if (destination < 0) destination = 0;
    if (destination == INT_MAX) return;
    uint64_t playhead = engine_get_presentation_frame(state->engine);
    uint64_t anchor = g_timeline_clipboard.anchor_start_frame;
    EngineClipInsert* entries = calloc((size_t)count, sizeof(*entries));
    if (!entries) return;
    UndoCreatedTrack* guards = NULL;
    for (int i = 0; i < count; ++i) {
        const TimelineClipboardEntry* entry = &g_timeline_clipboard.entries[i];
        const SessionClip* source = &entry->clip;
        uint64_t offset = entry->start_frame > anchor ? entry->start_frame - anchor : 0;
        if (offset > UINT64_MAX - playhead) goto done;
        entries[i] = (EngineClipInsert){.kind = source->kind, .media_id = source->media_id, .media_path = source->media_path,
            .name = source->name, .automation_lane_count = source->automation_lane_count,
            .transform = {.start_frame = playhead + offset, .offset_frames = source->offset_frames,
                .duration_frames = source->duration_frames, .gain = source->gain,
                .fade_in_frames = source->fade_in_frames, .fade_out_frames = source->fade_out_frames,
                .fade_in_curve = source->fade_in_curve, .fade_out_curve = source->fade_out_curve,
                .instrument_preset = source->instrument_preset, .instrument_params = source->instrument_params,
                .instrument_inherits_track = source->instrument_inherits_track,
                .midi_notes = source->midi_notes, .midi_note_count = source->midi_note_count}};
        if (source->automation_lane_count) {
            EngineAutomationLane* lanes = calloc((size_t)source->automation_lane_count, sizeof(*lanes));
            if (!lanes) goto done;
            entries[i].automation_lanes = lanes;
            for (int l = 0; l < source->automation_lane_count; ++l) {
                const SessionAutomationLane* source_lane = &source->automation_lanes[l];
                lanes[l].target = source_lane->target; lanes[l].point_count = source_lane->point_count;
                if (source_lane->point_count) {
                    lanes[l].points = calloc((size_t)source_lane->point_count, sizeof(*lanes[l].points));
                    if (!lanes[l].points) goto done;
                    for (int n = 0; n < source_lane->point_count; ++n)
                        lanes[l].points[n] = (EngineAutomationPoint){source_lane->points[n].frame, source_lane->points[n].value};
                }
            }
        }
    }
    uint64_t before[TIMELINE_MAX_SELECTION] = {0}, after[TIMELINE_MAX_SELECTION] = {0};
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int before_count = state->selection_count;
    if (!before_count && state->selected_clip_index >= 0) before_count = 1;
    int primary = 0;
    for (int i = 0; i < before_count; ++i) {
        TimelineSelectionEntry entry = state->selection_count ? state->selection[i] :
            (TimelineSelectionEntry){state->selected_track_index, state->selected_clip_index};
        if (entry.track_index < 0 || entry.track_index >= previous_tracks || entry.clip_index < 0 ||
            entry.clip_index >= tracks[entry.track_index].clip_count) goto done;
        before[i] = tracks[entry.track_index].clips[entry.clip_index].creation_index;
        if (entry.track_index == state->selected_track_index && entry.clip_index == state->selected_clip_index) primary = i;
    }
    uint64_t first = before[0]; before[0] = before[primary]; before[primary] = first;
    int created_count = destination >= previous_tracks ? destination + 1 - previous_tracks : 0;
    if (created_count) { guards = calloc((size_t)created_count, sizeof(*guards)); if (!guards) goto done; }
    UndoCommand command = {.type = UNDO_CMD_CLIP_CONTENT};
    command.data.clip_content_selection = (UndoClipContentSelection){.count = before_count > count ? before_count : count,
        .before = before, .after = after, .created_start = previous_tracks, .created_count = created_count, .created_tracks = guards};
    if (!undo_manager_begin_drag(&state->undo, &command)) goto done;
    UndoCommand* pending = &state->undo.active_drag;
    if (!engine_clip_content_insert(state->engine, destination, entries, count,
        pending->data.clip_content_selection.after, &pending->clip_content_before, &pending->clip_content_after)) {
        undo_manager_cancel_drag(&state->undo); goto done;
    }
    for (int i = 0; i < created_count; ++i)
        undo_created_track_capture(&engine_get_tracks(state->engine)[previous_tracks + i], &pending->data.clip_content_selection.created_tracks[i]);
    memcpy(after, pending->data.clip_content_selection.after, (size_t)count * sizeof(*after));
    if (!undo_manager_commit_drag(&state->undo, pending)) goto done;
    timeline_selection_clear(state);
    tracks = engine_get_tracks(state->engine);
    for (int i = 0; i < count; ++i)
        for (int c = 0; c < tracks[destination].clip_count; ++c)
            if (tracks[destination].clips[c].creation_index == after[i]) timeline_selection_add(state, destination, c);
    if (state->selection_count) {
        timeline_selection_set_primary(state, state->selection[0].track_index, state->selection[0].clip_index);
        inspector_input_set_clip(state, state->selection[0].track_index, state->selection[0].clip_index);
    }
    effects_panel_sync_from_engine(state);
done:
    free(guards); timeline_clipboard_free_insertions(entries, count);
}
