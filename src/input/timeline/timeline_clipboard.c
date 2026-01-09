#include "input/timeline/timeline_clipboard.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/timeline_drag.h"
#include "input/timeline_selection.h"

#include <stdint.h>

typedef struct {
    EngineSamplerSource* sampler;
    int track_index;
    uint64_t start_frame;
} TimelineClipboardEntry;

static struct {
    TimelineClipboardEntry entries[TIMELINE_MAX_SELECTION];
    int count;
    uint64_t anchor_start_frame;
} g_timeline_clipboard = {0};

static void timeline_clipboard_clear(void) {
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
    if (state->selection_count > 0) {
        int count = state->selection_count;
        if (count > TIMELINE_MAX_SELECTION) count = TIMELINE_MAX_SELECTION;
        for (int i = 0; i < count; ++i) {
            temp_entries[temp_count++] = state->selection[i];
        }
    } else if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
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
        if (!clip || !clip->sampler) {
            continue;
        }
        TimelineClipboardEntry* dst = &g_timeline_clipboard.entries[g_timeline_clipboard.count++];
        dst->sampler = clip->sampler;
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
        if (!src->sampler) {
            continue;
        }
        int clip_track = -1;
        int clip_idx = -1;
        if (!timeline_find_clip_by_sampler(state, src->sampler, &clip_track, &clip_idx)) {
            continue;
        }
        uint64_t desired_start = playhead + (src->start_frame > anchor ? (src->start_frame - anchor) : 0);
        int dup_index = -1;
        if (!engine_duplicate_clip(state->engine, clip_track, clip_idx, 0, &dup_index) || dup_index < 0) {
            continue;
        }
        int updated_index = dup_index;
        engine_clip_set_timeline_start(state->engine, clip_track, dup_index, desired_start, &updated_index);
        dup_index = updated_index;

        if (target_track != clip_track) {
            int moved = timeline_move_clip_to_track(state, clip_track, dup_index, target_track, desired_start);
            if (moved >= 0) {
                dup_index = moved;
                clip_track = target_track;
            }
        }

        new_sel[new_count].track_index = clip_track;
        new_sel[new_count].clip_index = dup_index;
        new_count++;
    }

    if (new_count > 0) {
        timeline_selection_clear(state);
        for (int i = 0; i < new_count; ++i) {
            timeline_selection_add(state, new_sel[i].track_index, new_sel[i].clip_index);
        }
        state->active_track_index = new_sel[0].track_index;
        state->selected_track_index = new_sel[0].track_index;
        state->selected_clip_index = new_sel[0].clip_index;
    }
}
