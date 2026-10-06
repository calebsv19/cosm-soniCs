#define _POSIX_C_SOURCE 200809L
#include "audio/wav_writer.h"
#include "effects/effects_manager.h"
#include "effects/sample_ramp.h"
#include "engine/engine_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int gain_get_desc(FxDesc*);
int gain_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);

// Processes a constant stereo fixture in variable partitions without changing parameter timing.
static void render(FxHandle* handle, FxVTable* vt, float* out, int frames, int partition) {
    for (int offset = 0; offset < frames;) {
        int count = frames - offset < partition ? frames - offset : partition;
        float input[2048];
        for (int n = 0; n < count; ++n) {
            input[n * 2] = 1;
            input[n * 2 + 1] = .5f;
        }
        vt->process(handle, input, out + offset * 2, count, 2);
        offset += count;
    }
}

// Verifies exact sample duration, linked channels, retargeting, rejected values, and reset behavior.
static void gain_transitions(unsigned rate) {
    FxDesc desc = {0};
    FxHandle *a = NULL, *b = NULL;
    FxVTable va = {0}, vb = {0};
    assert(gain_get_desc(&desc));
    assert(gain_create(&desc, &a, &va, rate, 1024, 2));
    assert(gain_create(&desc, &b, &vb, rate, 1024, 2));
    float one[4096], split[4096];
    va.set_param(a, 0, -6);
    vb.set_param(b, 0, -6);
    render(a, &va, one, 1, 1);
    render(b, &vb, split, 1, 1);
    float start = powf(10, -.3f);
    assert(fabsf(one[0] - start) < 1e-7f);
    va.set_param(a, 0, 0);
    vb.set_param(b, 0, 0);
    int samples = (int)round(rate * .020);
    render(a, &va, one, samples, 1024);
    render(b, &vb, split, samples, 17);
    for (int n = 0; n < samples; ++n) {
        assert(one[n * 2] == split[n * 2]);
        assert(one[n * 2 + 1] == one[n * 2] * .5f);
        assert(fabsf(one[n * 2] - (start + (1 - start) * (n + 1) / samples)) < 4e-5f);
    }
    assert(one[(samples - 1) * 2] == 1);
    va.set_param(a, 0, -20);
    render(a, &va, one, samples / 2, 13);
    float current = one[(samples / 2 - 1) * 2];
    va.set_param(a, 0, 0);
    va.set_param(a, 0, NAN);
    render(a, &va, one, 1, 1);
    assert(fabsf(one[0] - (current + (1 - current) / samples)) < 1e-6f);
    va.set_param(a, 0, -20);
    va.reset(a);
    render(a, &va, one, 1, 1);
    assert(fabsf(one[0] - .1f) < 1e-7f);
    va.destroy(a);
    vb.destroy(b);
}

// Checks exact silence at a ramp endpoint, including a mid-ramp repeated target.
static void zero_endpoint(void) {
    FxSampleRamp ramp;
    fx_sample_ramp_reset(&ramp, 1);
    fx_sample_ramp_target(&ramp, 0, 240);
    for (int i = 0; i < 240; ++i) {
        if (i == 100)
            fx_sample_ramp_target(&ramp, 0, 240);
        float value = fx_sample_ramp_next(&ramp);
        assert(value >= 0 && value <= 1);
    }
    assert(ramp.current == 0 && ramp.remaining == 0);
}

// Proves render revision handoff preserves the audible ramp while a new target takes effect.
static void manager_handoff(void) {
    FxConfig config = {.sample_rate = 48000, .max_block = 1024, .max_channels = 2};
    EffectsManager* control = fxm_create(&config);
    EffectParamSpec spec = {
        .type = FX_PARAM_TYPE_FLOAT, .min_value = -96, .max_value = 24, .smoothing_ms = 20};
    FxRegistryEntry entry = {.id = 1,
                             .name = "Gain",
                             .get_desc = gain_get_desc,
                             .create = gain_create,
                             .param_specs = &spec,
                             .param_spec_count = 1};
    assert(control && fxm_register_builtin(control, &entry, 1));
    FxInstId id = fxm_master_add(control, 1);
    assert(id);
    EffectsManager* active = fxm_clone_for_render(control);
    assert(active);
    float audio[2048];
    for (int i = 0; i < 2048; ++i)
        audio[i] = 1;
    fxm_render_master(active, audio, 1, 2);
    assert(fxm_master_set_param(control, id, 0, -20));
    EffectsManager* next = fxm_clone_for_render(control);
    assert(next);
    assert(fxm_transfer_render_state(next, active, NULL, 0));
    fxm_destroy(active);
    active = next;
    fxm_begin_render_block(active, 960);
    fxm_render_master(active, audio, 960, 2);
    assert(audio[0] < 1 && audio[0] > .99f && fabsf(audio[1918] - .1f) < 1e-7f);
    fxm_destroy(active);
    fxm_destroy(control);
}

// Verifies a zero-latency effect fades to dry and back without steps or misaligned delay blending.
static void bypass_transition(void) {
    FxConfig config = {.sample_rate = 48000, .max_block = 1024, .max_channels = 2};
    EffectsManager* fm = fxm_create(&config);
    FxRegistryEntry entry = {.id = 1, .name = "Gain", .get_desc = gain_get_desc, .create = gain_create};
    assert(fm && fxm_register_builtin(fm, &entry, 1));
    FxInstId id = fxm_master_add(fm, 1);
    assert(id && fxm_master_set_param(fm, id, 0, -20));
    float audio[480];
    for (int i = 0; i < 480; ++i)
        audio[i] = 1;
    fxm_render_master(fm, audio, 1, 2);
    assert(fabsf(audio[0] - .1f) < 1e-7f);
    assert(fxm_master_set_enabled(fm, id, false));
    for (int i = 0; i < 480; ++i)
        audio[i] = 1;
    fxm_render_master(fm, audio, 240, 2);
    for (int n = 0; n < 240; ++n)
        assert(fabsf(audio[n * 2] - (.1f + .9f * (n + 1) / 240)) < 4e-6f);
    assert(audio[478] == 1);
    assert(fxm_master_set_enabled(fm, id, true));
    for (int i = 0; i < 480; ++i)
        audio[i] = 1;
    fxm_render_master(fm, audio, 240, 2);
    assert(audio[0] > .99f && fabsf(audio[478] - .1f) < 1e-7f);
    assert(fxm_master_set_enabled(fm, id, false));
    for (int i = 0; i < 480; ++i)
        audio[i] = 1;
    fxm_render_master(fm, audio, 120, 2);
    assert(fabsf(audio[238] - .55f) < 4e-6f);
    EffectsManager* next = fxm_clone_for_render(fm);
    assert(next && fxm_transfer_render_state(next, fm, NULL, 0));
    fxm_destroy(fm);
    for (int i = 0; i < 480; ++i)
        audio[i] = 1;
    fxm_render_master(next, audio, 120, 2);
    assert(audio[0] > .55f && audio[0] < .56f && audio[238] == 1);
    fxm_destroy(next);
}

// Exercises deterministic worker-boundary adoption separately from the live-thread stress suite.
static void mixer_transitions(void) {
    char path[] = "/tmp/daw-transitions-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    float source[2048];
    for (int i = 0; i < 2048; ++i)
        source[i] = .25f;
    assert(wav_write_f32(path, source, 1024, 2, 48000));
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 256;
    Engine* engine = engine_create(&config);
    assert(engine && engine_add_clip_to_track(engine, 0, path, 0, NULL));
    float out[512], scratch[512];
    engine_mix_tracks(engine, 0, 16, out, scratch, 2);
    assert(out[0] == .25f);
    atomic_store(&engine->worker_running,
                 true); // No thread runs; exercise the real worker adoption branch deterministically.
    assert(engine_track_set_gain(engine, 0, 0));
    engine_mix_tracks(engine, 16, 240, out, scratch, 2);
    for (int n = 0; n < 240; ++n)
        assert(fabsf(out[n * 2] - .25f * (1 - (n + 1) / 240.0f)) < 4e-6f);
    assert(out[478] == 0);
    atomic_store(&engine->worker_running, false);
    assert(engine_track_set_gain(engine, 0, 1));
    engine_mix_tracks(engine, 0, 16, out, scratch, 2);
    assert(out[0] == .25f);
    atomic_store(&engine->worker_running, true);
    assert(engine_track_set_muted(engine, 0, true));
    engine_mix_tracks(engine, 16, 240, out, scratch, 2);
    assert(out[0] > .24f && out[478] == 0);
    assert(engine_track_set_muted(engine, 0, false));
    engine_mix_tracks(engine, 256, 240, out, scratch, 2);
    assert(out[0] > 0 && out[0] < .002f && out[478] == .25f);
    assert(engine_track_set_pan(engine, 0, 1));
    engine_mix_tracks(engine, 496, 240, out, scratch, 2);
    assert(out[0] > .24f && out[478] == 0 && out[479] == .25f);
    atomic_store(&engine->worker_running, false);
    assert(engine_track_set_pan(engine, 0, 0));
    assert(engine_add_track(engine) == 1 && engine_add_clip_to_track(engine, 1, path, 0, NULL));
    atomic_store(&engine->worker_running, true);
    assert(engine_track_set_solo(engine, 1, true));
    engine_mix_tracks(engine, 0, 240, out, scratch, 2);
    assert(out[0] > .49f && out[478] == .25f);
    assert(engine_transport_seek(engine, 0));
    engine_mix_tracks(engine, 0, 16, out, scratch, 2);
    assert(out[0] == .25f); // Explicit reset lands at authored targets without a new ramp.
    atomic_store(&engine->worker_running, false);
    engine_destroy(engine);
    assert(unlink(path) == 0);
}

// Supplies a constant source so loop resets and explicit control resets have exact expected samples.
static void constant_source(void* userdata, float* out, int frames, uint64_t start) {
    (void)userdata;
    (void)start;
    for (int i = 0; i < frames; ++i)
        out[i] = 1;
}

// Proves ordinary loop source resets preserve a ramp while explicit discontinuities snap to target.
static void loop_control_history(void) {
    EngineGraph* old = engine_graph_create(48000, 1, 256);
    EngineGraph* next = engine_graph_create(48000, 1, 256);
    const EngineGraphSourceOps ops = {.render = constant_source};
    assert(old && next);
    assert(engine_graph_add_source_identified(old, &ops, NULL, 1, 0, 42));
    assert(engine_graph_add_source_identified(next, &ops, NULL, 0, 0, 42));
    const int mapping[] = {0};
    float out[256];
    engine_graph_transfer_gains(next, old, mapping, 1);
    engine_graph_render_track(next, out, 120, 0, 0);
    assert(fabsf(out[119] - .5f) < 2e-6f);
    engine_graph_reset(next);
    engine_graph_render_track(next, out, 120, 0, 0);
    assert(out[0] < .5f && out[0] > .49f && out[119] == 0);
    engine_graph_transfer_gains(next, old, mapping, 1);
    engine_graph_reset_control_ramps(next);
    engine_graph_render_track(next, out, 120, 0, 0);
    assert(out[0] == 0 && out[119] == 0);
    engine_graph_destroy(old);
    engine_graph_destroy(next);
}

// Runs the first S3.4 transition acceptance increment against production DSP and manager code.
int main(void) {
    gain_transitions(44100);
    gain_transitions(48000);
    gain_transitions(96000);
    zero_endpoint();
    manager_handoff();
    bypass_transition();
    mixer_transitions();
    loop_control_history();
    puts("control_transitions_test: success (Gain ramps, partitions, retarget, reset, zero endpoint, "
         "revision handoff, bypass, mixer gain/pan/mute/solo)");
    return 0;
}
