#include "input/timeline_snap.h"

#include "app_state.h"
#include "engine/engine.h"
#include "time/tempo.h"
#include "ui/timeline_view.h"

#include <math.h>

static float snap_time_mode_interval(float visible_seconds) {
    if (visible_seconds <= 2.0f) return 0.125f;
    if (visible_seconds <= 4.0f) return 0.25f;
    if (visible_seconds <= 8.0f) return 0.5f;
    if (visible_seconds <= 16.0f) return 1.0f;
    if (visible_seconds <= 32.0f) return 2.0f;
    if (visible_seconds <= 60.0f) return 5.0f;
    return 10.0f;
}

static double snap_beat_subdivision(const TempoState* tempo, double visible_beats) {
    int beats_per_bar = (tempo && tempo->ts_num > 0) ? tempo->ts_num : 4;
    if (visible_beats <= 2.0) return 1.0 / 16.0;
    if (visible_beats <= 4.0) return 1.0 / 8.0;
    if (visible_beats <= 8.0) return 1.0 / 4.0;
    if (visible_beats <= 16.0) return 1.0 / 2.0;
    if (visible_beats <= 32.0) return 1.0;
    if (visible_beats <= 64.0) return 2.0;
    return (double)beats_per_bar;
}

float timeline_get_snap_interval_seconds(const AppState* state, float visible_seconds) {
    float fallback = TIMELINE_SNAP_SECONDS > 0.0f ? TIMELINE_SNAP_SECONDS : 0.25f;
    if (!state) {
        return fallback;
    }
    if (state->timeline_view_in_beats && state->tempo.bpm > 0.0f && state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sr = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
        if (sr > 0) {
            TempoState tempo = state->tempo;
            tempo.sample_rate = sr;
            tempo_state_clamp(&tempo);
            double visible_beats = tempo_seconds_to_beats((double)visible_seconds, &tempo);
            double subdivision = snap_beat_subdivision(&tempo, visible_beats);
            double interval_sec = tempo_beats_to_seconds(subdivision, &tempo);
            if (interval_sec > 0.0) {
                return (float)interval_sec;
            }
        }
    }
    float interval = snap_time_mode_interval(visible_seconds);
    if (interval <= 0.0f) {
        interval = fallback;
    }
    return interval;
}

float timeline_snap_seconds_to_grid(const AppState* state, float seconds, float visible_seconds) {
    if (!state) {
        return seconds;
    }
    if (!state->timeline_snap_enabled) {
        return seconds;
    }
    if (state->timeline_view_in_beats && state->tempo.bpm > 0.0f && state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sr = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
        if (sr > 0) {
            TempoState tempo = state->tempo;
            tempo.sample_rate = sr;
            tempo_state_clamp(&tempo);
            double visible_beats = tempo_seconds_to_beats((double)visible_seconds, &tempo);
            double subdivision = snap_beat_subdivision(&tempo, visible_beats);
            double beat_pos = tempo_seconds_to_beats((double)seconds, &tempo);
            double snapped_beats = floor(beat_pos / subdivision + 0.5) * subdivision;
            return (float)tempo_beats_to_seconds(snapped_beats, &tempo);
        }
    }
    float interval = snap_time_mode_interval(visible_seconds);
    if (interval <= 0.0f) {
        return seconds;
    }
    float snapped = roundf(seconds / interval) * interval;
    if (snapped < 0.0f) snapped = 0.0f;
    return snapped;
}
