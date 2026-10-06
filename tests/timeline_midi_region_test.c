#include "app_state.h"
#include "config.h"
#include "audio/wav_writer.h"
#include <math.h>
#include "engine/engine.h"
#include "engine/engine_internal.h"
#include "input/timeline/timeline_clipboard.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_midi_region.h"
#include "input/timeline/timeline_midi_trim.h"
#include "input/timeline_selection.h"
#include "input/timeline_drag.h"
#include "input/input_manager.h"
#include "input/timeline/timeline_input_keyboard.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_clip_press.h"
#include "input/timeline/timeline_input_mouse_drag.h"
#include "time/tempo.h"
#include "ui/timeline_midi_clip_preview.h"
#include "undo/undo_manager.h"

#include "test_wav_fixture.h"

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void expect_note(const EngineMidiNote* note,
                        uint64_t start_frame,
                        uint64_t duration_frames,
                        uint8_t pitch,
                        const char* message) {
    expect(note != NULL, message);
    expect(note->start_frame == start_frame, message);
    expect(note->duration_frames == duration_frames, message);
    expect(note->note == pitch, message);
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

static void test_midi_left_trim_later_removes_and_clips_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, start, duration, &clip_index),
           "failed to create MIDI clip for left trim test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, 12000, 60, 0.75f},
                                     NULL),
           "failed to add removed note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){12000, 24000, 62, 0.8f},
                                     NULL),
           "failed to add crossing note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){48000, 12000, 64, 0.9f},
                                     NULL),
           "failed to add retained note");

    int trim_index = clip_index;
    expect(timeline_midi_left_trim_apply(state.engine, 0, &trim_index, start + 24000),
           "MIDI left trim later should apply");
    const EngineClip* clip = only_clip(&state);
    expect(trim_index == 0, "trimmed clip index mismatch");
    expect(clip->timeline_start_frames == start + 24000, "left trim start mismatch");
    expect(clip->duration_frames == duration - 24000, "left trim duration mismatch");
    expect(clip->offset_frames == 0, "MIDI left trim should keep offset at zero");
    expect(engine_clip_midi_note_count(clip) == 2, "left trim note count mismatch");
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect_note(&notes[0], 0, 12000, 62, "crossing note should be clipped to local zero");
    expect_note(&notes[1], 24000, 12000, 64, "later note should shift left by trim delta");

    state_destroy(&state);
}

static void test_midi_left_trim_earlier_preserves_absolute_note_positions(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, start, duration, &clip_index),
           "failed to create MIDI clip for left extension test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){24000, 12000, 67, 0.7f},
                                     NULL),
           "failed to add extension test note");

    int trim_index = clip_index;
    expect(timeline_midi_left_trim_apply(state.engine, 0, &trim_index, start - 12000),
           "MIDI left trim earlier should apply");
    const EngineClip* clip = only_clip(&state);
    expect(clip->timeline_start_frames == start - 12000, "left extension start mismatch");
    expect(clip->duration_frames == duration + 12000, "left extension duration mismatch");
    expect(clip->offset_frames == 0, "MIDI left extension should keep offset at zero");
    expect(engine_clip_midi_note_count(clip) == 1, "left extension note count mismatch");
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect_note(&notes[0], 36000, 12000, 67, "left extension should shift notes right locally");

    state_destroy(&state);
}

static void test_midi_left_trim_undo_redo_restores_note_contents(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, start, duration, &clip_index),
           "failed to create MIDI clip for undo test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){12000, 24000, 62, 0.8f},
                                     NULL),
           "failed to add undo crossing note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){48000, 12000, 64, 0.9f},
                                     NULL),
           "failed to add undo retained note");

    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_CLIP_TRANSFORM;
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks != NULL, "tracks missing for undo test");
    expect(undo_clip_state_from_engine_clip(&tracks[0].clips[clip_index],
                                            0,
                                            &cmd.data.clip_transform.before),
           "failed to capture undo before state");
    int trim_index = clip_index;
    expect(timeline_midi_left_trim_apply(state.engine, 0, &trim_index, start + 24000),
           "MIDI left trim should apply before undo capture");
    tracks = engine_get_tracks(state.engine);
    expect(undo_clip_state_from_engine_clip(&tracks[0].clips[trim_index],
                                            0,
                                            &cmd.data.clip_transform.after),
           "failed to capture undo after state");
    expect(undo_manager_push(&state.undo, &cmd), "failed to push MIDI left trim undo");
    undo_clip_state_clear(&cmd.data.clip_transform.before);
    undo_clip_state_clear(&cmd.data.clip_transform.after);

    expect(undo_manager_undo(&state.undo, &state), "MIDI left trim undo failed");
    const EngineClip* clip = only_clip(&state);
    expect(clip->timeline_start_frames == start, "undo should restore MIDI region start");
    expect(clip->duration_frames == duration, "undo should restore MIDI region duration");
    expect(engine_clip_midi_note_count(clip) == 2, "undo should restore original notes");
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect_note(&notes[0], 12000, 24000, 62, "undo should restore crossing note");
    expect_note(&notes[1], 48000, 12000, 64, "undo should restore retained note");

    expect(undo_manager_redo(&state.undo, &state), "MIDI left trim redo failed");
    clip = only_clip(&state);
    expect(clip->timeline_start_frames == start + 24000, "redo should restore trimmed start");
    expect(clip->duration_frames == duration - 24000, "redo should restore trimmed duration");
    expect(engine_clip_midi_note_count(clip) == 2, "redo should restore trimmed notes");
    notes = engine_clip_midi_notes(clip);
    expect_note(&notes[0], 0, 12000, 62, "redo should restore clipped crossing note");
    expect_note(&notes[1], 24000, 12000, 64, "redo should restore shifted retained note");

    state_destroy(&state);
}

static void test_midi_left_trim_same_drag_restores_covered_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    EngineMidiNote original_notes[3] = {
        {0, 12000, 60, 0.75f},
        {12000, 24000, 62, 0.8f},
        {48000, 12000, 64, 0.9f},
    };
    expect(engine_add_midi_clip_to_track(state.engine, 0, start, duration, &clip_index),
           "failed to create MIDI clip for same-drag restore test");
    for (int i = 0; i < 3; ++i) {
        expect(engine_clip_midi_add_note(state.engine, 0, clip_index, original_notes[i], NULL),
               "failed to add same-drag restore note");
    }

    int trim_index = clip_index;
    expect(timeline_midi_left_trim_apply_from_notes(state.engine,
                                                   0,
                                                   &trim_index,
                                                   start,
                                                   duration,
                                                   original_notes,
                                                   3,
                                                   start + 24000),
           "same-drag right trim should apply from original notes");
    const EngineClip* clip = only_clip(&state);
    expect(engine_clip_midi_note_count(clip) == 2, "same-drag right trim should hide covered notes");

    expect(timeline_midi_left_trim_apply_from_notes(state.engine,
                                                   0,
                                                   &trim_index,
                                                   start,
                                                   duration,
                                                   original_notes,
                                                   3,
                                                   start),
           "same-drag left restore should apply from original notes");
    clip = only_clip(&state);
    expect(clip->timeline_start_frames == start, "same-drag restore start mismatch");
    expect(clip->duration_frames == duration, "same-drag restore duration mismatch");
    expect(engine_clip_midi_note_count(clip) == 3, "same-drag restore should bring covered notes back");
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect_note(&notes[0], 0, 12000, 60, "same-drag restore should restore covered note");
    expect_note(&notes[1], 12000, 24000, 62, "same-drag restore should restore crossing note");
    expect_note(&notes[2], 48000, 12000, 64, "same-drag restore should restore retained note");

    state_destroy(&state);
}

static void test_midi_clipboard_pastes_region_to_selected_track(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int destination_track = engine_add_track(state.engine);
    expect(destination_track == 1, "destination track should be created after source track");

    int clip_index = -1;
    uint64_t source_start = (uint64_t)cfg.sample_rate;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, source_start, duration, &clip_index),
           "failed to add source MIDI region");
    engine_clip_set_name(state.engine, 0, clip_index, "Layer source");
    expect(engine_clip_midi_set_instrument_preset(state.engine,
                                                  0,
                                                  clip_index,
                                                  ENGINE_INSTRUMENT_PRESET_WARM_KEYS),
           "failed to set source preset");
    EngineInstrumentParams params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_WARM_KEYS);
    params.level = 0.42f;
    params.osc_mix = 0.33f;
    expect(engine_clip_midi_set_instrument_params(state.engine, 0, clip_index, params),
           "failed to set source params");

    EngineMidiNote notes[2] = {
        {.start_frame = 1200, .duration_frames = 2400, .note = 60, .velocity = 0.55f},
        {.start_frame = 3600, .duration_frames = 2400, .note = 67, .velocity = 0.70f}
    };
    for (int i = 0; i < 2; ++i) {
        expect(engine_clip_midi_add_note(state.engine, 0, clip_index, notes[i], NULL),
               "failed to add source MIDI note");
    }

    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    timeline_clipboard_copy(&state);

    uint64_t paste_start = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_transport_seek(state.engine, paste_start), "failed to seek paste playhead");
    timeline_selection_set_single(&state, destination_track, -1);
    timeline_clipboard_paste(&state);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks != NULL, "tracks missing after paste");
    expect(tracks[0].clip_count == 1, "copy should leave source MIDI region in place");
    expect(tracks[destination_track].clip_count == 1, "paste should create one MIDI region on destination track");
    expect(state.selected_track_index == destination_track, "pasted MIDI region should become selected on destination track");
    expect(state.selected_clip_index == 0, "pasted MIDI selection index mismatch");

    const EngineClip* pasted = &tracks[destination_track].clips[0];
    expect(engine_clip_get_kind(pasted) == ENGINE_CLIP_KIND_MIDI, "pasted region should be MIDI");
    expect(pasted->timeline_start_frames == paste_start, "pasted MIDI region start mismatch");
    expect(pasted->duration_frames == duration, "pasted MIDI duration mismatch");
    expect(engine_clip_midi_instrument_preset(pasted) == ENGINE_INSTRUMENT_PRESET_WARM_KEYS,
           "pasted MIDI preset mismatch");
    EngineInstrumentParams pasted_params = engine_clip_midi_instrument_params(pasted);
    expect(pasted_params.level == params.level && pasted_params.osc_mix == params.osc_mix,
           "pasted MIDI params mismatch");
    expect(engine_clip_midi_note_count(pasted) == 2, "pasted MIDI note count mismatch");
    const EngineMidiNote* pasted_notes = engine_clip_midi_notes(pasted);
    expect_note(&pasted_notes[0], notes[0].start_frame, notes[0].duration_frames, notes[0].note,
                "first pasted MIDI note mismatch");
    expect_note(&pasted_notes[1], notes[1].start_frame, notes[1].duration_frames, notes[1].note,
                "second pasted MIDI note mismatch");

    expect(undo_manager_undo(&state.undo, &state), "undo paste failed");
    tracks = engine_get_tracks(state.engine);
    expect(tracks[0].clip_count == 1, "undo paste should preserve source region");
    expect(tracks[destination_track].clip_count == 0, "undo paste should remove destination region");

    expect(undo_manager_redo(&state.undo, &state), "redo paste failed");
    tracks = engine_get_tracks(state.engine);
    expect(tracks[destination_track].clip_count == 1, "redo paste should restore destination region");
    pasted = &tracks[destination_track].clips[0];
    expect(engine_clip_get_kind(pasted) == ENGINE_CLIP_KIND_MIDI, "redo restored region should be MIDI");
    expect(engine_clip_midi_note_count(pasted) == 2, "redo restored MIDI notes mismatch");

    state_destroy(&state);
}

static void test_midi_track_default_inheritance_and_region_override(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int destination_track = engine_add_track(state.engine);
    expect(destination_track == 1, "destination track should be created for inheritance test");
    expect(engine_track_midi_set_instrument_preset(state.engine,
                                                   0,
                                                   ENGINE_INSTRUMENT_PRESET_SOFT_PAD),
           "failed to set source track default");
    expect(engine_track_midi_set_instrument_preset(state.engine,
                                                   destination_track,
                                                   ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS),
           "failed to set destination track default");

    int clip_index = -1;
    uint64_t duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, duration, &clip_index),
           "failed to add inherited MIDI region");
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks != NULL, "tracks missing after inherited MIDI add");
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_inherits_track_instrument(clip), "new MIDI region should inherit track instrument");
    expect(engine_clip_midi_effective_instrument_preset(state.engine, 0, clip_index) ==
               ENGINE_INSTRUMENT_PRESET_SOFT_PAD,
           "new MIDI region should use source track default");

    expect(engine_track_midi_set_instrument_preset(state.engine,
                                                   0,
                                                   ENGINE_INSTRUMENT_PRESET_WARM_KEYS),
           "failed to change source track default");
    expect(engine_clip_midi_effective_instrument_preset(state.engine, 0, clip_index) ==
               ENGINE_INSTRUMENT_PRESET_WARM_KEYS,
           "inherited MIDI region should follow track default changes");

    expect(engine_clip_midi_set_instrument_preset(state.engine,
                                                  0,
                                                  clip_index,
                                                  ENGINE_INSTRUMENT_PRESET_PLUCK),
           "failed to set explicit region preset");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(!engine_clip_midi_inherits_track_instrument(clip), "region preset edit should break inheritance");
    expect(engine_track_midi_set_instrument_preset(state.engine,
                                                   0,
                                                   ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD),
           "failed to change source default after override");
    expect(engine_clip_midi_effective_instrument_preset(state.engine, 0, clip_index) ==
               ENGINE_INSTRUMENT_PRESET_PLUCK,
           "explicit region preset should ignore later track default changes");

    expect(engine_clip_midi_set_inherits_track_instrument(state.engine, 0, clip_index, true),
           "failed to restore region inheritance");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    timeline_clipboard_copy(&state);
    uint64_t paste_start = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_transport_seek(state.engine, paste_start), "failed to seek inheritance paste playhead");
    timeline_selection_set_single(&state, destination_track, -1);
    timeline_clipboard_paste(&state);

    tracks = engine_get_tracks(state.engine);
    expect(tracks[destination_track].clip_count == 1, "inherited paste should create destination MIDI region");
    const EngineClip* pasted = &tracks[destination_track].clips[0];
    expect(engine_clip_midi_inherits_track_instrument(pasted),
           "pasted inherited MIDI region should preserve inheritance flag");
    expect(engine_clip_midi_effective_instrument_preset(state.engine, destination_track, 0) ==
               ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS,
           "pasted inherited MIDI region should use destination track default");

    state_destroy(&state);
}

static void test_midi_clipboard_copy_ignores_stale_audio_selection(void) {
    const char* audio_fixture_path = "tmp/timeline_midi_region_stale_selection.wav";
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    daw_test_wav_write_silence_or_fail(audio_fixture_path,
                                       cfg.sample_rate,
                                       256,
                                       "timeline_midi_region_test");

    int destination_track = engine_add_track(state.engine);
    expect(destination_track == 1, "destination track should be created for stale selection copy test");

    int audio_index = -1;
    expect(engine_add_clip_to_track(state.engine,
                                    0,
                                    audio_fixture_path,
                                    0,
                                    &audio_index),
           "failed to add audio source clip for stale selection copy test");
    timeline_selection_set_single(&state, 0, audio_index);
    timeline_clipboard_copy(&state);

    int midi_index = -1;
    uint64_t midi_start = (uint64_t)cfg.sample_rate;
    uint64_t midi_duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, midi_start, midi_duration, &midi_index),
           "failed to add MIDI source clip for stale selection copy test");
    EngineMidiNote note = {.start_frame = 1200, .duration_frames = 2400, .note = 72, .velocity = 0.66f};
    expect(engine_clip_midi_add_note(state.engine, 0, midi_index, note, NULL),
           "failed to add MIDI note for stale selection copy test");

    state.selected_track_index = 0;
    state.selected_clip_index = midi_index;
    timeline_clipboard_copy(&state);

    uint64_t paste_start = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_transport_seek(state.engine, paste_start), "failed to seek stale selection paste playhead");
    timeline_selection_set_single(&state, destination_track, -1);
    timeline_clipboard_paste(&state);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(tracks != NULL, "tracks missing after stale selection paste");
    expect(tracks[destination_track].clip_count == 1,
           "stale audio selection copy should paste one destination clip");
    const EngineClip* pasted = &tracks[destination_track].clips[0];
    expect(engine_clip_get_kind(pasted) == ENGINE_CLIP_KIND_MIDI,
           "copying selected MIDI region should replace prior audio clipboard");
    expect(pasted->timeline_start_frames == paste_start, "stale selection MIDI paste start mismatch");
    expect(engine_clip_midi_note_count(pasted) == 1, "stale selection MIDI paste note count mismatch");
    const EngineMidiNote* pasted_notes = engine_clip_midi_notes(pasted);
    expect_note(&pasted_notes[0], note.start_frame, note.duration_frames, note.note,
                "stale selection MIDI paste note mismatch");

    state_destroy(&state);
    (void)unlink(audio_fixture_path);
}

// Measures rendered output so retained zero gain is checked beyond model fields.
static float rendered_peak(Engine* engine, uint64_t start, uint64_t frames) {
    EngineBounceBuffer bounce = {0};
    expect(engine_bounce_range_to_buffer(engine, start, start + frames, NULL, NULL, &bounce), "gain fixture bounce");
    float peak = 0;
    for (uint64_t i = 0; i < bounce.frame_count * (uint64_t)bounce.channels; ++i) {
        float sample = fabsf(bounce.data[i]);
        if (sample > peak) peak = sample;
    }
    engine_bounce_buffer_free(&bounce);
    // Also exercise the active revision used by playback, independently of the bounce implementation.
    engine_graph_reset(engine_render_source_graph(engine));
    float output[256], scratch[256];
    for (uint64_t offset = 0; offset < frames; offset += 128) {
        int count = frames - offset < 128 ? (int)(frames - offset) : 128;
        engine_mix_tracks(engine, start + offset, count, output, scratch, 2);
        for (int i = 0; i < count * 2; ++i) {
            float sample = fabsf(output[i]);
            if (sample > peak) peak = sample;
        }
    }
    return peak;
}

// Exercises zero-gain audio and MIDI regions through real copy/paste and undo/redo commands.
static void test_zero_gain_clipboard_and_undo(void) {
    const char* path = "tmp/timeline_zero_gain.wav";
    float samples[4096];
    for (int i = 0; i < 4096; ++i) samples[i] = 0.25f;
    expect(wav_write_pcm16_dithered(path, samples, 4096, 1, 48000, 7), "gain audio fixture");
    for (int midi = 0; midi < 2; ++midi) {
        AppState state;
        EngineRuntimeConfig cfg;
        state_init(&state, &cfg);
        int clip = -1;
        if (midi) {
            expect(engine_add_midi_clip_to_track(state.engine, 0, 0, 4096, &clip), "gain MIDI fixture");
            EngineMidiNote note = {.duration_frames = 4096, .note = 60, .velocity = 0.7f};
            expect(engine_clip_midi_add_note(state.engine, 0, clip, note, NULL), "gain MIDI note");
        } else {
            expect(engine_add_clip_to_track(state.engine, 0, path, 0, &clip), "gain audio import");
        }
        expect(rendered_peak(state.engine, 0, 4096) > 0.01f, "fixture was silent before gain edit");
        expect(engine_clip_set_gain(state.engine, 0, clip, 0), "zero clip gain");
        int destination = engine_add_track(state.engine);
        expect(destination == 1, "gain destination track");
        timeline_selection_set_single(&state, 0, clip);
        timeline_clipboard_copy(&state);
        expect(engine_transport_seek(state.engine, 8192), "gain paste seek");
        timeline_selection_set_single(&state, destination, -1);
        timeline_clipboard_paste(&state);
        const EngineTrack* tracks = engine_get_tracks(state.engine);
        expect(tracks[1].clip_count == 1 && tracks[1].clips[0].gain == 0, "paste revived muted clip");
        expect(rendered_peak(state.engine, 8192, 4096) == 0, "pasted zero gain not silent");
        expect(undo_manager_undo(&state.undo, &state), "zero paste undo");
        expect(engine_get_tracks(state.engine)[1].clip_count == 0, "zero paste undo removal");
        expect(undo_manager_redo(&state.undo, &state), "zero paste redo");
        expect(engine_get_tracks(state.engine)[1].clips[0].gain == 0, "redo revived muted clip");
        expect(rendered_peak(state.engine, 8192, 4096) == 0, "redo zero gain not silent");
        // Restore an audible clip and isolate the track snapshot's zero-gain semantics.
        expect(engine_clip_set_gain(state.engine, 1, 0, 1), "restore audible clip");
        expect(rendered_peak(state.engine, 8192, 4096) > 0.01f, "restored fixture silent");
        UndoCommand command = {.type = UNDO_CMD_TRACK_SNAPSHOT};
        command.data.track_snapshot_edit.track_index = 1;
        command.data.track_snapshot_edit.gain_before = 1;
        command.data.track_snapshot_edit.gain_after = 0;
        expect(engine_track_set_gain(state.engine, 1, 0), "zero track gain");
        undo_manager_push(&state.undo, &command);
        expect(undo_manager_undo(&state.undo, &state), "track gain undo");
        expect(rendered_peak(state.engine, 8192, 4096) > 0.01f, "track gain undo remained silent");
        expect(undo_manager_redo(&state.undo, &state), "track gain redo");
        expect(engine_get_tracks(state.engine)[1].gain == 0, "track redo revived zero gain");
        expect(rendered_peak(state.engine, 8192, 4096) == 0, "track gain redo not silent");
        state_destroy(&state);
    }
    unlink(path);
}

// Keeps a rejected FX edit on its original history stack and permits a corrected retry.
static void test_rejected_undo_redo_preserves_history(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);
    FxInstId id = engine_fx_master_add(state.engine, 1);
    expect(id != 0, "undo rejection fixture");
    UndoCommand command = {.type = UNDO_CMD_FX_EDIT};
    command.data.fx_edit.kind = UNDO_FX_EDIT_PARAM;
    command.data.fx_edit.id = id;
    command.data.fx_edit.before_state.param_count = 1;
    command.data.fx_edit.after_state.param_count = 1;
    command.data.fx_edit.before_state.params[0] = NAN;
    command.data.fx_edit.after_state.params[0] = -12;
    expect(undo_manager_push(&state.undo, &command), "push rejected command");
    expect(!undo_manager_undo(&state.undo, &state), "invalid undo accepted");
    expect(state.undo.undo_count == 1 && state.undo.redo_count == 0, "rejected undo consumed history");
    state.undo.undo_stack[0].data.fx_edit.before_state.params[0] = -3;
    expect(undo_manager_undo(&state.undo, &state), "undo retry");
    expect(state.undo.undo_count == 0 && state.undo.redo_count == 1, "undo retry stack movement");
    state.undo.redo_stack[0].data.fx_edit.after_state.params[0] = NAN;
    expect(!undo_manager_redo(&state.undo, &state), "invalid redo accepted");
    expect(state.undo.undo_count == 0 && state.undo.redo_count == 1, "rejected redo consumed history");
    state.undo.redo_stack[0].data.fx_edit.after_state.params[0] = -12;
    expect(undo_manager_redo(&state.undo, &state), "redo retry");
    FxMasterSnapshot snapshot;
    expect(engine_fx_master_snapshot(state.engine, &snapshot) && snapshot.items[0].params[0] == -12, "redo retry value");
    state_destroy(&state);
}

// Keeps track history and visible controls unchanged when a complete restore is rejected.
static void test_rejected_track_snapshot_history(void) {
    AppState state;
    EngineRuntimeConfig config;
    state_init(&state, &config);
    UndoCommand command = {.type = UNDO_CMD_TRACK_SNAPSHOT};
    command.data.track_snapshot_edit.track_index = 0;
    command.data.track_snapshot_edit.gain_before = NAN;
    command.data.track_snapshot_edit.pan_before = 0.75f;
    command.data.track_snapshot_edit.muted_before = true;
    command.data.track_snapshot_edit.gain_after = 1;
    state.effects_panel.track_snapshot.gain = 0.33f;
    expect(undo_manager_push(&state.undo, &command), "track snapshot history push");
    expect(!undo_manager_undo(&state.undo, &state), "invalid track snapshot undo accepted");
    const EngineTrack* track = engine_get_tracks(state.engine);
    expect(track[0].gain == 1 && track[0].pan == 0 && !track[0].muted,
           "rejected track snapshot changed other settings");
    expect(state.effects_panel.track_snapshot.gain == 0.33f && state.undo.undo_count == 1 && state.undo.redo_count == 0,
           "rejected track snapshot changed UI or consumed history");
    state.undo.undo_stack[0].data.track_snapshot_edit.gain_before = 0.25f;
    expect(undo_manager_undo(&state.undo, &state), "track snapshot retry");
    expect(track[0].gain == 0.25f && track[0].pan == 0.75f && track[0].muted &&
           state.effects_panel.track_snapshot.gain == 0.25f, "accepted track snapshot incomplete");
    expect(undo_manager_redo(&state.undo, &state), "track snapshot redo");
    expect(track[0].gain == 1 && track[0].pan == 0 && !track[0].muted, "track snapshot redo incomplete");
    state_destroy(&state);
}

// Keeps transform undo attached to clip identity after source replacement and timeline reordering.
static void test_audio_transform_identity_and_sorting(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);
    char path[] = "tmp/undo-transform-XXXXXX";
    int fd = mkstemp(path);
    expect(fd >= 0, "transform fixture path");
    close(fd);
    float samples[1024];
    for (int i = 0; i < 1024; ++i) samples[i] = 0.25f;
    expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "transform fixture audio");
    int index;
    expect(engine_add_clip_to_track(state.engine, 0, path, 2048, NULL), "transform neighbor");
    expect(engine_add_clip_to_track(state.engine, 0, path, 4096, &index), "transform target");
    expect(engine_clip_set_gain(state.engine, 0, index, 0.75f), "transform initial gain");
    UndoCommand command = {.type = UNDO_CMD_CLIP_TRANSFORM};
    expect(undo_clip_state_from_engine_clip(&engine_get_tracks(state.engine)[0].clips[index], 0,
           &command.data.clip_transform.after), "transform after snapshot");
    expect(undo_clip_state_clone(&command.data.clip_transform.before, &command.data.clip_transform.after),
           "transform before snapshot");
    command.data.clip_transform.before.start_frame = 0;
    command.data.clip_transform.before.gain = 0;
    uint64_t identity = command.data.clip_transform.after.creation_index;
    expect(undo_manager_push(&state.undo, &command), "transform history push");
    undo_clip_state_clear(&command.data.clip_transform.before);
    undo_clip_state_clear(&command.data.clip_transform.after);
    state.undo.undo_stack[0].data.clip_transform.before.gain = NAN;
    expect(!undo_manager_undo(&state.undo, &state), "invalid complete transform accepted");
    expect(state.undo.undo_count == 1 && state.undo.redo_count == 0 &&
           engine_get_tracks(state.engine)[0].clips[index].timeline_start_frames == 4096 &&
           engine_get_tracks(state.engine)[0].clips[index].gain == 0.75f,
           "rejected complete transform changed placement or consumed history");
    state.undo.undo_stack[0].data.clip_transform.before.gain = 0;
    expect(engine_clip_add_automation_point(state.engine, 0, index, ENGINE_AUTOMATION_TARGET_VOLUME, 0, 0, NULL),
           "transform source replacement");
    expect(undo_manager_undo(&state.undo, &state), "transform undo after sampler replacement");
    const EngineClip* clips = engine_get_tracks(state.engine)[0].clips;
    expect(clips[0].creation_index == identity && clips[0].timeline_start_frames == 0 && clips[0].gain == 0,
           "transform undo targeted wrong sorted clip");
    expect(clips[1].timeline_start_frames == 2048 && clips[1].gain == 1, "transform undo changed neighbor");
    expect(rendered_peak(state.engine, 0, 1024) == 0, "transform undo zero gain not rendered");
    expect(undo_manager_redo(&state.undo, &state), "transform redo");
    clips = engine_get_tracks(state.engine)[0].clips;
    expect(clips[1].creation_index == identity && clips[1].timeline_start_frames == 4096 && clips[1].gain == 0.75f,
           "transform redo targeted wrong sorted clip");
    expect(clips[0].timeline_start_frames == 2048 && clips[0].gain == 1, "transform redo changed neighbor");
    expect(rendered_peak(state.engine, 4096, 1024) > 0.01f, "transform redo remained silent");
    state_destroy(&state);
    unlink(path);
}

// Exercises the shared live preview with mixed history ownership and rejected later targets.
static void test_compound_preview_and_history_transfer(void) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    expect(engine_add_midi_clip_to_track(state.engine, 0, 100, 4096, NULL), "preview first clip");
    expect(engine_add_midi_clip_to_track(state.engine, 0, 9000, 4096, NULL), "preview second clip");
    UndoClipState before[2] = {0}, after[2] = {0};
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    for (int i = 0; i < 2; ++i) {
        expect(undo_clip_state_capture(state.engine, &tracks[0].clips[i], 0, &before[i]), "preview before");
        expect(undo_clip_state_clone(&after[i], &before[i]), "preview after");
        timeline_selection_add(&state, 0, i);
    }
    UndoCommand cmd = {.type = UNDO_CMD_MULTI_CLIP_TRANSFORM};
    cmd.data.multi_clip_transform = (UndoMultiClipTransform){.count = 2, .before = before, .after = after};
    expect(undo_manager_begin_drag(&state.undo, &cmd), "preview history reservation");
    for (int i = 0; i < 2; ++i) { undo_clip_state_clear(&before[i]); undo_clip_state_clear(&after[i]); }
    state.timeline_drag.track_index = 0; state.timeline_drag.clip_index = 0;
    TimelineSelectionEntry selection[2]; memcpy(selection, state.selection, sizeof(selection));
    state.undo.active_drag.data.multi_clip_transform.before[1].gain = NAN;
    expect(!timeline_apply_compound_preview(&state, 500, false), "invalid later preview accepted");
    expect(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 100 &&
           !memcmp(selection, state.selection, sizeof(selection)), "rejected preview changed clip or selection");
    state.undo.active_drag.data.multi_clip_transform.before[1].gain = 1;
    expect(timeline_apply_compound_preview(&state, 500, false), "compound live preview");
    expect(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 600 &&
           engine_get_tracks(state.engine)[0].clips[1].timeline_start_frames == 9500, "preview readback");
    expect(timeline_apply_compound_preview(&state, 700, false), "absolute preview retry");
    expect(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 800, "preview accumulated delta twice");
    expect(!timeline_apply_compound_drop(&state, 700, INT_MAX), "invalid compound destination accepted");
    expect(engine_get_track_count(state.engine) == 1 && engine_get_tracks(state.engine)[0].clip_count == 2,
           "rejected drop changed project");
    expect(timeline_apply_compound_drop(&state, 700, 2), "MIDI cross-track compound drop");
    expect(engine_get_track_count(state.engine) == 3 && engine_get_tracks(state.engine)[0].clip_count == 0 &&
           engine_get_tracks(state.engine)[2].clip_count == 2 && state.selection[0].track_index == 2 &&
           state.selection[1].track_index == 2, "MIDI drop did not move full selection");
    UndoClipState* captured = state.undo.active_drag.data.multi_clip_transform.before;
    expect(undo_manager_commit_drag(&state.undo, &state.undo.active_drag), "preview transfer");
    expect(!state.undo.active_drag_valid && state.undo.undo_stack[0].data.multi_clip_transform.before == captured,
           "release cloned instead of transferring prepared history");
    expect(engine_track_set_gain(state.engine, 2, 0.5f), "generated track edit");
    expect(!undo_manager_undo(&state.undo, &state) && state.undo.undo_count == 1 &&
           engine_get_track_count(state.engine) == 3, "undo discarded generated-track setting");
    expect(engine_track_set_gain(state.engine, 2, 1), "restore generated track setting");
    int extra = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 1, 0, 4096, &extra), "unrelated generated-track clip");
    expect(!undo_manager_undo(&state.undo, &state) && engine_get_tracks(state.engine)[2].clip_count == 2,
           "undo discarded unrelated generated-track content");
    expect(engine_remove_clip(state.engine, 1, extra), "remove unrelated fixture clip");
    expect(engine_track_midi_set_instrument_enabled(state.engine, 1, false), "restore generated gap instrument setting");
    FxInstId fx = engine_fx_track_add(state.engine, 1, 1);
    expect(fx != 0 && !undo_manager_undo(&state.undo, &state), "undo discarded generated-track FX");
    expect(engine_fx_track_remove(state.engine, 1, fx), "remove fixture FX");
    expect(undo_manager_undo(&state.undo, &state), "preview undo");
    expect(engine_get_track_count(state.engine) == 1 && state.selection_count == 2 &&
           state.selection[0].track_index == 0 && state.selection[1].track_index == 0, "undo retained generated tracks or stale selection");
    expect(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 100 &&
           engine_get_tracks(state.engine)[0].clips[1].timeline_start_frames == 9000, "preview undo complete state");
    expect(undo_manager_redo(&state.undo, &state), "preview redo");
    expect(engine_get_track_count(state.engine) == 3 && engine_get_tracks(state.engine)[2].clip_count == 2,
           "redo did not restore generated tracks");
    expect(undo_manager_undo(&state.undo, &state) && engine_get_track_count(state.engine) == 1,
           "second undo used stale generated-track identities");
    expect(undo_manager_redo(&state.undo, &state), "second generated-track redo");
    state_destroy(&state);
}

// Checks atomic source-offset preview for two audio clips, including a rejected later region.
static void test_compound_audio_slip(void) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    char path[] = "tmp/compound-slip-XXXXXX";
    int fd = mkstemp(path); expect(fd >= 0, "slip path"); close(fd);
    float samples[1024] = {0};
    expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "slip audio");
    UndoClipState before[2] = {0}, after[2] = {0};
    for (int i = 0; i < 2; ++i) {
        expect(engine_add_clip_to_track(state.engine, i, path, (uint64_t)i * 2000, NULL), "slip clip");
        expect(engine_clip_set_region(state.engine, i, 0, (uint64_t)i * 32, 512), "slip region");
        expect(undo_clip_state_capture(state.engine, &engine_get_tracks(state.engine)[i].clips[0], i, &before[i]), "slip before");
        expect(undo_clip_state_clone(&after[i], &before[i]), "slip after");
        timeline_selection_add(&state, i, 0);
    }
    UndoCommand cmd = {.type = UNDO_CMD_MULTI_CLIP_TRANSFORM};
    cmd.data.multi_clip_transform = (UndoMultiClipTransform){.count = 2, .before = before, .after = after};
    expect(undo_manager_begin_drag(&state.undo, &cmd), "slip begin");
    for (int i = 0; i < 2; ++i) { undo_clip_state_clear(&before[i]); undo_clip_state_clear(&after[i]); }
    state.timeline_drag.track_index = 0; state.timeline_drag.clip_index = 0;
    state.undo.active_drag.data.multi_clip_transform.before[1].duration_frames = 2048;
    expect(!timeline_apply_compound_preview(&state, 100, true), "invalid slip accepted");
    expect(engine_get_tracks(state.engine)[0].clips[0].offset_frames == 0, "partial slip");
    state.undo.active_drag.data.multi_clip_transform.before[1].duration_frames = 512;
    expect(timeline_apply_compound_preview(&state, 100, true), "slip preview");
    expect(engine_get_tracks(state.engine)[0].clips[0].offset_frames == 100 &&
           engine_get_tracks(state.engine)[1].clips[0].offset_frames == 132, "slip readback");
    expect(undo_manager_commit_drag(&state.undo, &state.undo.active_drag), "slip commit");
    expect(undo_manager_undo(&state.undo, &state), "slip undo");
    expect(engine_get_tracks(state.engine)[0].clips[0].offset_frames == 0 &&
           engine_get_tracks(state.engine)[1].clips[0].offset_frames == 32, "slip restoration");
    state_destroy(&state); unlink(path);
}

// Restores both moved selections and split neighbors through the actual compound-drop history path.
static void test_compound_overlap_history(bool multi, bool growth) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    int neighbor_track = growth ? 1 : 0;
    int midi_index = growth ? 1 : 0;
    char path[] = "tmp/compound-overlap-XXXXXX"; int fd = mkstemp(path); expect(fd >= 0, "overlap history path"); close(fd);
    float samples[1024] = {0}; expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "overlap history media");
    expect(engine_add_clip_to_track(state.engine, neighbor_track, path, 0, NULL) && engine_clip_set_region(state.engine, neighbor_track, 0, 0, 512), "history neighbor");
    int anchor; expect(engine_add_clip_to_track(state.engine, 0, path, 600, &anchor) &&
        engine_clip_set_region(state.engine, 0, anchor, 0, 128), "history anchor");
    expect(engine_add_midi_clip_to_track(state.engine, 1, 1000, 4096, NULL), "history MIDI selection");
    UndoClipState before[2] = {0}, after[2] = {0};
    expect(undo_clip_state_capture(state.engine, &engine_get_tracks(state.engine)[0].clips[anchor], 0, &before[0]), "history audio capture");
    expect(undo_clip_state_capture(state.engine, &engine_get_tracks(state.engine)[1].clips[midi_index], 1, &before[1]), "history MIDI capture");
    for (int i = 0; i < 2; ++i) expect(undo_clip_state_clone(&after[i], &before[i]), "history state clone");
    UndoCommand cmd = {.type = multi ? UNDO_CMD_MULTI_CLIP_TRANSFORM : UNDO_CMD_CLIP_TRANSFORM};
    if (multi) cmd.data.multi_clip_transform = (UndoMultiClipTransform){.count = 2, .before = before, .after = after};
    else { cmd.data.clip_transform.before = before[0]; cmd.data.clip_transform.after = after[0]; }
    expect(undo_manager_begin_drag(&state.undo, &cmd), "overlap begin");
    for (int i = 0; i < 2; ++i) { undo_clip_state_clear(&before[i]); undo_clip_state_clear(&after[i]); }
    timeline_selection_add(&state, 0, anchor); timeline_selection_add(&state, 1, midi_index);
    state.timeline_drag.track_index = 0; state.timeline_drag.clip_index = anchor;
    if (multi) expect(timeline_apply_compound_preview(&state, -472, false), "overlap preview");
    else expect(engine_clip_set_timeline_start(state.engine, 0, anchor, 128, &state.timeline_drag.clip_index), "single overlap preview");
    expect(timeline_apply_compound_drop(&state, -472, growth ? 1 : 0), "overlap complete drop");
    expect(state.undo.active_drag.clip_content_before && state.undo.active_drag.clip_content_after &&
        engine_get_tracks(state.engine)[neighbor_track].clip_count == 3, "drop did not retain overlap history");
    expect(undo_manager_commit_drag(&state.undo, &state.undo.active_drag), "overlap history commit");
    expect(unlink(path) == 0, "remove history source");
    expect(undo_manager_undo(&state.undo, &state), "overlap undo");
    expect(engine_get_track_count(state.engine) == 2, "undo retained generated tracks");
    const EngineTrack* restored = engine_get_tracks(state.engine);
    expect(restored[neighbor_track].clips[0].duration_frames == 512 &&
        restored[0].clips[growth ? 0 : 1].timeline_start_frames == 600 &&
        restored[1].clips[midi_index].timeline_start_frames == 1000, "undo lost initial gesture or neighbor content");
    for (int cycle = 0; cycle < 3; ++cycle) {
        expect(undo_manager_redo(&state.undo, &state) && engine_get_tracks(state.engine)[neighbor_track].clip_count == 3,
            "overlap redo");
        expect(engine_get_track_count(state.engine) == (growth ? 3 : 2), "redo generated topology");
        expect(undo_manager_undo(&state.undo, &state), "repeat complete overlap undo");
    }
    state_destroy(&state);
}

// Uses the real press handler to verify modifier ownership and complete mixed-media ripple history.
static void test_ripple_gesture_capture(void) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    char path[] = "tmp/ripple-gesture-XXXXXX"; int fd = mkstemp(path); expect(fd >= 0, "ripple path"); close(fd);
    float samples[1024] = {0}; expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "ripple media");
    expect(engine_add_clip_to_track(state.engine, 0, path, 100, NULL) &&
        engine_clip_set_region(state.engine, 0, 0, 0, 512), "ripple audio anchor");
    expect(engine_add_midi_clip_to_track(state.engine, 0, 1000, 128, NULL), "ripple MIDI follower");
    TimelineGeometry geom = {.content_width = 800, .visible_seconds = 8, .pixels_per_second = 100};
    InputManager manager = {.last_click_clip = -1, .last_click_track = -1};
    expect(timeline_input_mouse_handle_clip_press(&manager, &state, &geom, cfg.sample_rate, 0, 0,
        false, true, false, false, true), "right trim press");
    expect(state.timeline_drag.mode == TIMELINE_DRAG_MODE_TRIM_RIGHT && state.timeline_drag.trimming_right,
        "ordinary edge no longer selects trim");
    timeline_input_mouse_drag_end(&state); manager.last_click_clip = -1;
    expect(timeline_input_mouse_handle_clip_press(&manager, &state, &geom, cfg.sample_rate, 0, 0,
        false, true, false, true, true), "fade press");
    expect(state.timeline_drag.mode == TIMELINE_DRAG_MODE_FADE_OUT && !state.timeline_drag.trimming_right,
        "Alt audio edge no longer selects fade");
    timeline_input_mouse_drag_end(&state); manager.last_click_clip = -1;
    expect(timeline_input_mouse_handle_clip_press(&manager, &state, &geom, cfg.sample_rate, 0, 0,
        false, false, false, true, true), "ripple body press");
    expect(state.timeline_drag.mode == TIMELINE_DRAG_MODE_RIPPLE && state.timeline_drag.ripple_target_count == 1 &&
        state.undo.active_drag.data.multi_clip_transform.count == 2, "MIDI follower omitted from ripple history");
    expect(timeline_apply_compound_preview(&state, 200, false), "mixed ripple preview");
    expect(engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 300 &&
        engine_get_tracks(state.engine)[0].clips[1].timeline_start_frames == 1200, "mixed ripple moved only audio");
    state.undo.active_drag.data.multi_clip_transform.before[1].gain = NAN;
    expect(!timeline_apply_compound_preview(&state, 400, false) &&
        engine_get_tracks(state.engine)[0].clips[0].timeline_start_frames == 300, "partial rejected ripple preview");
    state.undo.active_drag.data.multi_clip_transform.before[1].gain = 1;
    expect(undo_manager_commit_drag(&state.undo, &state.undo.active_drag), "ripple commit");
    expect(undo_manager_undo(&state.undo, &state) && engine_get_tracks(state.engine)[0].clips[1].timeline_start_frames == 1000,
        "ripple MIDI undo");
    expect(undo_manager_redo(&state.undo, &state) && engine_get_tracks(state.engine)[0].clips[1].timeline_start_frames == 1200,
        "ripple MIDI redo");
    timeline_input_mouse_drag_end(&state); state_destroy(&state); unlink(path);
}

// Exercises the real duplicate shortcut and selection delete as one history entry each across media types.
static void test_selection_duplicate_delete_history(void) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    char path[] = "tmp/selection-history-XXXXXX"; int fd = mkstemp(path); expect(fd >= 0, "selection history path"); close(fd);
    float samples[1024] = {0}; expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "selection history media");
    expect(engine_add_clip_to_track(state.engine, 0, path, 100, NULL) &&
        engine_add_midi_clip_to_track(state.engine, 1, 200, 256, NULL), "selection history fixture");
    uint64_t original[2] = {engine_get_tracks(state.engine)[0].clips[0].creation_index,
        engine_get_tracks(state.engine)[1].clips[0].creation_index};
    timeline_selection_add(&state, 0, 0); timeline_selection_add(&state, 1, 0);
    timeline_selection_set_primary(&state, 0, 0);
    InputManager manager = {0}; SDL_Event event = {0};
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_d; event.key.keysym.mod = KMOD_CTRL;
    expect(timeline_input_keyboard_handle_event(&manager, &state, &event), "duplicate shortcut");
    expect(state.undo.undo_count == 1 && state.selection_count == 2 &&
        engine_get_tracks(state.engine)[0].clip_count == 2 && engine_get_tracks(state.engine)[1].clip_count == 2,
        "duplicate partial selection or multiple history entries");
    uint64_t copies[2] = {engine_get_tracks(state.engine)[0].clips[1].creation_index,
        engine_get_tracks(state.engine)[1].clips[1].creation_index};
    expect(unlink(path) == 0, "unlink selection history source");
    for (int cycle = 0; cycle < 3; ++cycle) {
        expect(undo_manager_undo(&state.undo, &state) && state.selection_count == 2 &&
            engine_get_tracks(state.engine)[0].clip_count == 1 &&
            engine_get_tracks(state.engine)[state.selection[0].track_index].clips[state.selection[0].clip_index].creation_index == original[0],
            "duplicate undo content or original selection");
        expect(undo_manager_redo(&state.undo, &state) && state.selection_count == 2 &&
            engine_get_tracks(state.engine)[state.selection[0].track_index].clips[state.selection[0].clip_index].creation_index == copies[0],
            "duplicate redo selection");
    }
    timeline_selection_delete(&state);
    expect(state.undo.undo_count == 2 && state.selection_count == 0 && engine_get_tracks(state.engine)[0].clip_count == 1 &&
        engine_get_tracks(state.engine)[1].clip_count == 1, "delete not a complete single-history action");
    expect(undo_manager_undo(&state.undo, &state) && state.selection_count == 2 &&
        engine_get_tracks(state.engine)[state.selection[1].track_index].clips[state.selection[1].clip_index].creation_index == copies[1],
        "delete undo failed to restore deleted MIDI selection");
    expect(undo_manager_redo(&state.undo, &state) && state.selection_count == 0, "delete redo selection");
    state_destroy(&state);
}

// Exercises mixed clipboard paste through the key event with one history entry and original selection recovery.
static void test_mixed_clipboard_transaction(void) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    char path[] = "tmp/mixed-paste-XXXXXX"; int fd = mkstemp(path); expect(fd >= 0, "mixed paste path"); close(fd);
    float samples[1024] = {0}; expect(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "mixed paste media");
    expect(engine_add_clip_to_track(state.engine, 0, path, 100, NULL) &&
        engine_add_midi_clip_to_track(state.engine, 1, 600, 256, NULL), "mixed paste fixture");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 100, .note = 65, .velocity = 0.6f};
    expect(engine_clip_midi_add_note(state.engine, 1, 0, note, NULL), "mixed paste note");
    timeline_selection_add(&state, 0, 0); timeline_selection_add(&state, 1, 0);
    timeline_selection_set_primary(&state, 0, 0);
    InputManager manager = {0}; SDL_Event event = {0};
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_c; event.key.keysym.mod = KMOD_GUI;
    expect(timeline_input_keyboard_handle_event(&manager, &state, &event), "mixed copy event");
    expect(engine_transport_seek(state.engine, 2048), "mixed paste seek");
    event.key.keysym.sym = SDLK_v;
    expect(timeline_input_keyboard_handle_event(&manager, &state, &event), "mixed paste event");
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    expect(state.undo.undo_count == 1 && state.selection_count == 2 && tracks[0].clip_count == 3 &&
        tracks[0].clips[1].timeline_start_frames == 2048 && tracks[0].clips[2].timeline_start_frames == 2548 &&
        tracks[0].clips[2].kind == ENGINE_CLIP_KIND_MIDI && tracks[0].clips[2].midi_notes.note_count == 1,
        "mixed paste placement metadata or history");
    expect(unlink(path) == 0, "unlink mixed paste source");
    for (int cycle = 0; cycle < 3; ++cycle) {
        expect(undo_manager_undo(&state.undo, &state) && engine_get_tracks(state.engine)[0].clip_count == 1 &&
            state.selection_count == 2 && state.selection[0].track_index == 0 && state.selection[1].track_index == 1,
            "mixed paste undo original selection");
        expect(undo_manager_redo(&state.undo, &state) && state.selection_count == 2 && state.selection[1].track_index == 0 &&
            engine_get_tracks(state.engine)[0].clip_count == 3, "mixed paste redo retained content");
    }
    state_destroy(&state);
}

// Checks selected neighbors and primary focus against actual identities through both reorder directions.
static void test_selection_after_single_clip_sort(bool trim) {
    AppState state; EngineRuntimeConfig cfg; state_init(&state, &cfg);
    expect(engine_add_midi_clip_to_track(state.engine, 0, 100, 1000, NULL) &&
           engine_add_midi_clip_to_track(state.engine, 0, 200, 1000, NULL) &&
           engine_add_midi_clip_to_track(state.engine, 0, 400, 1000, NULL) &&
           engine_add_track(state.engine) &&
           engine_add_midi_clip_to_track(state.engine, 1, 0, 1000, NULL), "selection reorder fixture");
    uint64_t identities[4];
    for (int i = 0; i < 3; ++i) {
        identities[i] = engine_get_tracks(state.engine)[0].clips[i].creation_index;
        timeline_selection_add(&state, 0, i);
    }
    identities[3] = engine_get_tracks(state.engine)[1].clips[0].creation_index;
    timeline_selection_add(&state, 1, 0);
    state.selected_track_index = 0; state.selected_clip_index = 1;
    int index = 0;
    const uint64_t starts[] = {500, 50, 50, 250};
    for (int step = 0; step < 4; ++step) {
        int old_index = index;
        expect(trim ? timeline_midi_left_trim_apply(state.engine, 0, &index, starts[step])
                    : engine_clip_set_timeline_start(state.engine, 0, index, starts[step], &index),
               "selection reorder operation");
        timeline_selection_update_index(&state, 0, old_index, index);
        const EngineTrack* tracks = engine_get_tracks(state.engine);
        expect(state.selection_count == 4, "reorder changed selection count");
        for (int i = 0; i < 4; ++i) {
            TimelineSelectionEntry entry = state.selection[i];
            expect(tracks[entry.track_index].clips[entry.clip_index].creation_index == identities[i],
                   "reorder changed selected identity");
        }
        expect(tracks[state.selected_track_index].clips[state.selected_clip_index].creation_index == identities[1],
               "reorder changed neighboring primary identity");
    }
    state_destroy(&state);
}

int main(void) {
    test_selection_after_single_clip_sort(false);
    test_selection_after_single_clip_sort(true);
    test_mixed_clipboard_transaction();
    test_selection_duplicate_delete_history();
    test_ripple_gesture_capture();
    test_compound_overlap_history(false, false);
    test_compound_overlap_history(true, false);
    test_compound_overlap_history(true, true);
    test_compound_audio_slip();
    test_compound_preview_and_history_transfer();
    test_zero_gain_clipboard_and_undo();
    test_audio_transform_identity_and_sorting();
    test_rejected_track_snapshot_history();
    test_rejected_undo_redo_preserves_history();
    test_create_selects_bar_length_midi_region();
    test_create_undo_redo_rebuilds_midi_region();
    test_midi_region_resize_bounds_follow_note_content();
    test_midi_preview_x_position_stays_fixed_when_region_extends();
    test_midi_left_trim_later_removes_and_clips_notes();
    test_midi_left_trim_earlier_preserves_absolute_note_positions();
    test_midi_left_trim_undo_redo_restores_note_contents();
    test_midi_left_trim_same_drag_restores_covered_notes();
    test_midi_clipboard_pastes_region_to_selected_track();
    test_midi_track_default_inheritance_and_region_override();
    test_midi_clipboard_copy_ignores_stale_audio_selection();
    printf("timeline_midi_region_test: ok\n");
    return 0;
}
