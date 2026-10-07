#include "input/timeline/timeline_input_mouse_click.h"
#include "app_state.h"
#include "input/timeline_selection.h"
#include "ui/effects_panel.h"

// Applies the original track-header command to its surviving runtime identity on accepted release.
void timeline_track_control_activate(AppState* state, uint64_t key) {
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int i = 0; tracks && i < engine_get_track_count(state->engine); ++i) {
        if (tracks[i].runtime_id != key / 2) continue;
        bool before = key % 2 ? tracks[i].solo : tracks[i].muted;
        timeline_selection_set_single(state, i, -1);
        if (key % 2) engine_track_set_solo(state->engine, i, !before);
        else engine_track_set_muted(state->engine, i, !before);
        effects_panel_sync_from_engine(state);
        return;
    }
}
