#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_midi_region.h"
#include "time/tempo.h"
#include "ui/timeline_midi_clip_preview.h"
#include "undo/undo_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* message) {
    fprintf(stderr, "timeline_midi_region_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static void state_init(AppState* state, EngineRuntimeConfig* cfg) {
    memset(state, 0, sizeof(*state));
    config_set_defaults(cfg);
    cfg->sample_rate = 48000;
    state->runtime_cfg = *cfg;
    state->engine = engine_create(cfg);
    expect(state->engine != NULL, "engine_create failed");
    undo_manager_init(&state->undo);
    state->tempo = tempo_state_default(cfg->sample_rate);
    tempo_map_init(&state->tempo_map, cfg->sample_rate);
    time_signature_map_init(&state->time_signature_map);
    state->timeline_visible_seconds = 8.0f;
    state->timeline_snap_enabled = false;
    state->active_track_index = 0;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    state->timeline_drop_track_index = 0;
}

static void state_destroy(AppState* state) {
    undo_manager_free(&state->undo);
    time_signature_map_free(&state->time_signature_map);
    tempo_map_free(&state->tempo_map);
    engine_destroy(state->engine);
    state->engine = NULL;
}

static const EngineClip* only_clip(const AppState* state) {
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    expect(tracks != NULL, "tracks missing");
    expect(engine_get_track_count(state->engine) > 0, "track missing");
    expect(tracks[0].clip_count == 1, "expected one clip");
    return &tracks[0].clips[0];
}

static void test_create_selects_bar_length_midi_region(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    uint64_t playhead = (uint64_t)cfg.sample_rate;
    expect(engine_transport_seek(state.engine, playhead), "transport seek failed");

    int track_index = -1;
    int clip_index = -1;
    expect(timeline_midi_region_create_on_active_track(&state, &track_index, &clip_index),
           "MIDI region creation failed");
    expect(track_index == 0, "created region on wrong track");
    expect(clip_index == 0, "unexpected created clip index");
    expect(state.selected_track_index == 0, "selected track not updated");
    expect(state.selected_clip_index == 0, "selected clip not updated");
    expect(state.selection_count == 1, "selection list not updated");
    expect(state.inspector.visible, "inspector should follow created MIDI region");

    const EngineClip* clip = only_clip(&state);
    expect(engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI, "created clip should be MIDI");
    expect(clip->timeline_start_frames == playhead, "created clip start mismatch");
    expect(clip->duration_frames == (uint64_t)cfg.sample_rate * 2u, "default MIDI region should span one 4/4 bar at 120 BPM");
    expect(clip->instrument != NULL, "MIDI region should own an instrument source");

    state_destroy(&state);
}

static void test_create_undo_redo_rebuilds_midi_region(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    expect(timeline_midi_region_create_on_active_track(&state, NULL, NULL), "MIDI region creation failed");
    expect(only_clip(&state) != NULL, "created clip missing");

    expect(undo_manager_undo(&state.undo, &state), "undo failed");
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks && tracks[0].clip_count == 0, "undo should remove MIDI region");

    expect(undo_manager_redo(&state.undo, &state), "redo failed");
    const EngineClip* clip = only_clip(&state);
    expect(engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI, "redo should rebuild MIDI region");
    expect(clip->duration_frames == (uint64_t)cfg.sample_rate * 2u, "redo duration mismatch");

    state_destroy(&state);
}

static void test_midi_region_resize_bounds_follow_note_content(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, start, duration, &clip_index),
           "failed to create MIDI clip for resize bound test");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks && clip_index >= 0, "missing created MIDI clip");
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(timeline_clip_midi_min_duration_frames(clip) == 1u,
           "empty MIDI clip minimum duration should be one frame");

    uint64_t note_start = (uint64_t)cfg.sample_rate / 2u;
    uint64_t note_duration = (uint64_t)cfg.sample_rate / 4u;
    uint64_t note_end = note_start + note_duration;
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){note_start, note_duration, 60, 1.0f},
                                     NULL),
           "failed to add resize-bound MIDI note");

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(timeline_clip_midi_content_end_frame(clip) == note_end,
           "MIDI content end should follow last note end");
    expect(timeline_clip_midi_min_duration_frames(clip) == note_end,
           "MIDI minimum duration should clamp to content end");
    expect(engine_clip_set_region(state.engine, 0, clip_index, 0, duration * 2u),
           "MIDI right-edge extension should be allowed");
    expect(!engine_clip_set_region(state.engine, 0, clip_index, 0, note_end - 1u),
           "MIDI shrink below final note end should fail");
    expect(engine_clip_set_region(state.engine, 0, clip_index, 0, note_end),
           "MIDI shrink to final note end should succeed");

    state_destroy(&state);
}

static void test_midi_preview_x_position_stays_fixed_when_region_extends(void) {
    uint64_t sample_rate = 48000;
    uint64_t note_start = sample_rate / 2u;
    uint64_t short_duration = sample_rate * 2u;
    uint64_t long_duration = sample_rate * 4u;
    SDL_Rect short_rect = {100, 20, 200, 64};
    SDL_Rect long_rect = {100, 20, 400, 64};

    int short_x = timeline_midi_clip_preview_frame_to_x(&short_rect,
                                                        note_start,
                                                        0,
                                                        short_duration);
    int long_x = timeline_midi_clip_preview_frame_to_x(&long_rect,
                                                       note_start,
                                                       0,
                                                       long_duration);
    expect(short_x == long_x,
           "MIDI preview note X should stay fixed when only the region right edge extends");
    expect(short_x == 150, "MIDI preview note X should match timeline pixels per second");
}

int main(void) {
    test_create_selects_bar_length_midi_region();
    test_create_undo_redo_rebuilds_midi_region();
    test_midi_region_resize_bounds_follow_note_content();
    test_midi_preview_x_position_stays_fixed_when_region_extends();
    printf("timeline_midi_region_test: ok\n");
    return 0;
}
