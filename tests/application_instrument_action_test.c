#include "app_state.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define CHECK(value, message) daw_test_expect("application_instrument_action_test", (value), (message))
static bool reject_publication, reject_history;
static int publications, history_allocations;

// Intercepts the real preparation boundary without changing production allocation behavior.
static bool action_publish(Engine* engine) {
    ++publications;
    return !reject_publication && engine_request_rebuild_sources(engine);
}

// Rejects history growth and counts whether finishing a reserved edit allocates again.
static void* action_history_realloc(void* ptr, size_t size) {
    ++history_allocations;
    return reject_history ? NULL : realloc(ptr, size);
}

#define engine_request_rebuild_sources action_publish
#include "../src/engine/engine_clips_midi.c"
#undef engine_request_rebuild_sources
#define realloc action_history_realloc
#include "../src/undo/undo_manager_stack.c"
#undef realloc
#include "../src/app/app_actions.c"

// Creates an isolated project with one inherited MIDI region and one authored note.
static AppState* fixture(DawActionTarget* region, DawActionTarget* track) {
    AppState* state = calloc(1, sizeof(*state));
    CHECK(state != NULL, "state allocation");
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    state->engine = engine_create(&config);
    CHECK(state->engine != NULL, "engine creation");
    undo_manager_init(&state->undo);
    state->selected_track_index = state->selected_clip_index = -1;
    int clip;
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 0, 48000, &clip), "region creation");
    EngineMidiNote note = {.start_frame = 100, .duration_frames = 1000, .note = 60, .velocity = .75f};
    CHECK(engine_clip_midi_add_note(state->engine, 0, clip, note, NULL), "fixture note");
    CHECK(engine_track_midi_set_instrument_enabled(state->engine, 0, false), "disabled track fixture");
    CHECK(daw_instrument_target_capture(state, 0, clip, region) == DAW_ACTION_UNCHANGED, "region handle");
    CHECK(daw_instrument_target_capture(state, 0, -1, track) == DAW_ACTION_UNCHANGED, "track handle");
    publications = history_allocations = 0;
    return state;
}

// Retires the fixture without saving or touching any user project.
static void destroy_fixture(AppState* state) {
    undo_manager_free(&state->undo);
    engine_destroy(state->engine);
    free(state);
}

// Builds a two-parameter patch that also changes the synthesis preset.
static DawInstrumentRequest patch(DawActionTarget target) {
    DawInstrumentRequest request = {.target = target, .set_preset = true,
        .preset = ENGINE_INSTRUMENT_PRESET_SYNTH_LAB,
        .parameter_mask = (UINT32_C(1) << ENGINE_INSTRUMENT_PARAM_LEVEL) |
                          (UINT32_C(1) << ENGINE_INSTRUMENT_PARAM_ATTACK_MS)};
    request.params.level = .37f;
    request.params.attack_ms = 73;
    return request;
}

// Verifies one publication, exact stored/effective readback, no-op history and scalar-only undo/redo.
static void test_batch_and_history(void) {
    DawActionTarget region, track;
    AppState* state = fixture(&region, &track);
    DawActionResult before = daw_action_get_instrument(state, region);
    CHECK(before.has_snapshot && before.snapshot.stored.inherits_track, "initial inheritance");
    DawInstrumentRequest request = patch(region);
    DawActionResult result = daw_action_set_instrument(state, &request);
    CHECK(result.status == DAW_ACTION_APPLIED && result.has_snapshot && publications == 1, "batch publication/readback");
    CHECK(result.snapshot.stored.params.level == .37f && result.snapshot.stored.params.attack_ms == 73 &&
          result.snapshot.effective_preset == request.preset && !result.snapshot.stored.inherits_track, "batch settings");
    CHECK(state->undo.undo_count == 1 && !state->undo.active_drag_valid, "one complete undo entry");
    publications = 0;
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_UNCHANGED && publications == 0 &&
          state->undo.undo_count == 1, "no-op created publication or history");
    // A later note edit must survive instrument-only history restoration.
    EngineMidiNote second = {.start_frame = 2000, .duration_frames = 1000, .note = 64, .velocity = .5f};
    CHECK(engine_clip_midi_add_note(state->engine, 0, 0, second, NULL), "later note edit");
    CHECK(undo_manager_undo(&state->undo, state), "instrument undo");
    DawActionResult undone = daw_action_get_instrument(state, region);
    CHECK(undone.snapshot.stored.inherits_track && undone.snapshot.stored.params.level == before.snapshot.stored.params.level &&
          engine_get_tracks(state->engine)[0].clips[0].midi_notes.note_count == 2, "undo damaged notes or inherited state");
    CHECK(undo_manager_redo(&state->undo, state), "instrument redo");
    CHECK(daw_action_get_instrument(state, region).snapshot.stored.params.level == .37f, "redo scalar values");
    request.target = track;
    DawInstrumentState disabled = daw_action_get_instrument(state, track).snapshot.stored;
    CHECK(!disabled.enabled, "track starts disabled");
    publications = 0;
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_APPLIED && publications == 1, "track patch batch");
    CHECK(undo_manager_undo(&state->undo, state) && !engine_track_midi_instrument_enabled(state->engine, 0), "disabled track restoration");
    CHECK(daw_action_get_instrument(state, track).snapshot.stored.params.level == disabled.params.level, "inactive params restoration");
    CHECK(undo_manager_redo(&state->undo, state) && engine_track_midi_instrument_enabled(state->engine, 0), "track redo enable");
    destroy_fixture(state);
}

// Verifies malformed requests and reservation/publication failures preserve content and redo history.
static void test_rejections(void) {
    DawActionTarget region, track;
    AppState* state = fixture(&region, &track);
    DawInstrumentRequest request = patch(region);
    reject_history = true;
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_HISTORY_UNAVAILABLE && publications == 0 &&
          !state->undo.active_drag_valid && state->undo.undo_count == 0, "history failure published content");
    reject_history = false;
    DawInstrumentRequest invalid = request;
    invalid.params.level = NAN;
    CHECK(daw_action_set_instrument(state, &invalid).status == DAW_ACTION_INVALID, "NaN accepted");
    invalid.params.level = INFINITY;
    CHECK(daw_action_set_instrument(state, &invalid).status == DAW_ACTION_INVALID, "infinity accepted");
    invalid.params.level = 1.51f;
    CHECK(daw_action_set_instrument(state, &invalid).status == DAW_ACTION_INVALID, "out-of-range silently clamped");
    invalid = request; invalid.parameter_mask |= UINT32_C(1) << ENGINE_INSTRUMENT_PARAM_COUNT;
    CHECK(daw_action_set_instrument(state, &invalid).status == DAW_ACTION_INVALID, "unknown parameter bit accepted");
    invalid = request; invalid.preset = ENGINE_INSTRUMENT_PRESET_COUNT;
    CHECK(daw_action_set_instrument(state, &invalid).status == DAW_ACTION_INVALID && publications == 0, "unknown preset accepted");
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_APPLIED && undo_manager_undo(&state->undo, state), "redo fixture");
    int redo = state->undo.redo_count;
    DawInstrumentState before = daw_action_get_instrument(state, region).snapshot.stored;
    reject_publication = true;
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_ENGINE_REJECTED &&
          state->undo.redo_count == redo && !state->undo.active_drag_valid && state->undo.undo_count == 0, "failed publication damaged history");
    CHECK(daw_action_get_instrument(state, region).snapshot.stored.inherits_track == before.inherits_track, "failed publication damaged inheritance");
    CHECK(!undo_manager_redo(&state->undo, state) && state->undo.redo_count == redo, "failed redo consumed history");
    reject_publication = false;
    CHECK(undo_manager_redo(&state->undo, state), "redo retry");
    destroy_fixture(state);
}

// Verifies serial ownership, cancellation retry and allocation-free history transfer after preview publication.
static void test_gestures(void) {
    DawActionTarget region, track;
    AppState* state = fixture(&region, &track);
    DawInstrumentRequest request = patch(region);
    DawInstrumentGesture gesture = {0};
    CHECK(daw_action_begin_instrument(state, region, &gesture).has_snapshot && gesture.active, "gesture reservation");
    CHECK(daw_action_set_instrument(state, &request).status == DAW_ACTION_BUSY, "discrete edit stole gesture");
    CHECK(daw_action_update_instrument(state, &gesture, &request).status == DAW_ACTION_APPLIED, "gesture preview");
    reject_publication = true;
    CHECK(daw_action_finish_instrument(state, &gesture, false).status == DAW_ACTION_ENGINE_REJECTED &&
          gesture.active && state->undo.active_drag_valid && state->undo.undo_count == 0, "failed cancel lost baseline");
    reject_publication = false;
    CHECK(daw_action_finish_instrument(state, &gesture, false).has_snapshot && !gesture.active &&
          daw_action_get_instrument(state, region).snapshot.stored.inherits_track, "cancel retry failed");
    CHECK(daw_action_begin_instrument(state, region, &gesture).has_snapshot, "second gesture");
    DawInstrumentGesture stale = gesture;
    CHECK(daw_action_update_instrument(state, &gesture, &request).status == DAW_ACTION_APPLIED, "second preview");
    history_allocations = 0; reject_history = true;
    CHECK(daw_action_finish_instrument(state, &gesture, true).status == DAW_ACTION_APPLIED &&
          history_allocations == 0 && state->undo.undo_count == 1, "release allocated or lost history");
    reject_history = false;
    CHECK(daw_action_begin_instrument(state, region, &gesture).has_snapshot, "new reservation");
    uint64_t serial = state->undo.drag_serial;
    CHECK(daw_action_update_instrument(state, &stale, &request).status == DAW_ACTION_BUSY, "stale preview accepted");
    CHECK(daw_action_finish_instrument(state, &stale, false).status == DAW_ACTION_BUSY &&
          state->undo.active_drag_valid && state->undo.drag_serial == serial, "stale release cancelled newer gesture");
    CHECK(daw_action_finish_instrument(state, &gesture, true).status == DAW_ACTION_UNCHANGED && state->undo.undo_count == 1, "empty gesture changed history");
    destroy_fixture(state);
}

// Holds off-thread request outcomes until the owning test thread joins the worker.
typedef struct WrongThreadCase {
    AppState* state;
    DawInstrumentRequest request;
    DawActionStatus read_status, write_status, capture_status;
} WrongThreadCase;

// Attempts inspection and mutation from a non-owner without reading mutable project storage.
static int wrong_thread(void* userdata) {
    WrongThreadCase* test = userdata;
    DawActionTarget unused;
    test->read_status = daw_action_get_instrument(test->state, test->request.target).status;
    test->write_status = daw_action_set_instrument(test->state, &test->request).status;
    test->capture_status = daw_instrument_target_capture(test->state, 0, 0, &unused);
    return 0;
}

// Verifies sorting, removed identities, project replacement and owner-thread refusal.
static void test_target_lifetimes(void) {
    DawActionTarget region, track;
    AppState* state = fixture(&region, &track);
    WrongThreadCase test = {.state = state, .request = patch(region)};
    SDL_Thread* worker = SDL_CreateThread(wrong_thread, "action-owner-test", &test);
    CHECK(worker != NULL, "owner test thread"); SDL_WaitThread(worker, NULL);
    CHECK(test.read_status == DAW_ACTION_WRONG_THREAD && test.write_status == DAW_ACTION_WRONG_THREAD &&
          test.capture_status == DAW_ACTION_WRONG_THREAD && publications == 0, "off-thread access accepted");
    int extra;
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 10000, 48000, &extra), "sorting neighbor");
    CHECK(engine_clip_set_timeline_start(state->engine, 0, 0, 20000, NULL), "region sort");
    CHECK(daw_action_set_instrument(state, &test.request).status == DAW_ACTION_APPLIED, "sorted target lost");
    CHECK(engine_get_tracks(state->engine)[0].clips[0].creation_index != region.clip_id &&
          engine_get_tracks(state->engine)[0].clips[1].instrument_params.level == .37f, "sorted action redirected");
    CHECK(engine_remove_clip(state->engine, 0, 1), "remove original target");
    CHECK(daw_action_set_instrument(state, &test.request).status == DAW_ACTION_STALE_TARGET, "removed clip accepted");
    int history = state->undo.undo_count;
    CHECK(!undo_manager_undo(&state->undo, state) && state->undo.undo_count == history, "removed target consumed history");
    EngineRuntimeConfig config; config_set_defaults(&config);
    Engine* old = state->engine;
    state->engine = engine_create(&config);
    CHECK(state->engine != NULL && engine_instance_id(state->engine) != track.project_generation, "engine generation reused");
    CHECK(daw_action_get_instrument(state, track).status == DAW_ACTION_STALE_TARGET, "replacement accepted old track handle");
    CHECK(!undo_manager_undo(&state->undo, state) && state->undo.undo_count == history, "replacement consumed old history");
    engine_destroy(old);
    destroy_fixture(state);
}

// Runs isolated software action, history and lifetime qualification without opening an audio device.
int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER) == 0, "SDL timer initialization");
    test_batch_and_history();
    test_rejections();
    test_gestures();
    test_target_lifetimes();
    SDL_Quit();
    puts("application_instrument_action_test: success");
    return 0;
}
