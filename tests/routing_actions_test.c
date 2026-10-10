#include "app_state.h"
#include "daw/routing_actions.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("routing_actions_test",(v),(m))
static bool fail_publication, fail_reservation;
static int publications;
// Injects failure exactly at routing candidate publication.
static bool publish(Engine* engine) { ++publications; return !fail_publication && engine_request_rebuild_sources(engine); }
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_routing.c"
#include "../src/engine/engine_tracks.c"
#undef engine_request_rebuild_sources
// Refuses history ownership before a routing candidate can publish.
static void* history_realloc(void* pointer, size_t size) { return fail_reservation ? NULL : realloc(pointer, size); }
#define realloc history_realloc
#include "../src/undo/undo_manager_stack.c"
#undef realloc
// Copies the complete current routing for guarded creative actions.
static EngineRoutingSnapshot capture(AppState* state) {
    EngineRoutingSnapshot result;
    CHECK(daw_action_get_routing(state, &result) == DAW_ACTION_UNCHANGED, "capture");
    return result;
}
// Carries application-owner refusal outcomes between joined fixture threads.
typedef struct { AppState* state; EngineRoutingSnapshot* expected; DawActionStatus read, write, history; } ThreadCheck;
// Attempts all routing entry points from a foreign thread without changing project ownership.
static int wrong_thread(void* value) {
    ThreadCheck* check = value; EngineRoutingSnapshot out;
    check->read = daw_action_get_routing(check->state, &out);
    check->write = daw_action_set_routing(check->state, check->expected, check->expected, &out);
    check->history = daw_action_routing_history(check->state, check->expected, false, &out);
    return 0;
}
// Qualifies empty-project return creation and stable one-publication history with later-edit protection.
static void creation(void) {
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "creation state");
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine = engine_create(&config);
    CHECK(state->engine && engine_remove_track(state->engine, 0), "empty project");
    undo_manager_init(&state->undo);
    EngineRoutingSnapshot empty = capture(state), out; CHECK(!empty.track_count, "zero lanes");
    publications = 0; fail_reservation = true;
    CHECK(daw_action_create_return(state, &empty, &out) == DAW_ACTION_HISTORY_UNAVAILABLE && !publications, "creation history reservation refusal");
    fail_reservation = false; fail_publication = true;
    CHECK(daw_action_create_return(state, &empty, &out) == DAW_ACTION_ENGINE_REJECTED, "creation publication refusal");
    fail_publication = false; out = capture(state);
    CHECK(engine_routing_snapshot_equal(&empty, &out) && !state->undo.undo_count && !state->undo.active_drag_valid, "creation rollback");
    int count = publications;
    CHECK(daw_action_create_return(state, &empty, &out) == DAW_ACTION_APPLIED && publications == count + 1 && state->undo.undo_count == 1 &&
          out.track_count == 1 && out.tracks[0].is_return, "creation from empty one publication/history");
    EngineRoutingSnapshot created = out; uint64_t identity = created.tracks[0].track_id;
    CHECK(daw_action_create_return(state, &empty, &out) == DAW_ACTION_STALE_TARGET, "creation stale guard");
    CHECK(engine_track_set_gain(state->engine, 0, .7f), "later created-lane mixer edit");
    count = publications;
    CHECK(daw_action_routing_history(state, &created, false, &out) == DAW_ACTION_ENGINE_REJECTED && publications == count && state->undo.undo_count == 1,
          "creation undo preserves later lane edits");
    CHECK(engine_track_set_gain(state->engine, 0, 1), "restore lane fader");
    fail_publication = true;
    CHECK(daw_action_routing_history(state, &created, false, &out) == DAW_ACTION_ENGINE_REJECTED && state->undo.undo_count == 1, "creation undo publication rollback");
    fail_publication = false; count = publications;
    CHECK(daw_action_routing_history(state, &created, false, &out) == DAW_ACTION_APPLIED && publications == count + 1 &&
          engine_routing_snapshot_equal(&empty, &out), "creation undo returns to empty");
    fail_publication = true;
    CHECK(daw_action_routing_history(state, &empty, true, &out) == DAW_ACTION_ENGINE_REJECTED && state->undo.redo_count == 1, "creation redo rollback");
    fail_publication = false; count = publications;
    CHECK(daw_action_routing_history(state, &empty, true, &out) == DAW_ACTION_APPLIED && publications == count + 1 &&
          engine_routing_snapshot_equal(&created, &out) && out.tracks[0].track_id == identity, "creation redo retains identity");
    FxInstId effect = engine_fx_track_add(state->engine, 0, 1); CHECK(effect, "later return effect");
    CHECK(daw_action_routing_history(state, &created, false, &out) == DAW_ACTION_ENGINE_REJECTED && state->undo.undo_count == 1,
          "creation undo preserves later effects");
    CHECK(engine_fx_track_remove(state->engine, 0, effect), "remove fixture effect");
    for (int n = 1; n < ENGINE_RETURN_CAP; ++n) {
        EngineRoutingSnapshot expected = capture(state);
        CHECK(daw_action_create_return(state, &expected, &out) == DAW_ACTION_APPLIED, "return capacity creation");
    }
    EngineRoutingSnapshot full = capture(state); count = publications;
    CHECK(daw_action_create_return(state, &full, &out) == DAW_ACTION_INVALID && publications == count && full.track_count == ENGINE_RETURN_CAP,
          "return capacity refusal before publication");
    for (int n = 0; n < ENGINE_RETURN_CAP; ++n) {
        EngineRoutingSnapshot expected = capture(state);
        CHECK(daw_action_routing_history(state, &expected, false, &out) == DAW_ACTION_APPLIED, "creation stack undo");
    }
    CHECK(!out.track_count, "all creation history restores empty project");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Exercises one-step routing edits/history and explicit stale, busy and preparation refusals.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER), "SDL");
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "state");
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine = engine_create(&config);
    CHECK(state->engine && engine_add_track(state->engine) == 1, "engine/return lane");
    undo_manager_init(&state->undo);
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 0, 4096, NULL) &&
          engine_clip_midi_add_note(state->engine, 0, 0, (EngineMidiNote){0, 2048, 60, .5f}, NULL), "musical source");
    EngineBounceOptions options = engine_bounce_options_default(); options.normalize_if_clipping = false;
    EngineBounceBuffer dry = {0}, wet = {0}, restored = {0};
    CHECK(engine_bounce_range_to_buffer_with_options(state->engine, 0, 4096, &options, NULL, NULL, &dry), "dry production audio");
    EngineRoutingSnapshot before = capture(state), next = before, out;
    publications = 0;
    next.tracks[1].is_return = true;
    next.tracks[0].sends[2] = (EngineSend){next.tracks[1].track_id, .5f};
    ThreadCheck thread_check = {.state = state, .expected = &before};
    SDL_Thread* thread = SDL_CreateThread(wrong_thread, "routing-owner", &thread_check);
    CHECK(thread != NULL, "thread"); SDL_WaitThread(thread, NULL);
    CHECK(thread_check.read == DAW_ACTION_WRONG_THREAD && thread_check.write == DAW_ACTION_WRONG_THREAD &&
          thread_check.history == DAW_ACTION_WRONG_THREAD && !publications, "owner refusal");
    fail_reservation = true;
    CHECK(daw_action_set_routing(state, &before, &next, &out) == DAW_ACTION_HISTORY_UNAVAILABLE && !publications, "history reservation refusal");
    fail_reservation = false; fail_publication = true;
    CHECK(daw_action_set_routing(state, &before, &next, &out) == DAW_ACTION_ENGINE_REJECTED, "publication refusal");
    fail_publication = false; out = capture(state);
    CHECK(engine_routing_snapshot_equal(&before, &out) && !state->undo.undo_count && !state->undo.active_drag_valid, "failure rollback");
    int count = publications;
    CHECK(daw_action_set_routing(state, &before, &next, &out) == DAW_ACTION_APPLIED && publications == count + 1 && state->undo.undo_count == 1, "one publication/history");
    CHECK(engine_routing_snapshot_equal(&next, &out), "exact accepted routing");
    CHECK(engine_bounce_range_to_buffer_with_options(state->engine, 0, 4096, &options, NULL, NULL, &wet), "wet production audio");
    for (size_t sample = 0; sample < dry.frame_count * dry.channels; ++sample)
        CHECK(fabsf(wet.data[sample] - dry.data[sample] * 1.5f) < 1e-6f, "accepted action audible return sum");
    CHECK(daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_APPLIED, "audio undo");
    CHECK(engine_bounce_range_to_buffer_with_options(state->engine, 0, 4096, &options, NULL, NULL, &restored) &&
          !memcmp(restored.data, dry.data, dry.frame_count * dry.channels * sizeof(float)), "undo exact original production audio");
    engine_bounce_buffer_free(&restored);
    CHECK(daw_action_routing_history(state, &before, true, &out) == DAW_ACTION_APPLIED, "audio redo");
    CHECK(engine_bounce_range_to_buffer_with_options(state->engine, 0, 4096, &options, NULL, NULL, &restored) &&
          !memcmp(restored.data, wet.data, wet.frame_count * wet.channels * sizeof(float)), "redo exact accepted production audio");
    engine_bounce_buffer_free(&restored); engine_bounce_buffer_free(&wet); engine_bounce_buffer_free(&dry);
    CHECK(daw_action_set_routing(state, &before, &next, &out) == DAW_ACTION_STALE_TARGET, "stale edit");
    count = publications;
    CHECK(daw_action_set_routing(state, &next, &next, &out) == DAW_ACTION_UNCHANGED && publications == count && state->undo.undo_count == 1, "no-op preserves history");
    state->bounce_active = true;
    CHECK(daw_action_set_routing(state, &next, &before, &out) == DAW_ACTION_BUSY &&
          daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_BUSY, "busy edit/history");
    state->bounce_active = false;
    fail_publication = true;
    CHECK(daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_ENGINE_REJECTED && state->undo.undo_count == 1, "history failure retains cursor");
    fail_publication = false;
    CHECK(engine_track_set_gain(state->engine, 0, .7f), "unrelated mixer edit");
    CHECK(daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_APPLIED && engine_routing_snapshot_equal(&before, &out), "scoped undo");
    CHECK(engine_get_tracks(state->engine)[0].gain == .7f, "routing undo preserves mixer");
    CHECK(daw_action_routing_history(state, &before, true, &out) == DAW_ACTION_APPLIED && engine_routing_snapshot_equal(&next, &out), "exact redo");
    EngineRoutingSnapshot changed = next; changed.tracks[0].sends[2].gain = .25f;
    CHECK(engine_routing_set(state->engine, &next, &changed), "external routing edit");
    CHECK(daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_STALE_TARGET, "stale history guard");
    CHECK(daw_action_routing_history(state, &changed, false, &out) == DAW_ACTION_ENGINE_REJECTED && state->undo.undo_count == 1, "external change preserves cursor");
    EngineRoutingSnapshot invalid = changed; invalid.tracks[1].sends[0] = (EngineSend){changed.tracks[1].track_id,.1f};
    CHECK(daw_action_set_routing(state, &changed, &invalid, &out) == DAW_ACTION_INVALID, "feedback refusal");
    invalid = changed; invalid.tracks[0].sends[2].gain = NAN;
    CHECK(daw_action_set_routing(state, &changed, &invalid, &out) == DAW_ACTION_INVALID, "nonfinite refusal");
    CHECK(engine_routing_set(state->engine, &changed, &next), "restore external edit");
    UndoCommand unrelated = {.type = UNDO_CMD_CLIP_RENAME}; CHECK(undo_manager_push(&state->undo, &unrelated), "unrelated history");
    CHECK(daw_action_routing_history(state, &next, false, &out) == DAW_ACTION_HISTORY_UNAVAILABLE && state->undo.undo_count == 2, "typed cursor cannot skip unrelated command");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); creation(); SDL_Quit();
    puts("routing_actions_test: success (exact routing, scoped history, owner/busy/stale/no-op, reservation/publication rollback, unrelated state preservation)");
    return 0;
}
