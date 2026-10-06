#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "session.h"
#include "engine/engine_internal.h"
#include "engine/graph.h"
#include "audio/wav_writer.h"
#include "ui/effects_panel.h"
#include "test_assert.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(v, m) daw_test_expect("session_transaction_test", (v), (m))
static int allocation_count, allocation_failure;
static bool fail_engine, fail_track, fail_fx;

// Injects each allocation owned by capture/restore while leaving the previous project available.
static void* transaction_calloc(size_t n, size_t size) {
    if (++allocation_count == allocation_failure) return NULL;
    return calloc(n, size);
}

// Exercises candidate-state and private registry allocation failures.
static void* transaction_malloc(size_t size) {
    if (++allocation_count == allocation_failure) return NULL;
    return malloc(size);
}

// Separates candidate engine creation failure from the live engine's lifetime.
static Engine* transaction_engine_create(const EngineRuntimeConfig* config) {
    return fail_engine ? NULL : engine_create(config);
}

// Models a rejected required structural edit after candidate preparation has started.
static int transaction_add_track(Engine* engine) { return fail_track ? -1 : engine_add_track(engine); }

// Models effect construction failure after tracks, clips, and maps have been prepared.
static FxInstId transaction_master_fx_add(Engine* engine, FxTypeId type) {
    return fail_fx ? 0 : engine_fx_master_add(engine, type);
}

#define calloc transaction_calloc
#define malloc transaction_malloc
#include "../src/session/session_document.c"
#define engine_create transaction_engine_create
#define engine_add_track transaction_add_track
#define engine_fx_master_add transaction_master_fx_add
#include "../src/session/session_apply.c"
#undef engine_fx_master_add
#undef engine_add_track
#undef engine_create
#undef malloc
#undef calloc

// Creates a complete control-owned project with maps, MIDI, automation, audio, EQ, and effects.
static AppState* create_fixture(const char* directory, const char* audio_path) {
    AppState* state = calloc(1, sizeof(*state));
    CHECK(state != NULL, "fixture allocation");
    config_set_defaults(&state->runtime_cfg);
    state->engine = engine_create(&state->runtime_cfg);
    CHECK(state->engine != NULL, "fixture engine");
    state->tempo = tempo_state_default(state->runtime_cfg.sample_rate);
    TempoEvent tempos[] = {{.beat = 0, .bpm = 110}, {.beat = 8, .bpm = 140}};
    TimeSignatureEvent signatures[] = {{.beat = 0, .ts_num = 4, .ts_den = 4}, {.beat = 8, .ts_num = 3, .ts_den = 4}};
    CHECK(tempo_map_set_events(&state->tempo_map, tempos, 2), "fixture tempo map");
    CHECK(time_signature_map_set_events(&state->time_signature_map, signatures, 2), "fixture signature map");
    state->timeline_visible_seconds = 8;
    state->timeline_vertical_scale = 1;
    state->selected_track_index = 1;
    state->selected_clip_index = 0;
    state->active_track_index = 1;
    state->selection_count = 1;
    state->selection[0] = (TimelineSelectionEntry){.track_index = 1, .clip_index = 0};
    SDL_strlcpy(state->data_paths.input_root, directory, sizeof(state->data_paths.input_root));
    SDL_strlcpy(state->data_paths.output_root, directory, sizeof(state->data_paths.output_root));
    SDL_strlcpy(state->data_paths.library_copy_root, directory, sizeof(state->data_paths.library_copy_root));
    media_registry_init(&state->media_registry, NULL);
    CHECK(media_registry_ensure_for_path(&state->media_registry, audio_path, "tone", NULL), "fixture registry");
    CHECK(engine_add_clip_to_track(state->engine, 0, audio_path, 64, NULL), "fixture audio");
    CHECK(engine_clip_set_gain(state->engine, 0, 0, 0.25f), "fixture gain");
    CHECK(engine_add_track(state->engine) == 1, "fixture MIDI track");
    CHECK(engine_add_midi_clip_to_track(state->engine, 1, 128, 512, NULL), "fixture MIDI clip");
    EngineMidiNote note = {.start_frame = 0, .duration_frames = 256, .note = 60, .velocity = 0.7f};
    CHECK(engine_clip_midi_add_note(state->engine, 1, 0, note, NULL), "fixture MIDI note");
    CHECK(engine_clip_add_automation_point(state->engine, 1, 0, ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL, 0, 0.5f, NULL), "fixture automation");
    CHECK(engine_fx_master_add(state->engine, 1) != 0 && engine_fx_track_add(state->engine, 0, 1) != 0, "fixture effects");
    EngineEqCurve curve;
    CHECK(engine_get_eq_curve(state->engine, -1, &curve), "fixture EQ read");
    curve.bands[0].enabled = true;
    curve.bands[0].gain_db = 3;
    CHECK(engine_set_master_eq_curve(state->engine, &curve), "fixture EQ accepted");
    state->effects_panel.eq_curve_master.bands[0].gain_db = 19; // A stale UI draft must not become the saved processor state.
    undo_manager_init(&state->undo);
    UndoCommand history = {.type = UNDO_CMD_CLIP_RENAME};
    CHECK(undo_manager_push(&state->undo, &history), "fixture history");
    return state;
}

// Releases only the resources the fixture and accepted project own.
static void destroy_fixture(AppState* state) {
    engine_destroy(state->engine);
    tempo_map_free(&state->tempo_map);
    time_signature_map_free(&state->time_signature_map);
    free(state->media_registry.entries);
    free(state->effects_panel.eq_curve_tracks);
    free(state->effects_panel.last_open_track_fx_ids);
    free(state->pending_track_fx);
    undo_manager_free(&state->undo);
    free(state);
}

// Attempts project operations from a non-owner to prove they fail before touching state.
static int wrong_thread(void* context) {
    AppState* state = context;
    SessionDocument doc;
    session_document_init(&doc);
    bool capture = session_document_capture(state, &doc);
    bool restore = session_apply_document(state, &doc);
    session_document_free(&doc);
    return capture || restore;
}

// Verifies failure rollback, immutable input, deep ownership, successful retry, and live capture.
int main(void) {
    char directory[] = "/tmp/daw-session-transaction-XXXXXX";
    CHECK(mkdtemp(directory) != NULL, "temporary directory");
    char audio_path[1024];
    snprintf(audio_path, sizeof(audio_path), "%s/tone.wav", directory);
    float audio[2048];
    for (int i = 0; i < 2048; ++i) audio[i] = 0.25f;
    CHECK(wav_write_f32(audio_path, audio, 1024, 2, 48000), "audio fixture");
    AppState* state = create_fixture(directory, audio_path);
    SessionDocument captured;
    session_document_init(&captured);
    allocation_count = 0;
    CHECK(session_document_capture(state, &captured), "initial complete capture");
    int capture_allocations = allocation_count;
    float before_audio[256] = {0};
    engine_graph_render_track(engine_render_source_graph(state->engine), before_audio, 128, 64, 0);
    CHECK(before_audio[0] != 0, "restoration audio baseline is silent");
    CHECK(captured.tempo_event_count == 2 && captured.time_signature_event_count == 2 && captured.track_count == 2 &&
          captured.master_fx_count == 1 && captured.tracks[0].fx_count == 1 && captured.tracks[1].clips[0].midi_note_count == 1,
          "capture omitted content");
    CHECK(captured.effects_panel.bands[0].gain_db == 3 && state->effects_panel.eq_curve_master.bands[0].gain_db == 19,
          "capture used or mutated stale EQ UI state");
    SessionTrack* original_tracks = captured.tracks;
    char original_media_id[MEDIA_ID_MAX];
    SDL_strlcpy(original_media_id, captured.tracks[0].clips[0].media_id, sizeof(original_media_id));
    for (int i = 1; i <= capture_allocations; ++i) {
        allocation_count = 0; allocation_failure = i;
        CHECK(!session_document_capture(state, &captured), "capture allocation failure ignored");
        CHECK(captured.tracks == original_tracks && captured.tempo_event_count == 2 && captured.tracks[1].clips[0].midi_note_count == 1,
              "failed capture replaced previous complete document");
    }
    allocation_failure = 0;
    Engine* original_engine = state->engine;
    TempoEvent* original_tempos = state->tempo_map.events;
    MediaRegistryEntry* original_registry = state->media_registry.entries;
    CHECK(engine_transport_play(state->engine), "old project play state");
    // Sweep restore-owned allocations without permitting a successful commit in this loop.
    int restore_allocations = 0;
    for (int i = 1; i < 30; ++i) {
        allocation_count = 0; allocation_failure = i; fail_fx = true;
        CHECK(!session_apply_document(state, &captured), "faulted restore committed");
        int attempted = allocation_count;
        CHECK(state->engine == original_engine && state->tempo_map.events == original_tempos &&
              state->media_registry.entries == original_registry && state->undo.undo_count == 1 &&
              engine_transport_is_playing(state->engine), "failed restore changed live project");
        CHECK(captured.tracks == original_tracks && !strcmp(captured.tracks[0].clips[0].media_id, original_media_id), "restore mutated input document");
        if (attempted < i) { restore_allocations = attempted; break; }
    }
    CHECK(restore_allocations > 0, "restore allocation sweep did not terminate");
    allocation_failure = 0; fail_fx = false;
    fail_engine = true;
    CHECK(!session_apply_document(state, &captured) && state->engine == original_engine, "engine failure discarded old project");
    fail_engine = false; fail_track = true;
    CHECK(!session_apply_document(state, &captured) && state->engine == original_engine, "track failure discarded old project");
    fail_track = false;
    char saved_path[SESSION_PATH_MAX];
    SDL_strlcpy(saved_path, captured.tracks[0].clips[0].media_path, sizeof(saved_path));
    captured.tracks[0].clips[0].media_id[0] = 0;
    SDL_strlcpy(captured.tracks[0].clips[0].media_path, "/nonexistent/daw-missing.wav", sizeof(captured.tracks[0].clips[0].media_path));
    CHECK(!session_apply_document(state, &captured) && state->engine == original_engine && state->undo.undo_count == 1,
          "missing media replaced old project");
    SDL_strlcpy(captured.tracks[0].clips[0].media_path, saved_path, sizeof(captured.tracks[0].clips[0].media_path));
    SDL_strlcpy(captured.tracks[0].clips[0].media_id, original_media_id, sizeof(captured.tracks[0].clips[0].media_id));
    SDL_Thread* wrong = SDL_CreateThread(wrong_thread, "session_wrong_owner", state);
    int result = -1;
    CHECK(wrong != NULL, "wrong owner fixture");
    SDL_WaitThread(wrong, &result);
    CHECK(result == 0, "wrong owner performed project operation");
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(engine_start(state->engine), "live capture fixture startup");
    CHECK(engine_transport_play(state->engine), "live capture play");
    for (int i = 0; i < 30; ++i) {
        SessionDocument live;
        session_document_init(&live);
        CHECK(session_document_capture(state, &live) && live.track_count == 2 && live.master_fx_count == 1, "live capture incomplete");
        session_document_free(&live);
    }
    fail_fx = true;
    CHECK(!session_apply_document(state, &captured) && engine_is_running(original_engine), "failed restore stopped old device");
    fail_fx = false;
    CHECK(session_apply_document(state, &captured), "complete restore retry");
    CHECK(state->engine != original_engine && state->undo.undo_count == 0 && !engine_is_running(state->engine), "accepted restore ownership/history");
    CHECK(!state->pending_master_fx_dirty && !state->pending_track_fx_dirty, "effects deferred beyond commit");
    float restored_audio[256] = {0};
    engine_graph_render_track(engine_render_source_graph(state->engine), restored_audio, 128, 64, 0);
    CHECK(memcmp(before_audio, restored_audio, sizeof(before_audio)) == 0, "restored audio source samples changed");
    SessionDocument roundtrip;
    session_document_init(&roundtrip);
    CHECK(session_document_capture(state, &roundtrip), "recapture restored project");
    CHECK(roundtrip.track_count == 2 && roundtrip.tracks[0].clip_count == 1 && roundtrip.tracks[1].clip_count == 1 &&
          roundtrip.tracks[0].clips[0].gain == 0.25f && roundtrip.tracks[1].clips[0].midi_notes[0].note == 60 &&
          roundtrip.tracks[0].fx_count == 1 && roundtrip.master_fx_count == 1 && roundtrip.tempo_event_count == 2 &&
          roundtrip.time_signature_event_count == 2 && roundtrip.effects_panel.bands[0].gain_db == 3,
          "restore/recapture dropped project content");
    // External documents may list clips in an order different from the engine's timeline order.
    captured.tracks[0].clips = realloc(captured.tracks[0].clips, 2 * sizeof(SessionClip));
    CHECK(captured.tracks[0].clips != NULL, "unsorted fixture allocation");
    captured.tracks[0].clips[1] = captured.tracks[0].clips[0];
    captured.tracks[0].clips[1].automation_lanes = NULL;
    captured.tracks[0].clips[1].automation_lane_count = 0;
    captured.tracks[0].clips[1].midi_notes = NULL;
    captured.tracks[0].clips[1].midi_note_count = 0;
    captured.tracks[0].clip_count = 2;
    captured.tracks[0].clips[0].start_frame = 300;
    captured.tracks[0].clips[0].selected = true;
    captured.tracks[0].clips[1].start_frame = 100;
    captured.tracks[0].clips[1].selected = false;
    captured.tracks[1].clips[0].selected = false;
    captured.selected_track_index = captured.active_track_index = 0;
    captured.selected_clip_index = 0;
    captured.selection_count = 1;
    captured.selection[0].track_index = 0;
    captured.selection[0].clip_index = 0;
    captured.clip_inspector.visible = true;
    captured.clip_inspector.track_index = 0;
    captured.clip_inspector.clip_index = 0;
    CHECK(session_apply_document(state, &captured), "unsorted document restore");
    CHECK(state->selected_track_index == 0 && state->selected_clip_index == 1 &&
          state->selection[0].clip_index == 1 && state->inspector.clip_index == 1 && engine_get_tracks(state->engine)[0].clips[1].timeline_start_frames == 300,
          "sorting changed restored selection identity");
    session_document_free(&roundtrip);
    session_document_free(&captured);
    destroy_fixture(state);
    CHECK(unlink(audio_path) == 0 && rmdir(directory) == 0, "fixture cleanup");
    printf("session_transaction_test: success (%d capture and %d restore allocation boundaries, live capture, rollback, complete retry)\n",
           capture_allocations, restore_allocations);
    return 0;
}
