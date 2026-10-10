#include "app_state.h"
#include "daw/composition.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("composition_test", (v), (m))
// Checks processed signal isolation, exact dry comparison, effect tails and unchanged authoring state.
int main(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0, "SDL");
    AppState* s = calloc(1, sizeof(*s)); EngineRuntimeConfig c; config_set_defaults(&c);
    s->engine = engine_create(&c); CHECK(s->engine != NULL, "engine"); undo_manager_init(&s->undo);
    s->tempo = tempo_state_default(c.sample_rate); tempo_map_init(&s->tempo_map, c.sample_rate);
    int clip = -1; CHECK(engine_add_midi_clip_to_track(s->engine, 0, 0, 48000, &clip), "region");
    CHECK(engine_clip_midi_add_note(s->engine, 0, clip, (EngineMidiNote){0, 24000, 60, .5f}, NULL), "note");
    DawActionTarget target; CHECK(daw_instrument_target_capture(s, 0, clip, &target) == DAW_ACTION_UNCHANGED, "identity");
    DawCompositionRequest request = {.has_target = true, .target = target, .start = 0, .end = 48000, .tail = 24000};
    DawCompositionRender dry = {0}, wet = {0}, repeated = {0};
    request.dry = true; CHECK(daw_action_render_composition(s, &request, &dry) == DAW_ACTION_UNCHANGED && dry.analysis.rms > 0, "dry");
    CHECK(dry.audio.frame_count == 72000 && dry.audio.channels == 2, "range and tail");
    DawFxSnapshot chain, after;
    CHECK(daw_action_get_fx(s, target.project_generation, target.track_id, &chain) == DAW_ACTION_UNCHANGED, "chain");
    EngineFxChainRequest edit = {.op = ENGINE_FX_CHAIN_ADD, .type = 1};
    CHECK(daw_action_edit_fx(s, &chain, &edit, &after) == DAW_ACTION_APPLIED, "gain"); chain = after;
    edit = (EngineFxChainRequest){.op = ENGINE_FX_CHAIN_PARAM, .id = chain.chain.items[0].id, .parameter = 0, .value = -6};
    CHECK(daw_action_edit_fx(s, &chain, &edit, &after) == DAW_ACTION_APPLIED, "gain target");
    int history = s->undo.undo_count; request.dry = false;
    CHECK(daw_action_render_composition(s, &request, &wet) == DAW_ACTION_UNCHANGED, "processed");
    double ratio = wet.analysis.rms / dry.analysis.rms; CHECK(ratio > .49 && ratio < .52, "physical six-dB reduction");
    CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_UNCHANGED &&
          !memcmp(wet.audio.data, repeated.audio.data, wet.audio.frame_count * wet.audio.channels * sizeof(float)), "repeatable actual DSP");
    daw_composition_render_free(&repeated); request.dry = true;
    CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_UNCHANGED &&
          !memcmp(dry.audio.data, repeated.audio.data, dry.audio.frame_count * dry.audio.channels * sizeof(float)), "dry private bypass");
    daw_composition_render_free(&repeated);
    DawFxSnapshot current; CHECK(daw_action_get_fx(s, target.project_generation, target.track_id, &current) == DAW_ACTION_UNCHANGED &&
          daw_fx_snapshot_equal(&current, &after) && s->undo.undo_count == history, "analysis read-only chain/history");
    int other = -1; CHECK(engine_add_midi_clip_to_track(s->engine, 0, 24000, 48000, &other), "overlapping source");
    CHECK(engine_clip_midi_add_note(s->engine, 0, other, (EngineMidiNote){0, 24000, 72, .6f}, NULL), "other note");
    CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_UNCHANGED &&
          !memcmp(dry.audio.data, repeated.audio.data, dry.audio.frame_count * dry.audio.channels * sizeof(float)), "region excludes another source on same lane");
    daw_composition_render_free(&repeated); request.target.clip_id = 0; request.target.kind = DAW_ACTION_TARGET_TRACK;
    CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_UNCHANGED &&
          memcmp(dry.audio.data, repeated.audio.data, dry.audio.frame_count * dry.audio.channels * sizeof(float)), "track includes both regions");
    daw_composition_render_free(&repeated);
    for (int p = 0; p < DAW_COMPOSITION_BINS; ++p) CHECK(isfinite(wet.analysis.spectrum_db[p]) && wet.analysis.frequency_hz[p] >= 20, "finite calibrated spectrum");
    request.end = 48000 * 11; CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_INVALID, "bounded range");
    request.end = 48000; request.window = UINT64_MAX; CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_INVALID, "invalid window");
    request.window = 0; s->bounce_active = true; CHECK(daw_action_render_composition(s, &request, &repeated) == DAW_ACTION_BUSY, "busy"); s->bounce_active = false;
    daw_composition_render_free(&dry); daw_composition_render_free(&wet);
    undo_manager_free(&s->undo); tempo_map_free(&s->tempo_map); engine_destroy(s->engine); free(s); SDL_Quit();
    puts("composition_test: success (production dry/wet gain, exact repeatability, isolated region/track, original level, calibrated metrics, read-only and bounds)");
    return 0;
}
