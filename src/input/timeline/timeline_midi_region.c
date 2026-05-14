#include "input/timeline/timeline_midi_region.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/inspector_input.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline_selection.h"
#include "input/timeline_snap.h"
#include "time/tempo.h"
#include "ui/timeline_view.h"
#include "undo/undo_manager.h"

#include <math.h>
#include <stddef.h>

static int timeline_midi_region_target_track(const AppState* state) {
    if (!state || !state->engine) {
        return -1;
    }
    int track_count = engine_get_track_count(state->engine);
    if (track_count <= 0) {
        return -1;
    }
    int candidates[] = {
        state->active_track_index,
        state->selected_track_index,
        state->timeline_drop_track_index,
        0
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int track_index = candidates[i];
        if (track_index >= 0 && track_index < track_count) {
            return track_index;
        }
    }
    return -1;
}

static int timeline_midi_region_sample_rate(const AppState* state) {
    if (!state) {
        return 48000;
    }
    if (state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        if (cfg && cfg->sample_rate > 0) {
            return cfg->sample_rate;
        }
    }
    if (state->runtime_cfg.sample_rate > 0) {
        return state->runtime_cfg.sample_rate;
    }
    return 48000;
}

uint64_t timeline_midi_region_default_duration_frames(const AppState* state, uint64_t start_frame) {
    int sample_rate = timeline_midi_region_sample_rate(state);
    if (state && state->tempo_map.event_count > 0) {
        double start_beat = tempo_map_samples_to_beats(&state->tempo_map, (int64_t)start_frame);
        const TimeSignatureEvent* sig = time_signature_map_event_at_beat(&state->time_signature_map, start_beat);
        double beats_per_bar = time_signature_beats_per_bar(sig);
        if (beats_per_bar <= 0.0) {
            beats_per_bar = 4.0;
        }
        double end_samples = tempo_map_beats_to_samples(&state->tempo_map, start_beat + beats_per_bar);
        double duration = end_samples - (double)start_frame;
        if (duration > 0.0) {
            return (uint64_t)llround(duration);
        }
    }
    return (uint64_t)sample_rate * 2u;
}

bool timeline_midi_region_create_on_active_track(AppState* state,
                                                 int* out_track_index,
                                                 int* out_clip_index) {
    if (out_track_index) {
        *out_track_index = -1;
    }
    if (out_clip_index) {
        *out_clip_index = -1;
    }
    if (!state || !state->engine) {
        return false;
    }
    int track_index = timeline_midi_region_target_track(state);
    if (track_index < 0) {
        return false;
    }

    int sample_rate = timeline_midi_region_sample_rate(state);
    uint64_t start_frame = engine_get_transport_frame(state->engine);
    if (state->timeline_snap_enabled && sample_rate > 0) {
        float visible = state->timeline_visible_seconds > 0.0f
            ? state->timeline_visible_seconds
            : TIMELINE_DEFAULT_VISIBLE_SECONDS;
        float seconds = (float)((double)start_frame / (double)sample_rate);
        float snapped = timeline_snap_seconds_to_grid(state, seconds, visible);
        if (snapped >= 0.0f) {
            start_frame = (uint64_t)llroundf(snapped * (float)sample_rate);
        }
    }
    uint64_t duration = timeline_midi_region_default_duration_frames(state, start_frame);
    if (duration == 0) {
        return false;
    }

    int clip_index = -1;
    if (!engine_add_midi_clip_to_track(state->engine, track_index, start_frame, duration, &clip_index)) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index >= track_count || clip_index < 0 || clip_index >= tracks[track_index].clip_count) {
        return false;
    }
    const EngineClip* clip = &tracks[track_index].clips[clip_index];

    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
    cmd.data.clip_add_remove.added = true;
    cmd.data.clip_add_remove.track_index = track_index;
    cmd.data.clip_add_remove.sampler = NULL;
    if (timeline_session_clip_from_engine(clip, &cmd.data.clip_add_remove.clip)) {
        undo_manager_push(&state->undo, &cmd);
        timeline_session_clip_clear(&cmd.data.clip_add_remove.clip);
    }

    timeline_selection_set_single(state, track_index, clip_index);
    inspector_input_show(state, track_index, clip_index, clip);
    state->active_track_index = track_index;
    state->timeline_drop_track_index = track_index;

    if (out_track_index) {
        *out_track_index = track_index;
    }
    if (out_clip_index) {
        *out_clip_index = clip_index;
    }
    return true;
}
