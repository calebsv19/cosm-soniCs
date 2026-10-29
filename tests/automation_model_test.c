#include "test_midi_editor_harness.h"
#include "session_automation.h"
#include "engine/automation.h"
#include "input/automation_input.h"
#include "daw/automation_actions.h"
#include "input/automation_keyboard.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_click.h"
#include "ui/timeline_view_internal.h"
#include "ui/layout.h"
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#define CHECK(v, m) daw_test_expect("automation_model_test", (v), (m))

// Checks legacy playback, explicit dB/balance, curves, boundaries and rejection without data loss.
static void curves(void) {
    EngineAutomationPoint points[] = {{20, .8f, ENGINE_AUTOMATION_CURVE_LINEAR}, {80, -.4f, ENGINE_AUTOMATION_CURVE_LINEAR}};
    EngineAutomationLane lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME};
    CHECK(engine_automation_lane_set_points(&lane, points, 2), "legacy points");
    CHECK(fabsf(engine_automation_lane_eval(&lane, 10, 100) - .4f) < .00001f, "legacy first baseline");
    CHECK(fabsf(engine_automation_lane_eval(&lane, 90, 100) + .2f) < .00001f, "legacy last baseline");
    CHECK(fabsf(engine_automation_lane_eval(&lane, 50, 100) - .2f) < .00001f, "legacy segment");
    lane.before = lane.after = ENGINE_AUTOMATION_BOUNDARY_HOLD;
    CHECK(engine_automation_lane_eval(&lane, 0, 100) == .8f && engine_automation_lane_eval(&lane, 100, 100) == -.4f, "hold boundaries");
    lane.points[0].curve = ENGINE_AUTOMATION_CURVE_HOLD;
    CHECK(engine_automation_lane_eval(&lane, 79, 100) == .8f && engine_automation_lane_eval(&lane, 80, 100) == -.4f, "sample exact hold transition");
    lane.points[0].curve = ENGINE_AUTOMATION_CURVE_SMOOTH;
    CHECK(fabsf(engine_automation_lane_eval(&lane, 35, 100) - .6125f) < .00001f, "smooth interpolation");
    CHECK(!engine_automation_lane_update_point(&lane, 0, 80, .2f, NULL) && lane.points[0].frame == 20, "collision damaged lane");
    EngineAutomationPoint duplicate[] = {{2, .1f, 0}, {2, .4f, 0}};
    CHECK(!engine_automation_lane_set_points(&lane, duplicate, 2) && lane.points[0].frame == 20, "bulk duplicate damaged lane");
    CHECK(!engine_automation_lane_insert_point(&lane, 30, NAN, NULL), "NaN accepted");
    lane.domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL;
    lane.points[0] = (EngineAutomationPoint){20, -12, 0}; lane.points[1] = (EngineAutomationPoint){80, 0, 0};
    CHECK(engine_automation_lane_validate(&lane, 100), "physical dB validation");
    CHECK(fabsf(engine_automation_lane_eval(&lane, 50, 100) - (powf(10, -6.f / 20) - 1)) < .00001f, "dB interpolation is not in dB");
    lane.points[0].value = -120;
    CHECK(engine_automation_lane_eval(&lane, 0, 100) == -1, "physical silence");
    lane.points[0].value = 25;
    CHECK(!engine_automation_lane_validate(&lane, 100), "gain range accepted");
    engine_automation_lane_free(&lane);
}

// Proves curve metadata survives real engine publication, capture, disk roundtrip, restore and undo.
static void persistence(void) {
    AppState state; EngineRuntimeConfig cfg;
    daw_midi_editor_test_state_init("automation_model_test", &state, &cfg);
    int clip = -1;
    CHECK(engine_add_midi_clip_to_track(state.engine, 0, 0, 96000, &clip), "MIDI region");
    EngineAutomationPoint points[] = {{12000, -9, ENGINE_AUTOMATION_CURVE_SMOOTH}, {48000, 0, ENGINE_AUTOMATION_CURVE_HOLD}};
    EngineAutomationLane lane = {.target=ENGINE_AUTOMATION_TARGET_VOLUME, .points=points, .point_count=2,
        .domain=ENGINE_AUTOMATION_DOMAIN_PHYSICAL, .before=ENGINE_AUTOMATION_BOUNDARY_HOLD, .after=ENGINE_AUTOMATION_BOUNDARY_HOLD};
    CHECK(automation_begin_edit(&state, 0, clip), "history reservation");
    CHECK(daw_automation_replace_lanes(&state, 0, clip, &lane, 1), "physical lane publish");
    CHECK(automation_commit_edit(&state), "history commit");
    CHECK(undo_manager_undo(&state.undo, &state), "metadata undo");
    CHECK(undo_manager_redo(&state.undo, &state), "metadata redo");
    const EngineClip* restored = &engine_get_tracks(state.engine)[0].clips[clip];
    CHECK(restored->automation_lanes[0].domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL &&
          restored->automation_lanes[0].points[0].curve == ENGINE_AUTOMATION_CURVE_SMOOTH, "history dropped metadata");
    SessionDocument doc = {0}, read = {0};
    CHECK(session_document_capture(&state, &doc), "capture");
    char path[128]; snprintf(path,sizeof(path),"/tmp/sonics-automation-model-%ld.json",(long)getpid());
    CHECK(session_document_write_file(&doc, path), "write");
    CHECK(session_document_read_file(path, &read), "read");
    SessionAutomationLane* saved = &read.tracks[0].clips[clip].automation_lanes[0];
    CHECK(saved->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL && saved->before == ENGINE_AUTOMATION_BOUNDARY_HOLD &&
          saved->after == ENGINE_AUTOMATION_BOUNDARY_HOLD && saved->points[0].curve == ENGINE_AUTOMATION_CURVE_SMOOTH, "disk metadata");
    CHECK(session_apply_clip_automation(state.engine, 0, clip, saved, 1), "restore");
    restored = &engine_get_tracks(state.engine)[0].clips[clip];
    CHECK(restored->automation_lanes[0].after == ENGINE_AUTOMATION_BOUNDARY_HOLD, "restore dropped endpoint");
    session_document_free(&doc); session_document_free(&read); unlink(path);
    daw_midi_editor_test_state_destroy(&state);
}

// Checks shared commands, refusal, gesture ownership and stable undo identity after row insertion.
static void commands(void) {
    AppState state; EngineRuntimeConfig cfg;
    daw_midi_editor_test_state_init("automation_model_test", &state, &cfg);
    int clip = -1, point = -1;
    CHECK(engine_add_midi_clip_to_track(state.engine, 0, 0, 96000, &clip), "command region");
    CHECK(daw_automation_add_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_PAN, 100, -.5f, &point), "discrete insertion");
    CHECK(state.undo.undo_count == 1 && !state.undo.active_drag_valid, "discrete history");
    CHECK(automation_begin_edit(&state, 0, clip) && automation_commit_edit(&state), "selection-only gesture");
    CHECK(state.undo.undo_count == 1, "selection created history");
    CHECK(daw_automation_add_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_PAN, 200, .5f, NULL), "second point");
    CHECK(!daw_automation_update_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_PAN, 0, 200, .2f, NULL), "point collision accepted");
    CHECK(state.undo.undo_count == 2 && engine_get_tracks(state.engine)[0].clips[clip].automation_lanes[1].points[0].frame == 100, "collision mutated history/content");
    CHECK(automation_begin_edit(&state, 0, clip), "gesture start");
    uint64_t serial = state.automation_ui.history_serial;
    CHECK(daw_automation_update_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_PAN, 0, 110, -.4f, NULL), "gesture preview");
    CHECK(daw_automation_update_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_PAN, 0, 120, -.3f, NULL), "second preview");
    CHECK(automation_commit_edit(&state) && state.undo.undo_count == 3, "gesture was not one history edit");
    CHECK(engine_insert_track(state.engine, 0), "row insertion");
    CHECK(undo_manager_undo(&state.undo, &state), "stable automation undo");
    CHECK(engine_get_tracks(state.engine)[1].clips[clip].automation_lanes[1].points[0].frame == 100 &&
          engine_get_tracks(state.engine)[0].clip_count == 0, "undo targeted neighbor");
    CHECK(undo_manager_redo(&state.undo, &state), "stable automation redo");
    UndoCommand other = {.type = UNDO_CMD_NONE};
    CHECK(undo_manager_begin_drag(&state.undo, &other), "unrelated owner");
    state.automation_ui.history_serial = serial;
    CHECK(!automation_commit_edit(&state), "stale release consumed another owner");
    automation_cancel_edit(&state);
    CHECK(state.undo.active_drag_valid && state.undo.active_drag.type == UNDO_CMD_NONE, "stale cancel consumed another owner");
    CHECK(!daw_automation_remove_point(&state, 1, clip, ENGINE_AUTOMATION_TARGET_PAN, 0), "command stole other history");
    undo_manager_cancel_drag(&state.undo);
    CHECK(daw_automation_remove_point(&state, 1, clip, ENGINE_AUTOMATION_TARGET_PAN, 0) &&
          undo_manager_undo(&state.undo, &state), "delete is not undoable");
    daw_midi_editor_test_state_destroy(&state);
}

// Verifies physical gain and stereo balance against the production instrument renderer.
static void rendered_sound(void) {
    float dry[512] = {0}, wet[512] = {0};
    EngineInstrumentSource* source = engine_instrument_source_create();
    EngineInstrumentParams params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    EngineMidiNote note = {.note = 60, .velocity = .6f, .duration_frames = 48000};
    CHECK(source && engine_instrument_source_set_midi_clip(source, 0, 48000,
        ENGINE_INSTRUMENT_PRESET_PURE_SINE, params, &note, 1, NULL, 0, NULL, 0), "dry render fixture");
    engine_instrument_source_reset(source, 48000, 2);
    engine_instrument_source_render(source, dry, 256, 0);
    EngineAutomationPoint gain_point = {0, 12, ENGINE_AUTOMATION_CURVE_LINEAR};
    EngineAutomationPoint pan_point = {0, .5f, ENGINE_AUTOMATION_CURVE_LINEAR};
    EngineAutomationLane lanes[] = {
        {.target = ENGINE_AUTOMATION_TARGET_VOLUME, .points = &gain_point, .point_count = 1,
         .domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL, .before = ENGINE_AUTOMATION_BOUNDARY_HOLD, .after = ENGINE_AUTOMATION_BOUNDARY_HOLD},
        {.target = ENGINE_AUTOMATION_TARGET_PAN, .points = &pan_point, .point_count = 1,
         .domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL, .before = ENGINE_AUTOMATION_BOUNDARY_HOLD, .after = ENGINE_AUTOMATION_BOUNDARY_HOLD}
    };
    CHECK(engine_instrument_source_set_midi_clip(source, 0, 48000, ENGINE_INSTRUMENT_PRESET_PURE_SINE,
        params, &note, 1, NULL, 0, lanes, 2), "automated render fixture");
    engine_instrument_source_reset(source, 48000, 2);
    engine_instrument_source_render(source, wet, 256, 0);
    float gain = powf(10, 12.f / 20);
    double energy = 0;
    for (int i = 0; i < 256; ++i) {
        CHECK(isfinite(wet[i * 2]) && isfinite(wet[i * 2 + 1]), "nonfinite automation output");
        CHECK(fabsf(wet[i * 2] - dry[i * 2] * gain * .5f) < .00001f &&
              fabsf(wet[i * 2 + 1] - dry[i * 2 + 1] * gain) < .00001f,
              "dB gain or right balance differs from authored units");
        energy += wet[i * 2 + 1] * wet[i * 2 + 1];
    }
    CHECK(energy > .001, "render was silently empty");
    engine_instrument_source_destroy(source);
}

// Routes real keyboard controls for authored units, sample precision, curves and endpoint policies.
static void keyboard_controls(void) {
    AppState state; EngineRuntimeConfig cfg;
    daw_midi_editor_test_state_init("automation_model_test", &state, &cfg);
    int clip = -1;
    CHECK(engine_add_midi_clip_to_track(state.engine, 0, 0, 96000, &clip), "keyboard region");
    EngineAutomationPoint point = {24000, -6, ENGINE_AUTOMATION_CURVE_LINEAR};
    EngineAutomationLane lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME, .points = &point, .point_count = 1,
        .domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL};
    CHECK(daw_automation_replace_lanes(&state, 0, clip, &lane, 1), "keyboard lane");
    state.timeline_automation_mode = true;
    state.automation_ui.track_index = 0;
    state.automation_ui.clip_index = clip;
    state.automation_ui.point_index = 0;
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = SDLK_UP;
    CHECK(automation_keyboard_handle(&state, &event), "value key ownership");
    const EngineAutomationLane* current = NULL;
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &current);
    CHECK(fabsf(current->points[0].value + 5.9f) < .00001f, "dB precise nudge");
    event.key.keysym.sym = SDLK_RIGHT; event.key.keysym.mod = KMOD_ALT;
    CHECK(automation_keyboard_handle(&state, &event), "sample timing key");
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &current);
    CHECK(current->points[0].frame == 24001, "Alt nudge was not one sample");
    event.key.keysym.sym = SDLK_3; event.key.keysym.mod = KMOD_NONE;
    CHECK(automation_keyboard_handle(&state, &event), "curve key");
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &current);
    CHECK(current->points[0].curve == ENGINE_AUTOMATION_CURVE_SMOOTH, "smooth curve selection");
    event.key.keysym.sym = SDLK_LEFTBRACKET;
    CHECK(automation_keyboard_handle(&state, &event), "boundary key");
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &current);
    CHECK(current->before == ENGINE_AUTOMATION_BOUNDARY_HOLD, "boundary toggle");
    CHECK(undo_manager_undo(&state.undo, &state), "boundary undo");
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &current);
    CHECK(current->before == ENGINE_AUTOMATION_BOUNDARY_BASELINE && current->points[0].curve == ENGINE_AUTOMATION_CURVE_SMOOTH, "undo changed unrelated shape");
    event.key.keysym.sym = SDLK_DELETE;
    CHECK(automation_keyboard_handle(&state, &event) && state.automation_ui.point_index == -1, "delete key");
    CHECK(undo_manager_undo(&state.undo, &state), "delete keyboard history");
    daw_midi_editor_test_state_destroy(&state);
}

// Drives sampled timeline input to prove off-center presses stay unchanged and Alt drags retain their grab offset.
static void pointer_controls(void) {
    AppState state; EngineRuntimeConfig cfg;
    daw_midi_editor_test_state_init("automation_model_test", &state, &cfg);
    int clip = -1;
    CHECK(engine_add_midi_clip_to_track(state.engine, 0, 0, 192000, &clip), "pointer region");
    CHECK(daw_automation_add_point(&state, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, 20000, 0, NULL), "pointer point");
    state.timeline_automation_mode = true;
    state.timeline_snap_enabled = true;
    state.timeline_visible_seconds = 4;
    state.timeline_window_start_seconds = 0;
    TimelineGeometry geom;
    CHECK(timeline_compute_geometry(&state, ui_layout_get_pane(&state, 1), &geom), "pointer geometry");
    SDL_Rect rect;
    timeline_view_compute_lane_clip_rect(geom.track_top, geom.track_height, geom.content_left,
        (int)lround(4 * geom.pixels_per_second), &rect);
    int x = timeline_seconds_to_x(&geom, 20000.f / 48000);
    int y = rect.y + rect.h / 2;
    state.mouse_x = x + 4; state.mouse_y = y + 3;
    timeline_input_mouse_click_update(&state.input_manager, &state, false, true);
    CHECK(state.automation_ui.dragging && state.automation_ui.point_index == 0, "off-center point capture");
    timeline_input_mouse_click_update(&state.input_manager, &state, true, true);
    const EngineAutomationLane* lane = NULL;
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &lane);
    CHECK(lane->points[0].frame == 20000 && lane->points[0].value == 0, "press jumped point to cursor or grid");
    timeline_input_mouse_click_update(&state.input_manager, &state, true, false);
    CHECK(state.undo.undo_count == 1, "unchanged press created history");
    timeline_input_mouse_click_update(&state.input_manager, &state, false, true);
    SDL_SetModState(KMOD_ALT);
    state.mouse_x += 13;
    timeline_input_mouse_click_update(&state.input_manager, &state, true, true);
    timeline_input_mouse_click_update(&state.input_manager, &state, true, false);
    SDL_SetModState(KMOD_NONE);
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &lane);
    CHECK(lane->points[0].value == 0 && lane->points[0].frame > 20000 && lane->points[0].frame % 12000 != 0,
        "relative free timing changed vertical grab value or snapped");
    CHECK(state.undo.undo_count == 2 && undo_manager_undo(&state.undo, &state), "point gesture history");
    engine_clip_get_automation_lane(state.engine, 0, clip, ENGINE_AUTOMATION_TARGET_VOLUME, &lane);
    CHECK(lane->points[0].frame == 20000 && lane->points[0].value == 0, "point drag undo baseline");
    daw_midi_editor_test_state_destroy(&state);
}

// Reads the preserved real project without modifying it and checks backward-compatible lane metadata.
static void saved_project(const char* path) {
    SessionDocument doc = {0};
    CHECK(session_document_read_file(path, &doc), "existing saved project read");
    int clips = 0;
    for (int t = 0; t < doc.track_count; ++t) clips += doc.tracks[t].clip_count;
    CHECK(doc.track_count == 7 && clips == 40, "saved project baseline differs");
    session_document_free(&doc);
}

// Runs foundational semantics, disk roundtrip and shared command regressions.
int main(int argc, char** argv) {
    curves(); persistence(); commands(); rendered_sound(); keyboard_controls(); pointer_controls();
    if (argc > 1) saved_project(argv[1]);
    puts("automation_model_test: success");
    return 0;
}
