#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/routing_actions.h"
#include "daw/mix_capture.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/mixer_automation_panel.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Captures current exact guards while keeping the large bounded snapshot on the heap.
static EngineRoutingSnapshot* snapshot(AppState* s) {
    EngineRoutingSnapshot* out = calloc(1, sizeof(*out));
    assert(out && daw_action_get_routing(s, out) == DAW_ACTION_UNCHANGED);
    return out;
}
// Applies one copied routing candidate and updates the caller's accepted guard.
static void apply(AppState* s, EngineRoutingSnapshot* current, EngineRoutingSnapshot* next) {
    EngineRoutingSnapshot* out = calloc(1, sizeof(*out));
    assert(out && daw_action_set_routing(s, current, next, out) == DAW_ACTION_APPLIED);
    *current = *out;
    free(out);
}
// Supplies prepared bounded linear/hold/smooth controls on original project frames.
static EngineMixerAutomation curve(float first, float last, int shape) {
    EngineMixerAutomation a = {.count = 2,
                               .before = ENGINE_AUTOMATION_BOUNDARY_HOLD,
                               .after = ENGINE_AUTOMATION_BOUNDARY_HOLD};
    a.points[0] = (EngineAutomationPoint){0, first, shape};
    a.points[1] = (EngineAutomationPoint){4096, last, 0};
    return a;
}
// Renders the complete graph and compares every sample with an independent mathematical gain model.
static EngineBounceBuffer render(AppState* s) {
    EngineBounceOptions o = engine_bounce_options_default();
    o.normalize_if_clipping = false;
    EngineBounceBuffer b = {0};
    assert(engine_bounce_range_to_buffer_with_options(s->engine, 0, 8192, &o, NULL, NULL, &b));
    return b;
}
// Verifies linear gain multiplication, independently of engine curve evaluation.
static void expected(const EngineBounceBuffer* b, bool automated, bool muted) {
    for (int f = 0; f < 8192; ++f) {
        double x = f < 4096 ? (double)f / 4096 : 1;
        double v = muted ? 0 : automated ? .25 * x * (1 + .5 * x) : .25 * 1.2;
        for (int ch = 0; ch < 2; ++ch)
            assert(fabs(b->data[f * 2 + ch] - v) < 1e-6);
    }
}
// Clicks production editor geometry using matching press/release rather than direct model writes.
static void button(AppState* s, int index) {
    DawMixerAutomationLayout l;
    daw_mixer_automation_layout(s, &l);
    assert(l.editable);
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = l.buttons[index].x + 2;
    event.button.y = l.buttons[index].y + 2;
    assert(daw_mixer_automation_event(s, &event));
    event.type = SDL_MOUSEBUTTONUP;
    assert(daw_mixer_automation_event(s, &event));
}
// Qualifies production routing, exact history, persistence, native editing, guard/refusal and
// capture provenance.
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
    ui_init_panes(s);
    ui_layout_panes(s, 1600, 1000);
    s->timeline_visible_seconds = 10;
    s->timeline_vertical_scale = 1;
    DAW_TEST_TEMP_PATH(directory, "sonics-mixer-auto-XXXXXX");
    assert(mkdtemp(directory));
    char audio[512], project[512];
    snprintf(audio, sizeof(audio), "%s/input.wav", directory);
    snprintf(project, sizeof(project), "%s/project.json", directory);
    float pcm[8192 * 2];
    for (int i = 0; i < 8192 * 2; ++i)
        pcm[i] = .25f;
    EngineBounceBuffer input = {pcm, 8192, 2, 48000};
    assert(engine_bounce_write_wav(&input, audio, ENGINE_BOUNCE_WAV_FLOAT32) &&
           engine_add_clip_to_track(s->engine, 0, audio, 0, NULL));
    EngineRoutingSnapshot* current = snapshot(s);
    EngineRoutingSnapshot* out = calloc(1, sizeof(*out));
    assert(daw_action_create_group(s, current, "Strings", out) == DAW_ACTION_APPLIED);
    *current = *out;
    int group = out->track_count - 1;
    assert(daw_action_create_return(s, current, out) == DAW_ACTION_APPLIED);
    *current = *out;
    int room = out->track_count - 1;
    EngineRoutingSnapshot* next = malloc(sizeof(*next));
    *next = *current;
    next->tracks[0].parent_id = next->tracks[group].track_id;
    next->tracks[group].sends[0] =
        (EngineSend){.return_id = next->tracks[room].track_id, .gain = .2f};
    apply(s, current, next);
    EngineBounceBuffer b = render(s);
    expected(&b, false, false);
    engine_bounce_buffer_free(&b);
    int before = s->undo.undo_count;
    DawMixSnapshot* guard = NULL;
    assert(daw_action_get_mix_snapshot(s, &guard) == DAW_ACTION_UNCHANGED);
    *next = *current;
    next->tracks[group].volume_automation = curve(0, 1, 0);
    next->tracks[group].sends[0].automation = curve(0, .5, 0);
    apply(s, current, next);
    assert(s->undo.undo_count == before + 1 && !daw_mix_snapshot_current(s, guard));
    daw_mix_snapshot_free(guard);
    b = render(s);
    expected(&b, true, false);
    assert(daw_action_routing_history(s, current, false, out) == DAW_ACTION_APPLIED);
    *current = *out;
    EngineBounceBuffer plain = render(s);
    expected(&plain, false, false);
    engine_bounce_buffer_free(&plain);
    assert(daw_action_routing_history(s, current, true, out) == DAW_ACTION_APPLIED);
    *current = *out;
    assert(session_save_to_file(s, project) && session_load_from_file(s, project));
    free(current);
    current = snapshot(s);
    EngineBounceBuffer loaded = render(s);
    assert(b.frame_count == loaded.frame_count &&
           !memcmp(b.data, loaded.data, b.frame_count * b.channels * sizeof(float)));
    engine_bounce_buffer_free(&loaded);
    engine_bounce_buffer_free(&b);
    assert(engine_track_set_gain(s->engine, group, 0));
    b = render(s);
    expected(&b, true, false);
    engine_bounce_buffer_free(&b);
    assert(engine_track_set_muted(s->engine, group, true));
    b = render(s);
    expected(&b, true, true);
    engine_bounce_buffer_free(&b);
    assert(engine_track_set_muted(s->engine, group, false));
    *next = *current;
    next->tracks[group].volume_automation.points[1].frame = 0;
    assert(daw_action_set_routing(s, current, next, out) == DAW_ACTION_INVALID);
    *next = *current;
    next->tracks[0].volume_automation = curve(0, 1, 0);
    assert(daw_action_set_routing(s, current, next, out) == DAW_ACTION_INVALID);
    *next = *current;
    next->tracks[group].sends[0].return_id = 0;
    assert(daw_action_set_routing(s, current, next, out) == DAW_ACTION_INVALID);
    EngineMixerAutomation a = curve(0, 1, ENGINE_AUTOMATION_CURVE_HOLD);
    assert(engine_mixer_automation_eval(&a, 2048, .3f) == 0 &&
           engine_mixer_automation_eval(&a, 4096, .3f) == 1);
    a.points[0].curve = ENGINE_AUTOMATION_CURVE_SMOOTH;
    assert(fabs(engine_mixer_automation_eval(&a, 1024, .3f) - .15625) < 1e-7);
    a.before = a.after = ENGINE_AUTOMATION_BOUNDARY_BASELINE;
    assert(engine_mixer_automation_eval(&a, 8192, .3f) == .3f);
    s->layout_runtime.mixer_ratio = .55f;
    ui_layout_panes(s, 1600, 1000);
    s->routing_ui.open = true;
    s->routing_ui.selected = group;
    s->routing_ui.captured = *current;
    daw_mixer_automation_open(s);
    assert(s->routing_ui.automation_mode && s->routing_ui.auto_lane == 0);
    strcpy(s->routing_ui.auto_frame, "2048");
    strcpy(s->routing_ui.auto_value, "0.25");
    before = s->undo.undo_count;
    button(s, 4);
    assert(s->undo.undo_count == before + 1 &&
           engine_get_tracks(s->engine)[group].volume_automation.count == 3);
    button(s, 8);
    assert(engine_get_tracks(s->engine)[group].volume_automation.points[1].curve ==
           ENGINE_AUTOMATION_CURVE_HOLD);
    button(s, 11);
    assert(engine_get_tracks(s->engine)[group].volume_automation.points[1].curve ==
           ENGINE_AUTOMATION_CURVE_LINEAR);
    button(s, 12);
    assert(engine_get_tracks(s->engine)[group].volume_automation.points[1].curve ==
           ENGINE_AUTOMATION_CURVE_HOLD);
    before = s->undo.undo_count;
    strcpy(s->routing_ui.auto_frame, "00");
    button(s, 4);
    assert(s->undo.undo_count == before);
    ui_layout_panes(s, 280, 160);
    DawMixerAutomationLayout layout;
    daw_mixer_automation_layout(s, &layout);
    assert(!layout.editable);
    daw_routing_panel_release(s);
    free(current);
    free(next);
    free(out);
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
    rmdir(directory);
    puts(
        "mixer_automation_test: passed (analytic production curves, group/send factor, mute and "
        "static-zero safety, exact save/reopen, shared history, native edits, frame/range guards)");
    return 0;
}
