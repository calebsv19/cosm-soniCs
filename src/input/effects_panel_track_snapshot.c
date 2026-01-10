#include "input/effects_panel_track_snapshot.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool resolve_target_track(const AppState* state, int* out_track_index) {
    if (!state || !out_track_index || !state->engine) {
        return false;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    if (panel->target != FX_PANEL_TARGET_TRACK || panel->target_track_index < 0) {
        return false;
    }
    int track_count = engine_get_track_count(state->engine);
    if (panel->target_track_index >= track_count) {
        return false;
    }
    *out_track_index = panel->target_track_index;
    return true;
}

static float gain_db_from_mouse(const SDL_Rect* rect, int mouse_x) {
    if (!rect || rect->w <= 0) {
        return 0.0f;
    }
    float t = (float)(mouse_x - rect->x) / (float)rect->w;
    t = clampf(t, 0.0f, 1.0f);
    return -20.0f + t * 40.0f;
}

static float gain_linear_from_db(float db) {
    return powf(10.0f, db / 20.0f);
}

static float pan_from_mouse(const SDL_Rect* rect, int mouse_x) {
    if (!rect || rect->w <= 0) {
        return 0.0f;
    }
    float t = (float)(mouse_x - rect->x) / (float)rect->w;
    t = clampf(t, 0.0f, 1.0f);
    return -1.0f + t * 2.0f;
}

static void apply_gain(AppState* state, float gain_db) {
    if (!state) {
        return;
    }
    float linear = gain_linear_from_db(gain_db);
    if (linear < 0.000001f) linear = 0.000001f;
    state->effects_panel.track_snapshot.gain = linear;
    int track_index = -1;
    if (resolve_target_track(state, &track_index)) {
        engine_track_set_gain(state->engine, track_index, linear);
    }
}

static void apply_pan(AppState* state, float pan) {
    if (!state) {
        return;
    }
    state->effects_panel.track_snapshot.pan = clampf(pan, -1.0f, 1.0f);
    int track_index = -1;
    if (resolve_target_track(state, &track_index)) {
        engine_track_set_pan(state->engine, track_index, state->effects_panel.track_snapshot.pan);
    }
}

static void toggle_mute(AppState* state) {
    if (!state) {
        return;
    }
    int track_index = -1;
    if (resolve_target_track(state, &track_index)) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (tracks && track_index >= 0) {
            engine_track_set_muted(state->engine, track_index, !tracks[track_index].muted);
        }
    } else {
        state->effects_panel.track_snapshot.muted = !state->effects_panel.track_snapshot.muted;
    }
}

static void toggle_solo(AppState* state) {
    if (!state) {
        return;
    }
    int track_index = -1;
    if (resolve_target_track(state, &track_index)) {
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (tracks && track_index >= 0) {
            engine_track_set_solo(state->engine, track_index, !tracks[track_index].solo);
        }
    } else {
        state->effects_panel.track_snapshot.solo = !state->effects_panel.track_snapshot.solo;
    }
}

bool effects_panel_track_snapshot_handle_mouse_down(AppState* state,
                                                    const EffectsPanelLayout* layout,
                                                    const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    SDL_Point pt = {event->button.x, event->button.y};
    const EffectsPanelTrackSnapshotLayout* snap = &layout->track_snapshot;
    EffectsPanelTrackSnapshotState* snap_state = &state->effects_panel.track_snapshot;

    if (SDL_PointInRect(&pt, &snap->eq_rect)) {
        Uint32 now = SDL_GetTicks();
        bool is_double = event->button.clicks >= 2;
        if (!is_double && snap_state->last_click_ticks != 0 && now - snap_state->last_click_ticks <= 500) {
            is_double = true;
        }
        snap_state->last_click_ticks = now;
        if (is_double) {
            snap_state->eq_open = true;
            state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EQ;
        }
        return true;
    }

    if (SDL_PointInRect(&pt, &snap->gain_hit_rect)) {
        snap_state->dragging = true;
        snap_state->active_control = FX_SNAPSHOT_CONTROL_GAIN;
        float gain_db = gain_db_from_mouse(&snap->gain_hit_rect, event->button.x);
        apply_gain(state, gain_db);
        return true;
    }

    if (SDL_PointInRect(&pt, &snap->pan_hit_rect)) {
        snap_state->dragging = true;
        snap_state->active_control = FX_SNAPSHOT_CONTROL_PAN;
        float pan = pan_from_mouse(&snap->pan_hit_rect, event->button.x);
        apply_pan(state, pan);
        return true;
    }

    if (SDL_PointInRect(&pt, &snap->mute_rect)) {
        toggle_mute(state);
        return true;
    }

    if (SDL_PointInRect(&pt, &snap->solo_rect)) {
        toggle_solo(state);
        return true;
    }

    return false;
}

bool effects_panel_track_snapshot_handle_mouse_up(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    EffectsPanelTrackSnapshotState* snap_state = &state->effects_panel.track_snapshot;
    if (!snap_state->dragging) {
        return false;
    }
    snap_state->dragging = false;
    snap_state->active_control = FX_SNAPSHOT_CONTROL_NONE;
    return true;
}

bool effects_panel_track_snapshot_handle_mouse_motion(AppState* state,
                                                      const EffectsPanelLayout* layout,
                                                      const SDL_Event* event) {
    if (!state || !layout || !event) {
        return false;
    }
    const EffectsPanelTrackSnapshotLayout* snap = &layout->track_snapshot;
    EffectsPanelTrackSnapshotState* snap_state = &state->effects_panel.track_snapshot;
    if (!snap_state->dragging) {
        return false;
    }
    if (snap_state->active_control == FX_SNAPSHOT_CONTROL_GAIN) {
        float gain_db = gain_db_from_mouse(&snap->gain_hit_rect, event->motion.x);
        apply_gain(state, gain_db);
        return true;
    }
    if (snap_state->active_control == FX_SNAPSHOT_CONTROL_PAN) {
        float pan = pan_from_mouse(&snap->pan_hit_rect, event->motion.x);
        apply_pan(state, pan);
        return true;
    }
    return false;
}
