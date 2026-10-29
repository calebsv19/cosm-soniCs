#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/routing.h"
#include "ui/live_meter_panel.h"
#include "ui/layout.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// Qualifies exact waveforms and callback partition invariance at all supported monitoring rates.
static void math_test(void) {
    for (int rate_index = 0; rate_index < 3; ++rate_index) {
        int rate = rate_index == 0 ? 44100 : rate_index == 1 ? 48000 : 96000, n = rate * 2 / 5;
        float* pcm = calloc(n * 2, sizeof(float));
        EngineLiveMeterState a = {0}, b = {0};
        for (int mode = 0; mode < 5; ++mode) {
            memset(&a, 0, sizeof(a));
            memset(&b, 0, sizeof(b));
            for (int f = 0; f < n; ++f) {
                float l = (f % 2 ? -.25 : .25), r = mode == 0   ? l
                                                    : mode == 1 ? -l
                                                    : mode == 2 ? (f % 4 < 2 ? .25 : -.25)
                                                    : mode == 3 ? 0
                                                                : 0;
                pcm[f * 2] = mode == 4 ? 0 : l;
                pcm[f * 2 + 1] = r;
            }
            engine_live_meter_feed(&a, pcm, n, 2, rate, 0, 7);
            for (int f = 0; f < n;) {
                int count = n - f < 127 ? n - f : 127;
                engine_live_meter_feed(&b, pcm + f * 2, count, 2, rate, f, 7);
                f += count;
            }
            assert(a.result.available && a.result.valid && a.result.end == n &&
                   a.result.latency == 7);
            assert(a.result.rms[0] == b.result.rms[0] && a.result.rms[1] == b.result.rms[1] &&
                   a.result.correlation == b.result.correlation &&
                   a.result.mono_ratio == b.result.mono_ratio);
            if (mode == 0)
                assert(a.result.correlation == 1 && a.result.mono_ratio == 1);
            if (mode == 1)
                assert(a.result.correlation == -1 && a.result.mono_ratio == 0 &&
                       a.result.side_rms == .25);
            if (mode == 2)
                assert(fabs(a.result.correlation) < 1e-12 &&
                       fabs(20 * log10(a.result.mono_ratio) + 3.01029995664) < 1e-9);
            if (mode == 3)
                assert(!a.result.correlation_available && a.result.balance == -1 &&
                       fabs(a.result.mono_ratio - sqrt(.5)) < 1e-12);
            if (mode == 4)
                assert(!a.result.correlation_available && !a.result.mono_available);
        }
        for (int f = 0; f < n * 2; ++f)
            pcm[f] = 1.25;
        memset(&a, 0, sizeof(a));
        engine_live_meter_feed(&a, pcm, n, 2, rate, 0, 0);
        assert(a.result.peak[0] == 1.25 && a.result.full_scale[0] == n &&
               a.result.overload[1] == n);
        pcm[0] = NAN;
        engine_live_meter_feed(&a, pcm, n, 2, rate, n, 0);
        assert(!a.result.valid);
        engine_live_meter_feed(&a, pcm, 1, 2, rate, 123, 0);
        assert(!a.result.available);
        free(pcm);
    }
}
// Provides a paused dummy endpoint so physical hardware and an active user app are unnecessary.
static void silence(float* pcm, int frames, int channels, void* user) {
    (void)user;
    memset(pcm, 0, frames * channels * sizeof(float));
}
// Reads real native meter rectangles and executes one matching or cancelled pointer release.
static void click(AppState* s, int index, bool matching) {
    DawLiveMeterLayout l;
    daw_live_meter_panel_layout(s, &l);
    assert(l.editable);
    SDL_Rect r = l.buttons[index];
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = r.x + r.w / 2;
    e.button.y = r.y + r.h / 2;
    assert(daw_live_meter_panel_event(s, &e));
    e.type = SDL_MOUSEBUTTONUP;
    if (!matching)
        e.button.x = -1;
    assert(daw_live_meter_panel_event(s, &e));
}
// Holds publication from a separate thread with a timeout so regression failures cannot deadlock.
typedef struct MeterLock {
    SDL_mutex* mutex;
    SDL_sem* ready;
    SDL_sem* release;
} MeterLock;
// Exercises the nonblocking publication branch while render completes a new window.
static int lock_meter(void* context) {
    MeterLock* l = context;
    SDL_LockMutex(l->mutex);
    SDL_SemPost(l->ready);
    SDL_SemWaitTimeout(l->release, 1000);
    SDL_UnlockMutex(l->mutex);
    return 0;
}
// Qualifies monitor retargeting and exact callback partition parity without physical output.
static void monitor_math(void) {
    AudioDevice a = {0}, b = {0};
    a.spec.sample_rate = b.spec.sample_rate = 48000;
    atomic_init(&a.monitor_mono, 1);
    atomic_init(&b.monitor_mono, 1);
    float x[1280], y[1280];
    for (int i = 0; i < 640; ++i) {
        x[i * 2] = y[i * 2] = .75;
        x[i * 2 + 1] = y[i * 2 + 1] = -.25;
    }
    audio_device_monitor_render(&a, x, 100, 2);
    for (int f = 0; f < 100;) {
        int n = 100 - f < 37 ? 100 - f : 37;
        audio_device_monitor_render(&b, y + f * 2, n, 2);
        f += n;
    }
    atomic_store(&a.monitor_mono, 0);
    atomic_store(&b.monitor_mono, 0);
    audio_device_monitor_render(&a, x + 200, 540, 2);
    for (int f = 100; f < 640;) {
        int n = 640 - f < 127 ? 640 - f : 127;
        audio_device_monitor_render(&b, y + f * 2, n, 2);
        f += n;
    }
    assert(!memcmp(x, y, sizeof(x)) && a.monitor_blend == 0 && b.monitor_blend == 0);
    for (int i = 220; i < 640; ++i)
        assert(x[i * 2] == .75 && x[i * 2 + 1] == -.25);
}
// Qualifies live production child/group/return/master telemetry and isolated native monitoring.
int main(void) {
    math_test();
    monitor_math();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    AppState* s = calloc(1, sizeof(*s));
    config_set_defaults(&s->runtime_cfg);
    s->runtime_cfg.default_fade_in_ms = s->runtime_cfg.default_fade_out_ms = 0;
    s->runtime_cfg.block_size = 128;
    s->engine = engine_create(&s->runtime_cfg);
    assert(s->engine);
    undo_manager_init(&s->undo);
    s->tempo = tempo_state_default(48000);
    tempo_map_init(&s->tempo_map, 48000);
    time_signature_map_init(&s->time_signature_map);
    input_manager_init(&s->input_manager);
    ui_init_panes(s);
    s->layout_runtime.mixer_ratio = .6;
    ui_layout_panes(s, 1600, 1000);
    DAW_TEST_TEMP_PATH(root, "sonics-live-fixture-XXXXXX");
    assert(mkdtemp(root));
    char path[512];
    snprintf(path, 512, "%s/source.wav", root);
    float* pcm = malloc(20480 * 2 * sizeof(float));
    for (int f = 0; f < 20480; ++f) {
        pcm[f * 2] = .25;
        pcm[f * 2 + 1] = -.25;
    }
    EngineBounceBuffer input = {pcm, 20480, 2, 48000};
    assert(engine_bounce_write_wav(&input, path, ENGINE_BOUNCE_WAV_FLOAT32));
    assert(engine_add_clip_to_track(s->engine, 0, path, 0, NULL));
    uint64_t group, returns;
    assert(engine_append_group(s->engine, 0, "Stereo group", &group));
    assert(engine_append_return(s->engine, 0, &returns));
    EngineRoutingSnapshot before, next;
    assert(engine_routing_capture(s->engine, &before));
    next = before;
    next.tracks[0].parent_id = group;
    next.tracks[1].sends[0] = (EngineSend){.return_id = returns, .gain = 1};
    assert(engine_routing_set(s->engine, &before, &next));
    assert(engine_track_set_gain(s->engine, 1, .5) && engine_track_set_gain(s->engine, 2, .25));
    engine_source_plan_apply(s->engine);
    float out[256], scratch[256];
    for (int f = 0; f < 20480; f += 128)
        engine_mix_tracks(s->engine, f, 128, out, scratch, 2);
    for (int t = -1; t < 3; ++t) {
        EngineMeterSnapshot m;
        assert(t < 0 ? engine_get_master_meter_snapshot(s->engine, &m)
                     : engine_get_track_meter_snapshot(s->engine, t, &m));
        double expected = t == 0 ? .25 : t == 1 ? .125 : t == 2 ? .03125 : .15625;
        assert(m.live.available && m.live.valid && m.live.correlation == -1 &&
               m.live.mono_ratio == 0 && fabs(m.live.rms[0] - expected) < 1e-6);
    }
    DawLiveMeterInfo info;
    assert(daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED && info.fresh &&
           !info.output_available);
    assert(daw_monitor_set(s, info.expected_monitor, true) == DAW_ACTION_ENGINE_REJECTED);
    AudioDeviceSpec spec = {48000, 128, 2};
    assert(audio_device_open(&s->engine->device, &spec, silence, NULL));
    assert(daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED);
    char old[128];
    strcpy(old, info.expected_monitor);
    int history = s->undo.undo_count;
    uint64_t head = engine_get_presentation_frame(s->engine);
    EngineBounceOptions options = engine_bounce_options_default();
    options.normalize_if_clipping = false;
    EngineBounceBuffer a = {0}, b = {0};
    assert(
        engine_bounce_range_to_buffer_with_options(s->engine, 0, 20480, &options, NULL, NULL, &a));
    assert(daw_live_meter_panel_open(s));
    SDL_Event outside = {.type = SDL_MOUSEBUTTONDOWN};
    outside.button.button = SDL_BUTTON_LEFT;
    outside.button.x = outside.button.y = 0;
    assert(!daw_live_meter_panel_event(s, &outside));
    outside.type = SDL_KEYDOWN;
    outside.key.keysym.sym = SDLK_SPACE;
    assert(!daw_live_meter_panel_event(s, &outside));
    click(s, 2, false);
    assert(!atomic_load(&s->engine->device.monitor_mono));
    click(s, 2, true);
    assert(atomic_load(&s->engine->device.monitor_mono) &&
           !strcmp(daw_live_meter_toolbar_label(s), "Mono Monitor"));
    assert(daw_monitor_set(s, old, false) == DAW_ACTION_STALE_TARGET);
    float monitor[640];
    for (int f = 0; f < 320; ++f) {
        monitor[f * 2] = .25;
        monitor[f * 2 + 1] = -.25;
    }
    audio_device_monitor_render(&s->engine->device, monitor, 320, 2);
    for (int f = 260; f < 320; ++f)
        assert(monitor[f * 2] == 0 && monitor[f * 2 + 1] == 0);
    assert(
        engine_bounce_range_to_buffer_with_options(s->engine, 0, 20480, &options, NULL, NULL, &b));
    assert(a.frame_count == b.frame_count &&
           !memcmp(a.data, b.data, a.frame_count * 2 * sizeof(float)));
    assert(s->undo.undo_count == history && engine_get_presentation_frame(s->engine) == head);
    click(s, 3, true);
    assert(!s->live_meter_ui.open && atomic_load(&s->engine->device.monitor_mono));
    assert(daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED && info.mono);
    assert(daw_monitor_set(s, info.expected_monitor, false) == DAW_ACTION_APPLIED);
    audio_device_monitor_render(&s->engine->device, monitor, 320, 2);
    assert(s->engine->device.monitor_blend == 0);
    // Contention drops publication rather than blocking render; old frames remain coherent.
    EngineMeterSnapshot cached;
    assert(engine_get_master_meter_snapshot(s->engine, &cached));
    MeterLock held = {s->engine->meter_mutex, SDL_CreateSemaphore(0), SDL_CreateSemaphore(0)};
    assert(held.ready && held.release);
    SDL_Thread* locker = SDL_CreateThread(lock_meter, "meter-lock", &held);
    assert(locker);
    SDL_SemWait(held.ready);
    Uint32 began = SDL_GetTicks();
    for (int frame = 20480; frame < 38400; frame += 128)
        engine_mix_tracks(s->engine, frame, 128, out, scratch, 2);
    assert(SDL_GetTicks() - began < 800);
    SDL_SemPost(held.release);
    SDL_WaitThread(locker, NULL);
    SDL_DestroySemaphore(held.ready);
    SDL_DestroySemaphore(held.release);
    EngineMeterSnapshot retained;
    assert(engine_get_master_meter_snapshot(s->engine, &retained) &&
           retained.live.end == cached.live.end);
    engine_mix_tracks(s->engine, 38400, 128, out, scratch, 2);
    assert(engine_get_master_meter_snapshot(s->engine, &retained) && retained.live.end == 38400);
    char project[512];
    snprintf(project, 512, "%s/project.json", root);
    assert(session_save_to_file(s, project));
    assert(daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED &&
           daw_monitor_set(s, info.expected_monitor, true) == DAW_ACTION_APPLIED);
    assert(session_load_from_file(s, project));
    assert(daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED && !info.mono &&
           !info.meter.available);
    engine_bounce_buffer_free(&a);
    engine_bounce_buffer_free(&b);
    free(pcm);
    engine_destroy(s->engine);
    tempo_map_free(&s->tempo_map);
    time_signature_map_free(&s->time_signature_map);
    free(s);
    printf("live_stereo_test: passed (phase/mono/energy calibration at 44.1/48/96 kHz, partition "
           "parity, live child/group/return/master, exact guards, native controls, export/history "
           "preservation and contention); fixture=%s\n",
           root);
    return 0;
}
