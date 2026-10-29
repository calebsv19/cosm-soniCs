#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/ab_comparison.h"
#include "ui/ab_panel.h"
#include "ui/sound_panel.h"
#include "ui/font.h"
#include "ui/layout.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
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
// Uses a paused dummy output so only explicit headless callback steps consume comparison frames.
static void silence(float* out, int frames, int channels, void* user) {
    (void)user;
    memset(out, 0, (size_t)frames * channels * sizeof(float));
}
// Captures exact production configuration independently of the comparison cache.
static DawMixSnapshot* guard(AppState* s) {
    DawMixSnapshot* p = NULL;
    assert(daw_action_get_mix_snapshot(s, &p) == DAW_ACTION_UNCHANGED);
    return p;
}
// Activates fully visible production native controls through matching pointer releases.
static void button(AppState* s, int index) {
    DawAbLayout l;
    daw_ab_panel_layout(s, &l);
    assert(l.editable);
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = l.buttons[index].x + 2;
    e.button.y = l.buttons[index].y + 2;
    assert(daw_ab_panel_event(s, &e));
    e.type = SDL_MOUSEBUTTONUP;
    assert(daw_ab_panel_event(s, &e));
}
// Tests calibrated RMS/silence/headroom, immutable capture, native/MCP-shared actions and lifecycle
// preservation.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_AUDIO) && !TTF_Init());
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
    s->layout_runtime.mixer_ratio = .55;
    ui_layout_panes(s, 1600, 1000);
    float samples[24000 * 2];
    for (int f = 0; f < 24000; ++f) {
        samples[f * 2] = .25;
        samples[f * 2 + 1] = -.25;
    }
    char dir[] = "/tmp/sonics-ab-fixture-XXXXXX";
    assert(mkdtemp(dir));
    char audio[512], project[512];
    snprintf(audio, sizeof(audio), "%s/input.wav", dir);
    snprintf(project, sizeof(project), "%s/project.json", dir);
    EngineBounceBuffer input = {samples, 24000, 2, 48000};
    assert(engine_bounce_write_wav(&input, audio, ENGINE_BOUNCE_WAV_FLOAT32) &&
           engine_add_clip_to_track(s->engine, 0, audio, 0, NULL));
    DawCompositionRequest request = {.start = 0, .end = 24000};
    DawMixSnapshot* before = guard(s);
    int undo = s->undo.undo_count;
    assert(daw_ab_capture(s, 0, before, 0, &request) == DAW_ACTION_APPLIED);
    float* immutable = s->ab->captures[0].audio.data;
    assert(s->ab_revision == 1 && immutable[0] == .25 && s->undo.undo_count == undo);
    FxRegistryEntry registry = {
        .id = 9925, .name = "comparison-delay5", .get_desc = delay5, .create = delay_create};
    assert(fxm_register_builtin(s->engine->fxm, &registry, 1));
    FxInstId delay_id = engine_fx_master_add(s->engine, 9925);
    assert(delay_id);
    FxInstId gain = engine_fx_master_add(s->engine, 1);
    assert(gain && engine_fx_master_set_param(s->engine, gain, 0, -6.020599913f));
    assert(daw_ab_capture(s, 1, before, 1, &request) == DAW_ACTION_STALE_TARGET);
    daw_mix_snapshot_free(before);
    DawMixSnapshot* now = guard(s);
    assert(daw_ab_capture(s, 1, now, 1, &request) == DAW_ACTION_APPLIED);
    assert(s->ab->ready && s->ab->match_available && s->ab->captures[0].audio.data == immutable &&
           immutable[0] == .25);
    assert(fabs(s->ab->gains[0] - .5) < 1e-6 && s->ab->gains[1] == 1);
    DawCompositionRequest wrong = request;
    wrong.end = 24001;
    assert(daw_ab_capture(s, 2, now, 1, &wrong) == DAW_ACTION_INVALID && s->ab_revision == 2);
    assert(daw_ab_listen(s, 1, 0, true, false) == DAW_ACTION_STALE_TARGET);
    AudioDeviceSpec spec = {48000, 128, 2};
    assert(audio_device_open(&s->engine->device, &spec, silence, NULL));
    uint64_t authored = engine_get_presentation_frame(s->engine);
    assert(daw_ab_listen(s, 2, 0, true, false) == DAW_ACTION_APPLIED);
    float output[512];
    audio_device_preview_render(&s->engine->device, output, 128, 2);
    for (int f = 0; f < 128; ++f)
        assert(fabs(output[f * 2] - .125) < 1e-6 && fabs(output[f * 2 + 1] + .125) < 1e-6);
    uint64_t position;
    assert(engine_comparison_status(s->engine, 2, &position) && position == 128);
    assert(daw_ab_listen(s, 2, 1, true, false) == DAW_ACTION_APPLIED);
    audio_device_preview_render(&s->engine->device, output, 256, 2);
    assert(engine_comparison_status(s->engine, 2, &position) && position == 384);
    assert(engine_get_presentation_frame(s->engine) == authored && s->undo.undo_count == undo &&
           daw_mix_snapshot_current(s, now));
    s->sound_ui.listening = 14;
    daw_sound_panel_release(s);
    assert(engine_comparison_status(s->engine, 2, &position));
    assert(daw_ab_stop(s, 2) == DAW_ACTION_APPLIED);
    assert(engine_preview_play(s->engine, samples, 24000, 2, 48000));
    assert(daw_ab_stop(s, 2) == DAW_ACTION_UNCHANGED && engine_preview_active(s->engine));
    engine_preview_stop(s->engine);
    assert(daw_ab_panel_open(s));
    strcpy(s->ab_ui.fields[0], "0");
    strcpy(s->ab_ui.fields[1], "24000");
    strcpy(s->ab_ui.fields[2], "0");
    button(s, 2);
    button(s, 3);
    assert(s->ab->selection == 1);
    button(s, 6);
    assert(s->ab->selection == 3);
    button(s, 4);
    button(s, 11);
    assert(!s->ab_ui.open);
    assert(daw_ab_panel_open(s) && !strcmp(s->ab_ui.fields[1], "24000"));
    button(s, 11);
    double matched[4];
    bool available;
    float a[] = {2, -2}, b[] = {1, -1};
    EngineBounceBuffer x = {a, 1, 2, 48000}, y = {b, 1, 2, 48000};
    assert(daw_ab_gains(&x, &y, matched, &available) && available &&
           fabs(matched[0] - .475) < 1e-12 && fabs(matched[1] - .95) < 1e-12 &&
           matched[2] == matched[3]);
    a[0] = a[1] = 0;
    assert(daw_ab_gains(&x, &y, matched, &available) && !available);
    a[0] = NAN;
    assert(!daw_ab_gains(&x, &y, matched, &available));
    assert(engine_fx_master_remove(s->engine, delay_id));
    assert(session_save_to_file(s, project) && session_load_from_file(s, project));
    assert(!s->ab && !s->ab_ui.open);
    daw_mix_snapshot_free(now);
    daw_ab_release(s);
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
    puts("ab_comparison_test: passed (anti-phase stereo RMS, attenuation/headroom, immutable "
         "production captures, exact guards, native switching/field retention, dummy callback "
         "alignment, separate audition ownership, authored/history preservation and session "
         "retirement)");
    return 0;
}
