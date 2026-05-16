#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "input/timeline/timeline_clipboard.h"
#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_midi_region.h"
#include "input/timeline/timeline_midi_trim.h"
#include "input/timeline_selection.h"
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
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int destination_track = engine_add_track(state.engine);
    expect(destination_track == 1, "destination track should be created for stale selection copy test");

    int audio_index = -1;
    expect(engine_add_clip_to_track(state.engine,
                                    0,
                                    "assets/audio/kamhunt-timbo-drumline-loop-103bpm-171091.mp3",
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
}

int main(void) {
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
