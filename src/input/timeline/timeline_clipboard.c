#include "input/timeline/timeline_clipboard.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline_drag.h"
#include "input/timeline_selection.h"
#include "ui/effects_panel.h"
#include "undo/undo_manager.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    SessionClip clip;
    int track_index;
    uint64_t start_frame;
} TimelineClipboardEntry;

static struct {
    TimelineClipboardEntry entries[TIMELINE_MAX_SELECTION];
    int count;
    uint64_t anchor_start_frame;
} g_timeline_clipboard = {0};

static bool timeline_clipboard_apply_automation(Engine* engine,
                                                int track_index,
                                                int clip_index,
                                                const SessionClip* src) {
    if (!engine || !src) {
        return false;
    }
    for (int l = 0; l < src->automation_lane_count; ++l) {
        const SessionAutomationLane* lane = &src->automation_lanes[l];
        if (!engine_clip_set_automation_lane_points(engine,
                                                    track_index,
                                                    clip_index,
                                                    lane->target,
                                                    (const EngineAutomationPoint*)lane->points,
                                                    lane->point_count)) {
            return false;
        }
    }
    return true;
}

static bool timeline_clipboard_add_clip_from_snapshot(AppState* state,
                                                      const SessionClip* src,
                                                      int target_track,
                                                      uint64_t start_frame,
                                                      int* out_clip_index) {
    if (!state || !state->engine || !src || target_track < 0) {
        return false;
    }

    int new_clip_index = -1;
    if (src->kind == ENGINE_CLIP_KIND_MIDI) {
        if (!engine_add_midi_clip_to_track(state->engine,
                                           target_track,
                                           start_frame,
                                           src->duration_frames,
                                           &new_clip_index)) {
            return false;
        }
        engine_clip_midi_set_instrument_preset(state->engine,
                                               target_track,
                                               new_clip_index,
                                               src->instrument_preset);
        engine_clip_midi_set_instrument_params(state->engine,
                                               target_track,
                                               new_clip_index,
                                               src->instrument_params);
        engine_clip_midi_set_inherits_track_instrument(state->engine,
                                                       target_track,
                                                       new_clip_index,
                                                       src->instrument_inherits_track);
        for (int n = 0; n < src->midi_note_count; ++n) {
            if (!engine_clip_midi_add_note(state->engine,
                                           target_track,
                                           new_clip_index,
                                           src->midi_notes[n],
                                           NULL)) {
                return false;
            }
        }
    } else {
        if (src->media_path[0] == '\0') {
            return false;
        }
        const char* media_id = src->media_id[0] != '\0' ? src->media_id : NULL;
        if (!engine_add_clip_to_track_with_id(state->engine,
                                              target_track,
                                              src->media_path,
                                              media_id,
                                              start_frame,
                                              &new_clip_index)) {
            return false;
        }
    }

    if (src->name[0] != '\0') {
        engine_clip_set_name(state->engine, target_track, new_clip_index, src->name);
    }
    engine_clip_set_gain(state->engine, target_track, new_clip_index, src->gain == 0.0f ? 1.0f : src->gain);
    engine_clip_set_region(state->engine, target_track, new_clip_index, src->offset_frames, src->duration_frames);
    engine_clip_set_fades(state->engine, target_track, new_clip_index, src->fade_in_frames, src->fade_out_frames);
    engine_clip_set_fade_curves(state->engine,
                                target_track,
                                new_clip_index,
                                src->fade_in_curve,
                                src->fade_out_curve);
    if (!timeline_clipboard_apply_automation(state->engine, target_track, new_clip_index, src)) {
        return false;
    }

    int sorted_index = new_clip_index;
    engine_clip_set_timeline_start(state->engine, target_track, new_clip_index, start_frame, &sorted_index);
    if (out_clip_index) {
        *out_clip_index = sorted_index;
    }
    return true;
}

static void timeline_clipboard_clear(void) {
    for (int i = 0; i < g_timeline_clipboard.count; ++i) {
        timeline_session_clip_clear(&g_timeline_clipboard.entries[i].clip);
    }
    g_timeline_clipboard.count = 0;
    g_timeline_clipboard.anchor_start_frame = 0;
}

void timeline_clipboard_copy(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    timeline_clipboard_clear();

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
        if (count > TIMELINE_MAX_SELECTION) count = TIMELINE_MAX_SELECTION;
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

    uint64_t anchor = UINT64_MAX;
    uint64_t selected_anchor = UINT64_MAX;
    for (int i = 0; i < temp_count && g_timeline_clipboard.count < TIMELINE_MAX_SELECTION; ++i) {
        TimelineSelectionEntry entry = temp_entries[i];
        if (entry.track_index < 0 || entry.track_index >= track_count) {
            continue;
        }
        const EngineTrack* track = &tracks[entry.track_index];
        if (!track || entry.clip_index < 0 || entry.clip_index >= track->clip_count) {
            continue;
        }
        const EngineClip* clip = &track->clips[entry.clip_index];
        if (!timeline_clip_is_timeline_region(clip)) {
            continue;
        }
        TimelineClipboardEntry* dst = &g_timeline_clipboard.entries[g_timeline_clipboard.count];
        if (!timeline_session_clip_from_engine(clip, &dst->clip)) {
            continue;
        }
        dst->track_index = entry.track_index;
        dst->start_frame = clip->timeline_start_frames;
        g_timeline_clipboard.count++;
        if (dst->start_frame < anchor) {
            anchor = dst->start_frame;
        }
        if (entry.track_index == state->selected_track_index &&
            entry.clip_index == state->selected_clip_index) {
            selected_anchor = dst->start_frame;
        }
    }

    if (anchor == UINT64_MAX) {
        timeline_clipboard_clear();
        return;
    }
    if (selected_anchor != UINT64_MAX) {
        g_timeline_clipboard.anchor_start_frame = selected_anchor;
    } else {
        g_timeline_clipboard.anchor_start_frame = anchor;
    }
}

void timeline_clipboard_paste(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (g_timeline_clipboard.count <= 0) {
        return;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
    if (sample_rate <= 0) {
        return;
    }
    uint64_t playhead = engine_get_transport_frame(state->engine);
    uint64_t anchor = g_timeline_clipboard.anchor_start_frame;
    int track_count = engine_get_track_count(state->engine);
    int target_track = state->selected_track_index;
    if (target_track < 0 || target_track >= track_count) {
        target_track = g_timeline_clipboard.entries[0].track_index;
    }
    if (target_track < 0) target_track = 0;
    while (target_track >= track_count) {
        engine_add_track(state->engine);
        track_count = engine_get_track_count(state->engine);
    }

    TimelineSelectionEntry new_sel[TIMELINE_MAX_SELECTION];
    int new_count = 0;

    for (int i = 0; i < g_timeline_clipboard.count && new_count < TIMELINE_MAX_SELECTION; ++i) {
        const TimelineClipboardEntry* src = &g_timeline_clipboard.entries[i];
        uint64_t desired_start = playhead + (src->start_frame > anchor ? (src->start_frame - anchor) : 0);
        int dup_index = -1;
        if (!timeline_clipboard_add_clip_from_snapshot(state,
                                                       &src->clip,
                                                       target_track,
                                                       desired_start,
                                                       &dup_index) ||
            dup_index < 0) {
            continue;
        }

        new_sel[new_count].track_index = target_track;
        new_sel[new_count].clip_index = dup_index;
        new_count++;

        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (tracks && target_track >= 0 && target_track < engine_get_track_count(state->engine)) {
            const EngineTrack* track = &tracks[target_track];
            if (track && dup_index >= 0 && dup_index < track->clip_count) {
                const EngineClip* clip = &track->clips[dup_index];
                UndoCommand cmd = {0};
                cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
                cmd.data.clip_add_remove.added = true;
                cmd.data.clip_add_remove.track_index = target_track;
                cmd.data.clip_add_remove.sampler = clip->sampler;
                if (timeline_session_clip_from_engine(clip, &cmd.data.clip_add_remove.clip)) {
                    undo_manager_push(&state->undo, &cmd);
                    timeline_session_clip_clear(&cmd.data.clip_add_remove.clip);
                }
            }
        }
    }

    if (new_count > 0) {
        timeline_selection_clear(state);
        for (int i = 0; i < new_count; ++i) {
            timeline_selection_add(state, new_sel[i].track_index, new_sel[i].clip_index);
        }
        state->active_track_index = new_sel[0].track_index;
        state->selected_track_index = new_sel[0].track_index;
        state->selected_clip_index = new_sel[0].clip_index;
        effects_panel_sync_from_engine(state);
    }
}
