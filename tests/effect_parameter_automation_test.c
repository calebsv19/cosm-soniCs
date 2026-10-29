#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/fx_actions.h"
#include "daw/routing_actions.h"
#include "daw/mix_capture.h"
#include "ui/mixer_automation_panel.h"
#include "ui/font.h"
#include "ui/layout.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// Models actual latency independently of the compensation graph.
struct FxHandle {
    float ring[64];
    unsigned write, delay;
};
// Describes a real three-sample delay.
static int delay3(FxDesc* d) {
    *d = (FxDesc){.name = "delay3",
                  .api_version = FX_API_VERSION,
                  .flags = FX_FLAG_INPLACE_OK | FX_FLAG_HAS_LATENCY,
                  .num_inputs = 1,
                  .num_outputs = 1,
                  .latency_samples = 3};
    return 1;
}
// Describes a real five-sample delay.
static int delay5(FxDesc* d) {
    delay3(d);
    d->latency_samples = 5;
    return 1;
}
// Describes a real four-sample delay.
static int delay4(FxDesc* d) {
    delay3(d);
    d->latency_samples = 4;
    return 1;
}
// Delays interleaved PCM using an independent stereo ring index.
static void delay_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    for (int f = 0; f < frames; ++f) {
        unsigned read = (h->write + 32 - h->delay) % 32;
        for (int ch = 0; ch < channels; ++ch) {
            float v = in[f * channels + ch];
            out[f * channels + ch] = h->ring[read * 2 + ch];
            h->ring[h->write * 2 + ch] = v;
        }
        h->write = (h->write + 1) % 32;
    }
}
// Clears the fixture DSP's own delayed history.
static void delay_reset(FxHandle* h) {
    memset(h->ring, 0, sizeof(h->ring));
    h->write = 0;
}
// Frees one fixture-owned delay handle.
static void delay_free(FxHandle* h) {
    free(h);
}
// Creates the delay effect separately from the graph's latency calculations.
static int delay_create(const FxDesc* d, FxHandle** h, FxVTable* v, uint32_t rate, uint32_t block,
                        uint32_t channels) {
    (void)rate;
    (void)block;
    (void)channels;
    *h = calloc(1, sizeof(**h));
    if (!*h)
        return 0;
    (*h)->delay = d->latency_samples;
    *v = (FxVTable){.process = delay_process, .reset = delay_reset, .destroy = delay_free};
    return 1;
}

// Reads an exact stable track or master effect-chain guard.
static DawFxSnapshot capture(AppState* s, int track) {
    DawFxSnapshot out;
    assert(daw_action_get_fx(s, engine_instance_id(s->engine),
                             track < 0 ? 0 : engine_get_tracks(s->engine)[track].runtime_id,
                             &out) == DAW_ACTION_UNCHANGED);
    return out;
}
// Applies complete physical points as one reserved shared effect history step.
static void curve(AppState* s, int track, int slot, float first, float last) {
    DawFxSnapshot current = capture(s, track), out;
    EngineFxChainRequest request = {
        .op = ENGINE_FX_CHAIN_AUTOMATION,
        .id = current.chain.items[slot].id,
        .automation = {
            .count = 2, .before = 1, .after = 1, .points = {{0, first, 0}, {4096, last, 0}}}};
    assert(daw_action_edit_fx(s, &current, &request, &out) == DAW_ACTION_APPLIED);
}
// Uses the detached production graph without normalization or authored-state mutation.
static EngineBounceBuffer bounce(AppState* s) {
    EngineBounceOptions o = engine_bounce_options_default();
    o.normalize_if_clipping = false;
    EngineBounceBuffer b = {0};
    assert(engine_bounce_range_to_buffer_with_options(s->engine, 0, 8192, &o, NULL, NULL, &b));
    return b;
}
// Independently computes both channel gains for serial child/group/return/master curves.
static void expected(const EngineBounceBuffer* b) {
    for (int f = 0; f < 8192; ++f) {
        double x = f < 4096 ? (double)f / 4096 : 1;
        double child = pow(10, (-12 + 12 * x) / 20), room = pow(10, -6 * x / 20),
               master = pow(10, (-3 + 3 * x) / 20);
        for (int ch = 0; ch < 2; ++ch) {
            double pan = sqrt(ch ? x : 1 - x);
            double v = .25 * child * pan * (1 + .5 * room) * master;
            assert(fabs(b->data[f * 2 + ch] - v) < 2e-6);
        }
    }
}
// Sends matching pointer releases through the production point-editor geometry.
static void button(AppState* s, int index) {
    DawMixerAutomationLayout l;
    daw_mixer_automation_layout(s, &l);
    assert(l.editable);
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = l.buttons[index].x + 2;
    e.button.y = l.buttons[index].y + 2;
    assert(daw_mixer_automation_event(s, &e));
    e.type = SDL_MOUSEBUTTONUP;
    assert(daw_mixer_automation_event(s, &e));
}
// Qualifies exact physical DSP, persistence, native editing, guarded history and compensated serial
// timing.
int main(void) {
    assert(!SDL_Init(SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9));
    AppState* s = calloc(1, sizeof(*s));
    assert(s);
    config_set_defaults(&s->runtime_cfg);
    s->runtime_cfg.default_fade_in_ms = s->runtime_cfg.default_fade_out_ms = 0;
    s->engine = engine_create(&s->runtime_cfg);
    assert(s->engine);
    undo_manager_init(&s->undo);
    s->tempo = tempo_state_default(48000);
    tempo_map_init(&s->tempo_map, 48000);
    time_signature_map_init(&s->time_signature_map);
    input_manager_init(&s->input_manager);
    DAW_TEST_TEMP_PATH(dir, "sonics-fx-auto-XXXXXX");
    assert(mkdtemp(dir));
    char audio[512], project[512];
    snprintf(audio, sizeof(audio), "%s/input.wav", dir);
    snprintf(project, sizeof(project), "%s/project.json", dir);
    float pcm[8192 * 2];
    for (int i = 0; i < 8192 * 2; ++i)
        pcm[i] = .25;
    EngineBounceBuffer input = {pcm, 8192, 2, 48000};
    assert(engine_bounce_write_wav(&input, audio, ENGINE_BOUNCE_WAV_FLOAT32) &&
           engine_add_clip_to_track(s->engine, 0, audio, 0, NULL));
    EngineRoutingSnapshot* routing = calloc(1, sizeof(*routing));
    EngineRoutingSnapshot* next = calloc(1, sizeof(*next));
    assert(routing && next);
    assert(daw_action_get_routing(s, routing) == DAW_ACTION_UNCHANGED);
    assert(daw_action_create_group(s, routing, "Strings", next) == DAW_ACTION_APPLIED);
    *routing = *next;
    int group = next->track_count - 1;
    assert(daw_action_create_return(s, routing, next) == DAW_ACTION_APPLIED);
    *routing = *next;
    int room = next->track_count - 1;
    next->tracks[0].parent_id = next->tracks[group].track_id;
    next->tracks[group].sends[0] =
        (EngineSend){.return_id = next->tracks[room].track_id, .gain = .5};
    assert(daw_action_set_routing(s, routing, next, routing) == DAW_ACTION_APPLIED);
    assert(engine_fx_track_add(s->engine, 0, 1) && engine_fx_track_add(s->engine, group, 3) &&
           engine_fx_track_add(s->engine, room, 1) && engine_fx_master_add(s->engine, 1));
    DawMixSnapshot* guard = NULL;
    assert(daw_action_get_mix_snapshot(s, &guard) == DAW_ACTION_UNCHANGED);
    curve(s, 0, 0, -12, 0);
    curve(s, group, 0, -1, 1);
    curve(s, room, 0, 0, -6);
    curve(s, -1, 0, -3, 0);
    assert(!daw_mix_snapshot_current(s, guard));
    daw_mix_snapshot_free(guard);
    EngineBounceBuffer b = bounce(s);
    expected(&b);
    DawFxSnapshot master = capture(s, -1);
    assert(undo_manager_undo(&s->undo, s));
    assert(capture(s, -1).chain.items[0].automation.count == 0);
    assert(undo_manager_redo(&s->undo, s));
    DawFxSnapshot redo = capture(s, -1);
    assert(daw_fx_snapshot_equal(&master, &redo));
    assert(session_save_to_file(s, project) && session_load_from_file(s, project));
    EngineBounceBuffer loaded = bounce(s);
    assert(!memcmp(b.data, loaded.data, b.frame_count * b.channels * sizeof(float)));
    engine_bounce_buffer_free(&loaded);
    engine_bounce_buffer_free(&b);
    FxRegistryEntry registry[] = {
        {.id = 9911, .name = "d3", .get_desc = delay3, .create = delay_create},
        {.id = 9912, .name = "d5", .get_desc = delay5, .create = delay_create},
        {.id = 9913, .name = "d4", .get_desc = delay4, .create = delay_create}};
    assert(fxm_register_builtin(s->engine->fxm, registry, 3));
    int tracks[] = {0, group, room, -1};
    int types[] = {9911, 9912, 9912, 9913};
    for (int i = 0; i < 4; ++i) {
        int t = tracks[i];
        FxInstId id = t < 0 ? engine_fx_master_add(s->engine, types[i])
                            : engine_fx_track_add(s->engine, t, types[i]);
        assert(id);
        assert(t < 0 ? engine_fx_master_reorder(s->engine, id, 0)
                     : engine_fx_track_reorder(s->engine, t, id, 0));
    }
    b = bounce(s);
    expected(&b);
    engine_bounce_buffer_free(&b);
    master = capture(s, -1);
    EngineFxChainRequest bad = {.op = ENGINE_FX_CHAIN_AUTOMATION,
                                .id = master.chain.items[0].id,
                                .automation = {.count = 1, .points = {{0, 0, 0}}}};
    DawFxSnapshot out;
    assert(daw_action_edit_fx(s, &master, &bad, &out) == DAW_ACTION_INVALID);
    bad.id = master.chain.items[1].id;
    bad.automation.points[0].value = -97;
    assert(daw_action_edit_fx(s, &master, &bad, &out) == DAW_ACTION_INVALID);
    ui_init_panes(s);
    s->layout_runtime.mixer_ratio = .55;
    ui_layout_panes(s, 1600, 1000);
    s->effects_panel.target = FX_PANEL_TARGET_MASTER;
    s->effects_panel.selected_slot_index = 1;
    assert(daw_effect_automation_open(s));
    assert(s->routing_ui.effect_mode && s->routing_ui.auto_lane == 1);
    strcpy(s->routing_ui.auto_frame, "2048");
    strcpy(s->routing_ui.auto_value, "-2");
    int count = s->undo.undo_count;
    button(s, 4);
    assert(s->undo.undo_count == count + 1 && capture(s, -1).chain.items[1].automation.count == 3);
    button(s, 11);
    assert(capture(s, -1).chain.items[1].automation.count == 2);
    button(s, 12);
    assert(capture(s, -1).chain.items[1].automation.count == 3);
    button(s, 7);
    assert(!capture(s, -1).chain.items[1].automation.count);
    button(s, 11);
    assert(capture(s, -1).chain.items[1].automation.count == 3);
    button(s, 13);
    assert(!s->routing_ui.open);
    free(routing);
    free(next);
    undo_manager_clear(&s->undo);
    engine_destroy(s->engine);
    tempo_map_free(&s->tempo_map);
    time_signature_map_free(&s->time_signature_map);
    free(s);
    ui_font_shutdown();
    TTF_Quit();
    SDL_Quit();
    unlink(audio);
    unlink(project);
    rmdir(dir);
    puts("effect_parameter_automation_test: passed (exact signed physical curves on "
         "child/group/return/master, 17-frame PDC, capture guards, save/reopen, native edits and "
         "shared history)");
    return 0;
}
