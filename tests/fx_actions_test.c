#include "app_state.h"
#include "daw/fx_actions.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#define CHECK(v,m) daw_test_expect("fx_actions_test", (v), (m))
static bool fail_publish, fail_history;
static int publications;
// Injects failure at the actual prepared chain publication boundary.
static bool publish(Engine* engine) { ++publications; return !fail_publish && engine_request_rebuild_sources(engine); }
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_fx_chain.c"
#undef engine_request_rebuild_sources
// Injects reservation failure before any effect candidate can publish.
static void* reserve(void* p, size_t n) { return fail_history ? NULL : realloc(p, n); }
#define realloc reserve
#include "../src/undo/undo_manager_stack.c"
#undef realloc
// Reads a copied master or stable track chain from the private owning engine.
static DawFxSnapshot read_chain(AppState* s, uint64_t id) {
    DawFxSnapshot out;
    CHECK(daw_action_get_fx(s, engine_instance_id(s->engine), id, &out) == DAW_ACTION_UNCHANGED, "snapshot");
    return out;
}
// Exercises every edit and full identity-preserving history with independent lane chains.
int main(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0, "SDL");
    AppState* s = calloc(1, sizeof(*s));
    EngineRuntimeConfig c; config_set_defaults(&c); s->engine = engine_create(&c);
    CHECK(s->engine != NULL, "engine"); undo_manager_init(&s->undo);
    uint64_t track = engine_get_tracks(s->engine)[0].runtime_id;
    DawFxSnapshot empty = read_chain(s, track), out;
    EngineFxChainRequest request = {.op = ENGINE_FX_CHAIN_ADD, .type = 1};
    fail_history = true;
    CHECK(daw_action_edit_fx(s, &empty, &request, &out) == DAW_ACTION_HISTORY_UNAVAILABLE, "history refusal");
    fail_history = false; fail_publish = true;
    CHECK(daw_action_edit_fx(s, &empty, &request, &out) == DAW_ACTION_ENGINE_REJECTED, "publication refusal");
    fail_publish = false;
    out = read_chain(s, track); CHECK(daw_fx_snapshot_equal(&out, &empty) && !s->undo.undo_count, "rollback");
    int before = publications;
    CHECK(daw_action_edit_fx(s, &empty, &request, &out) == DAW_ACTION_APPLIED && publications == before + 1 && s->undo.undo_count == 1, "one add publication/history");
    FxInstId first = out.chain.items[0].id;
    CHECK(daw_action_edit_fx(s, &empty, &request, &out) == DAW_ACTION_STALE_TARGET, "stale chain");
    out = read_chain(s, track);
    DawFxSnapshot expected = out;
    request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_PARAM, .id = first, .parameter = 0, .value = NAN};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_INVALID, "nonfinite");
    request.value = -6;
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED, "physical gain");
    expected = out;
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_UNCHANGED && s->undo.undo_count == 2, "no-op");
    request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_ADD, .type = 90};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED, "reverb add");
    FxInstId second = out.chain.items[1].id; expected = out;
    request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_REORDER, .id = second, .position = 0};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED && out.chain.items[0].id == second, "reorder");
    expected = out; request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_ENABLE, .id = second, .enabled = false};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED, "bypass");
    expected = out; request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_REMOVE, .id = first};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED, "remove");
    DawFxSnapshot final = out;
    fail_publish = true; CHECK(!undo_manager_undo(&s->undo, s) && s->undo.undo_count == 6, "undo publication rollback"); fail_publish = false;
    for (int i = 0; i < 6; ++i) CHECK(undo_manager_undo(&s->undo, s), "undo chain");
    out = read_chain(s, track); CHECK(daw_fx_snapshot_equal(&empty, &out), "fully empty");
    for (int i = 0; i < 6; ++i) CHECK(undo_manager_redo(&s->undo, s), "redo chain");
    out = read_chain(s, track); CHECK(daw_fx_snapshot_equal(&final, &out), "exact IDs and targets");
    expected = read_chain(s, 0); request = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_ADD, .type = 21};
    CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_APPLIED && out.chain.count == 1, "master limiter");
    expected = read_chain(s, track); CHECK(daw_fx_snapshot_equal(&expected, &final), "master independent");
    s->bounce_active = true; CHECK(daw_action_edit_fx(s, &expected, &request, &out) == DAW_ACTION_BUSY, "busy refusal"); s->bounce_active = false;
    CHECK(engine_fx_track_set_enabled(s->engine, 0, second, true), "external edit");
    CHECK(!daw_fx_history_restore(s, s->undo.undo_stack[s->undo.undo_count - 2].data.fx_chain, false), "history conflict preserves user change");
    expected=read_chain(s,track);DawFxSnapshot source=read_chain(s,0);FxMasterSnapshot portable=source.chain;
    portable.items[0].id=expected.chain.items[0].id; // Portable identities must never be adopted.
    request=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_REPLACE,.restore=&portable};
    int history=s->undo.undo_count;before=publications;
    int capacity=s->undo.undo_capacity;s->undo.undo_capacity=s->undo.undo_count;
    fail_history=true;CHECK(daw_action_edit_fx(s,&expected,&request,&out)==DAW_ACTION_HISTORY_UNAVAILABLE,"portable history reservation");fail_history=false;s->undo.undo_capacity=capacity;
    fail_publish=true;CHECK(daw_action_edit_fx(s,&expected,&request,&out)==DAW_ACTION_ENGINE_REJECTED,"portable publication rollback");fail_publish=false;
    out=read_chain(s,track);CHECK(daw_fx_snapshot_equal(&expected,&out) && s->undo.undo_count==history,"portable failure unchanged");before=publications;
    CHECK(daw_action_edit_fx(s,&expected,&request,&out)==DAW_ACTION_APPLIED && s->undo.undo_count==history+1 && publications==before+1,"portable one-step replace");
    CHECK(out.chain.count==1 && out.chain.items[0].type==source.chain.items[0].type && out.chain.items[0].id!=source.chain.items[0].id && out.chain.items[0].id!=expected.chain.items[0].id,"fresh independent effect identity");
    DawFxSnapshot applied=out,master=read_chain(s,0);CHECK(daw_fx_snapshot_equal(&source,&master),"portable apply preserves source chain");
    CHECK(undo_manager_undo(&s->undo,s),"portable undo");out=read_chain(s,track);CHECK(daw_fx_snapshot_equal(&expected,&out),"portable undo restores original identity/settings");
    CHECK(undo_manager_redo(&s->undo,s),"portable redo");out=read_chain(s,track);CHECK(daw_fx_snapshot_equal(&applied,&out),"portable redo same new identity");
    history=s->undo.undo_count;CHECK(daw_action_edit_fx(s,&applied,&request,&out)==DAW_ACTION_UNCHANGED && s->undo.undo_count==history,"portable no-op compares sound without identity");
    portable.items[0].params[0]=NAN;CHECK(daw_action_edit_fx(s,&applied,&request,&out)==DAW_ACTION_INVALID,"portable invalid parameter");portable=source.chain;portable.items[0].type=UINT32_MAX;
    CHECK(daw_action_edit_fx(s,&applied,&request,&out)==DAW_ACTION_INVALID,"portable unknown effect");out=read_chain(s,track);CHECK(daw_fx_snapshot_equal(&applied,&out) && s->undo.undo_count==history,"invalid full chain retained");
    undo_manager_free(&s->undo); engine_destroy(s->engine); free(s); SDL_Quit();
    puts("fx_actions_test: success (track/master, six edits, exact history IDs, no-op, busy/stale, reservation/publication rollback)");
    return 0;
}
