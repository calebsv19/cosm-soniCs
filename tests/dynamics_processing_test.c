#define _POSIX_C_SOURCE 200809L
#include "audio/wav_writer.h"
#include "effects/param_specs/dynamics_param_specs.h"
#include "engine/engine_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int limiter_get_desc(FxDesc*);
int limiter_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int sccomp_get_desc(FxDesc*);
int sccomp_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int compressor_get_desc(FxDesc*);
int compressor_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
static bool render_guard;
static unsigned forbidden_heap_calls;
static int allocation_fail_after = -1;
// Injects a bounded preparation failure without changing any active render
// manager.
static bool fail_allocation(void) {
    if (allocation_fail_after < 0)
        return false;
    if (allocation_fail_after == 0)
        return true;
    --allocation_fail_after;
    return false;
}
// Counts heap use in instrumented production dynamics and manager objects
// during guarded rendering.
void* daw_test_malloc(size_t n) {
    if (render_guard)
        ++forbidden_heap_calls;
    return fail_allocation() ? NULL : malloc(n);
}
// Counts prepared allocations that would be forbidden during processing or
// parameter application.
void* daw_test_calloc(size_t n, size_t size) {
    if (render_guard)
        ++forbidden_heap_calls;
    return fail_allocation() ? NULL : calloc(n, size);
}
// Counts storage resizing that would be forbidden during rendering.
void* daw_test_realloc(void* p, size_t n) {
    if (render_guard)
        ++forbidden_heap_calls;
    return fail_allocation() ? NULL : realloc(p, n);
}
// Counts deallocation that would be forbidden during rendering.
void daw_test_free(void* p) {
    if (render_guard)
        ++forbidden_heap_calls;
    free(p);
}

// Holds one real production DSP instance under test.
typedef struct Processor {
    FxHandle* handle;
    FxVTable vt;
} Processor;
// Creates a production limiter or compressor with a defined format.
static Processor processor(bool limiter, unsigned rate, unsigned channels) {
    Processor p = {0};
    FxDesc desc = {0};
    assert((limiter ? limiter_get_desc : compressor_get_desc)(&desc));
    assert((limiter ? limiter_create : compressor_create)(&desc, &p.handle, &p.vt, rate, 1024, channels));
    return p;
}
// Applies a control under the same heap prohibition as the render thread.
static void parameter(Processor* p, unsigned index, float value) {
    render_guard = true;
    p->vt.set_param(p->handle, index, value);
    render_guard = false;
}
// Resets a production processor under the render-thread heap prohibition.
static void reset_processor(Processor* p) {
    render_guard = true;
    p->vt.reset(p->handle);
    render_guard = false;
}

// Processes a buffer using varied partitions to expose state and stride errors.
static void process(Processor* p, const float* input, float* output, int frames, int channels,
                    int partition) {
    render_guard = true;
    for (int offset = 0; offset < frames;) {
        int count = frames - offset < partition ? frames - offset : partition;
        p->vt.process(p->handle, input + offset * channels, output + offset * channels, count, channels);
        offset += count;
    }
    render_guard = false;
}
// Checks exact impulse delay across rates, channel counts, endpoints, and
// partition boundaries.
static void limiter_delay(void) {
    unsigned rates[] = {44100, 48000, 96000};
    for (int r = 0; r < 3; ++r)
        for (int ch = 1; ch <= 6; ch += ch == 1 ? 1 : 4) {
            Processor p = processor(true, rates[r], ch);
            for (int ms = 0; ms <= 3; ++ms) {
                parameter(&p, 1, ms);
                reset_processor(&p);
                unsigned delay = (unsigned)llround(rates[r] * ms * .001);
                assert(p.vt.latency(p.handle) == delay);
                assert(p.vt.max_latency(p.handle) == (unsigned)llround(rates[r] * .003));
                float in[1024 * 6] = {0}, out[1024 * 6] = {0};
                for (int c = 0; c < ch; ++c)
                    in[c] = .2f / (c + 1);
                process(&p, in, out, 1024, ch, 17);
                for (int n = 0; n < 1024; ++n)
                    for (int c = 0; c < ch; ++c)
                        assert(fabsf(out[n * ch + c] - (n == (int)delay ? in[c] : 0)) < 1e-7f);
            }
            p.vt.destroy(p.handle);
        }
}
// Verifies ceiling, stereo linking, anticipation, release, partition
// invariance, and reset transitions.
static void limiter_behavior(void) {
    Processor a = processor(true, 48000, 2), b = processor(true, 48000, 2);
    parameter(&a, 0, -6);
    parameter(&b, 0, -6);
    float input[4096 * 2], one[4096 * 2], split[4096 * 2];
    for (int n = 0; n < 4096; ++n) {
        input[n * 2] = n < 256 ? .2f : (n == 256 ? 4 : .2f);
        input[n * 2 + 1] = input[n * 2] * .5f;
    }
    process(&a, input, one, 4096, 2, 4096);
    process(&b, input, split, 4096, 2, 13);
    float ceiling = powf(10, -.3f);
    for (int n = 0; n < 8192; ++n)
        assert(fabsf(one[n] - split[n]) < 1e-7f && fabsf(one[n]) <= ceiling + 1e-6f);
    for (int n = 0; n < 4096; ++n)
        assert(one[n * 2 + 1] == one[n * 2] * .5f);
    assert(one[255 * 2] > one[256 * 2]); // Future transient attenuates audio
                                         // before its delayed arrival.
    assert(fabsf(one[(256 + 48) * 2] - ceiling) < 1e-6f);
    assert(one[4000 * 2] > one[400 * 2] && one[4000 * 2] < .2f);
    assert(a.vt.gain_reduction_db(a.handle) < -17.9f);
    parameter(&a, 1, NAN);
    assert(a.vt.latency(a.handle) == 48);
    parameter(&a, 1, 3);
    assert(a.vt.latency(a.handle) == 144);
    for (int n = 0; n < 8192; ++n)
        one[n] = .1f;
    process(&a, one, one, 512, 2, 7);
    for (int n = 0; n < 144 * 2; ++n)
        assert(one[n] == 0);
    assert(fabsf(one[144 * 2] - .1f) < 1e-7f);
    parameter(&a, 1, 3); // Repeating the same sample delay retains the captured constant history.
    one[0] = one[1] = .1f;
    process(&a, one, one, 1, 2, 1);
    assert(fabsf(one[0] - .1f) < 1e-7f);
    parameter(&a, 1, 0);
    one[0] = INFINITY;
    one[1] = NAN;
    process(&a, one, one, 1, 2, 1);
    assert(one[0] == 0 && one[1] == 0);
    a.vt.destroy(a.handle);
    b.vt.destroy(b.handle);
}
// Computes an independent analytic compressor transfer with a continuous
// quadratic knee.
static float reference_reduction(float input_db, float knee) {
    float delta = input_db + 18;
    if (delta <= -knee / 2)
        return 0;
    if (knee == 0 || delta >= knee / 2)
        return -.75f * delta;
    float distance = delta + knee / 2;
    return -.75f * distance * distance / (2 * knee);
}
// Checks steady transfer, knee endpoints, detector modes, makeup exclusion, and
// independent channels.
static void compressor_behavior(void) {
    Processor p = processor(false, 48000, 2);
    parameter(&p, 2, .1f);
    parameter(&p, 4, 6);
    float data[2048];
    const float levels[] = {-30, -21.001f, -21, -18, -15, -14.999f, -6};
    for (int mode = 0; mode < 2; ++mode)
        for (int knee = 0; knee <= 6; knee += 6)
            for (unsigned level = 0; level < sizeof(levels) / sizeof(levels[0]); ++level) {
                parameter(&p, 6, mode);
                parameter(&p, 5, knee);
                reset_processor(&p);
                float x = powf(10, levels[level] / 20);
                for (int block = 0; block < 8; ++block) {
                    for (int n = 0; n < 1024; ++n) {
                        data[2 * n] = x;
                        data[2 * n + 1] = .001f;
                    }
                    process(&p, data, data, 1024, 2, 1024);
                }
                float gr = reference_reduction(levels[level], knee);
                assert(fabsf(20 * log10f(data[2046] / x) - (gr + 6)) < .002f);
                assert(fabsf(p.vt.gain_reduction_db(p.handle) - gr) < .002f);
                assert(fabsf(data[2047] - .001f * powf(10, .3f)) < 1e-7f);
            }
    parameter(&p, 1, 1);
    parameter(&p, 4, 0);
    reset_processor(&p);
    for (int n = 0; n < 2048; ++n)
        data[n] = .5f;
    process(&p, data, data, 1024, 2, 23);
    assert(data[2047] == .5f && p.vt.gain_reduction_db(p.handle) == 0);
    p.vt.destroy(p.handle);
}

// Checks the existing sidechain-capable fallback and explicit-key API against
// the same continuous knee.
static void sidechain_compressor_behavior(void) {
    Processor p = {0};
    FxDesc desc = {0};
    assert(sccomp_get_desc(&desc) && sccomp_create(&desc, &p.handle, &p.vt, 48000, 1024, 2));
    parameter(&p, 2, .1f);
    parameter(&p, 4, 6);
    float data[2048], key[1024];
    for (int mode = 0; mode < 2; ++mode)
        for (int explicit_key = 0; explicit_key < 2; ++explicit_key) {
            parameter(&p, 6, mode);
            reset_processor(&p);
            float x = powf(10, -15.0f / 20); // Upper knee endpoint, which previously jumped.
            for (int block = 0; block < 8; ++block) {
                for (int n = 0; n < 1024; ++n) {
                    data[2 * n] = x;
                    data[2 * n + 1] = x * .5f;
                    key[n] = x;
                }
                render_guard = true;
                if (explicit_key)
                    p.vt.process_sc(p.handle, data, key, data, 1024, 2, 1);
                else
                    p.vt.process(p.handle, data, data, 1024, 2);
                render_guard = false;
            }
            assert(fabsf(p.vt.gain_reduction_db(p.handle) + 2.25f) < .002f);
            assert(fabsf(20 * log10f(data[2046] / x) - 3.75f) < .002f);
            assert(data[2047] == data[2046] * .5f);
        }
    p.vt.destroy(p.handle);
}

// Checks compressor attack/release recovery and sample-identical output under
// repartitioning.
static void compressor_transitions(void) {
    for (int mode = 0; mode < 2; ++mode) {
        Processor a = processor(false, 48000, 1), b = processor(false, 48000, 1);
        parameter(&a, 6, mode);
        parameter(&b, 6, mode);
        float input[8192], one[8192], split[8192];
        for (int n = 0; n < 8192; ++n)
            input[n] = n < 1024 ? 1 : .01f;
        process(&a, input, one, 8192, 1, 8192);
        process(&b, input, split, 8192, 1, 31);
        for (int n = 0; n < 8192; ++n)
            assert(fabsf(one[n] - split[n]) < 1e-7f);
        assert(one[0] > one[1023]);
        assert(one[8191] > one[1024] && one[8191] <= .010001f);
        reset_processor(&a);
        memset(one, 0, sizeof(one));
        process(&a, one, one, 8192, 1, 8192);
        assert(a.vt.gain_reduction_db(a.handle) == 0);
        parameter(&a, 0, NAN);
        parameter(&a, 1, INFINITY);
        process(&a, input, split, 8192, 1, 8192);
        for (int n = 0; n < 8192; ++n)
            assert(isfinite(split[n]));
        a.vt.destroy(a.handle);
        b.vt.destroy(b.handle);
    }
}

// Registers only the actual dynamics under test, with their real control
// metadata.
static EffectsManager* manager(void) {
    FxConfig config = {.sample_rate = 48000, .max_block = 512, .max_channels = 2};
    EffectsManager* fm = fxm_create(&config);
    FxRegistryEntry entries[] = {
        {21, "Limiter", limiter_get_desc, limiter_create, kLimiterParamSpecs, LIMITER_PARAM_SPEC_COUNT},
        {20, "Compressor", compressor_get_desc, compressor_create, kCompressorParamSpecs,
         COMPRESSOR_PARAM_SPEC_COUNT}};
    assert(fm && fxm_register_builtin(fm, entries, 2) && fxm_set_track_count(fm, 2));
    return fm;
}
static float scope_value;
// Captures the production manager's scope value to test makeup-independent
// reduction.
static void scope(void* user, bool master, int track, FxInstId id, FxTypeId type, float value) {
    (void)user;
    (void)master;
    (void)track;
    (void)id;
    (void)type;
    scope_value = value;
}
// Mixes two impulse tracks through real serial chains, compensation, and the
// master chain.
static void render_manager(EffectsManager* fm, float* out, int frames, int start) {
    float track[1024];
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    render_guard = true;
    fxm_begin_render_block(fm, frames);
    for (int t = 0; t < 2; ++t) {
        memset(track, 0, sizeof(track));
        if (start == 0)
            track[0] = track[1] = t == 0 ? .2f : -.2f;
        fxm_render_track(fm, t, track, frames, 2);
        fxm_align_track(fm, t, track, frames, 2);
        for (int n = 0; n < frames * 2; ++n)
            out[n] += track[n];
    }
    fxm_render_master(fm, out, frames, 2);
    render_guard = false;
}
// Proves serial latency, parallel cancellation, bypass, new targets, and
// compatible history transfer.
static void manager_alignment(void) {
    EffectsManager* control = manager();
    FxInstId first = fxm_track_add(control, 0, 21);
    assert(first && fxm_track_add(control, 0, 21) && fxm_master_add(control, 21));
    EffectsManager* active = fxm_clone_for_render(control);
    assert(active);
    float out[1024];
    render_manager(active, out, 31, 0);
    assert(fxm_processing_latency(active) == 144);
    EffectsManager* next = fxm_clone_for_render(control);
    int mapping[] = {0, 1};
    render_guard = true;
    assert(fxm_transfer_render_state(next, active, mapping, 2));
    render_guard = false;
    fxm_destroy(active);
    active = next;
    for (int n = 31; n < 512; n += 31) {
        render_manager(active, out, 31, n);
        for (int i = 0; i < 62; ++i)
            assert(fabsf(out[i]) < 1e-7f);
    }
    assert(fxm_track_set_param(control, 0, first, 1, 3));
    next = fxm_clone_for_render(control);
    assert(next && fxm_transfer_render_state(next, active, mapping, 2));
    fxm_destroy(active);
    active = next;
    render_manager(active, out, 512, 0);
    assert(fxm_processing_latency(active) == 240);
    for (int i = 0; i < 1024; ++i)
        assert(fabsf(out[i]) < 1e-7f);
    assert(fxm_track_set_enabled(control, 0, first, false));
    next = fxm_clone_for_render(control);
    assert(next && fxm_transfer_render_state(next, active, mapping, 2));
    fxm_destroy(active);
    active = next;
    render_manager(active, out, 512, 0);
    assert(fxm_processing_latency(active) == 96);
    for (int i = 0; i < 1024; ++i)
        assert(fabsf(out[i]) < 1e-7f);
    fxm_destroy(active);
    fxm_destroy(control);
    control = manager();
    FxInstId comp = fxm_master_add(control, 20);
    assert(comp);
    assert(fxm_master_set_param(control, comp, 4, 12));
    active = fxm_clone_for_render(control);
    fxm_set_scope_tap_callback(active, scope, NULL);
    for (int block = 0; block < 100; ++block) {
        for (int i = 0; i < 1024; ++i)
            out[i] = 1;
        render_guard = true;
        fxm_begin_render_block(active, 512);
        fxm_render_master(active, out, 512, 2);
        render_guard = false;
    }
    assert(fabsf(scope_value + 13.5f) < .01f); // -13.5 dB detector reduction despite +12 dB makeup.
    fxm_destroy(active);
    fxm_destroy(control);
}
// Exercises every allocation boundary in a dynamics revision, including
// compensation rings.
static void preparation_failures(void) {
    EffectsManager* control = manager();
    assert(fxm_track_add(control, 0, 21) && fxm_master_add(control, 21));
    EffectsManager* active = fxm_clone_for_render(control);
    assert(active);
    int failures = 0;
    bool finished = false;
    for (int boundary = 0; boundary < 64; ++boundary) {
        allocation_fail_after = boundary;
        EffectsManager* next = fxm_clone_for_render(control);
        allocation_fail_after = -1;
        if (next) {
            fxm_destroy(next);
            finished = true;
            break;
        }
        ++failures;
        float out[1024];
        render_guard = true;
        fxm_reset_render_state(active);
        render_guard = false;
        render_manager(active, out, 512, 0);
        assert(fxm_processing_latency(active) == 96);
        for (int n = 0; n < 1024; ++n)
            assert(fabsf(out[n]) < 1e-7f);
    }
    assert(finished && failures >= 10);
    fxm_destroy(active);
    fxm_destroy(control);
}

// Verifies delayed positive audio survives a compatible revision instead of
// disappearing with both tracks.
static void positive_history_transfer(void) {
    EffectsManager* control = manager();
    assert(fxm_track_add(control, 0, 21) && fxm_master_add(control, 21));
    EffectsManager* active = fxm_clone_for_render(control);
    float track[256] = {0}, out[256] = {0};
    int mapping[] = {0, 1};
    bool observed = false;
    for (int start = 0; start < 160; start += 16) {
        if (start == 32) {
            EffectsManager* next = fxm_clone_for_render(control);
            assert(next && fxm_transfer_render_state(next, active, mapping, 2));
            fxm_destroy(active);
            active = next;
        }
        memset(out, 0, sizeof(out));
        render_guard = true;
        fxm_begin_render_block(active, 16);
        for (int t = 0; t < 2; ++t) {
            memset(track, 0, sizeof(track));
            if (start == 0)
                track[0] = track[1] = .1f;
            fxm_render_track(active, t, track, 16, 2);
            fxm_align_track(active, t, track, 16, 2);
            for (int n = 0; n < 32; ++n)
                out[n] += track[n];
        }
        fxm_render_master(active, out, 16, 2);
        render_guard = false;
        for (int n = 0; n < 16; ++n) {
            float expected = start + n == 96 ? .2f : 0;
            assert(fabsf(out[n * 2] - expected) < 1e-7f);
            if (expected)
                observed = true;
        }
    }
    assert(observed);
    fxm_destroy(active);
    fxm_destroy(control);
}

// Proves real mixer alignment, exact-range repeated bounce, diagnostics, and
// live revision changes.
static void engine_integration(void) {
    char path[] = "/tmp/daw-dynamics-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    float audio[1024] = {0};
    audio[0] = audio[1] = .1f;
    audio[1022] = audio[1023] = .15f;
    assert(wav_write_f32(path, audio, 512, 2, 48000));
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 64;
    Engine* engine = engine_create(&config);
    assert(engine && engine_add_track(engine) == 1);
    assert(engine_add_clip_to_track(engine, 0, path, 0, NULL));
    assert(engine_add_clip_to_track(engine, 1, path, 0, NULL));
    FxInstId limiter = engine_fx_track_add(engine, 0, 21);
    assert(limiter && engine_fx_track_set_param(engine, 0, limiter, 1, 3));
    assert(engine_fx_master_add(engine, 21));
    float mixed[128], scratch[128];
    for (int start = 0; start < 256; start += 64) {
        render_guard = true;
        engine_mix_tracks(engine, start, 64, mixed, scratch, 2);
        render_guard = false;
        for (int n = 0; n < 64; ++n)
            assert(fabsf(mixed[n * 2] - (start + n == 192 ? .2f : 0)) < 2e-6f);
    }
    EngineDiagnostics diagnostics;
    assert(engine_get_diagnostics(engine, &diagnostics));
    assert(diagnostics.processing_latency_frames == 192);
    for (int repeat = 0; repeat < 2; ++repeat) {
        EngineBounceBuffer bounce = {0};
        assert(engine_bounce_range_to_buffer(engine, 0, 512, NULL, NULL, &bounce));
        assert(bounce.frame_count == 512 && bounce.channels == 2);
        for (int n = 0; n < 1024; ++n)
            assert(fabsf(bounce.data[n] - audio[n] * 2) < 2e-6f);
        engine_bounce_buffer_free(&bounce);
    }
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(engine_start(engine) && engine_transport_play(engine));
    for (int edit = 0; edit < 24; ++edit) {
        assert(engine_fx_track_set_param(engine, 0, limiter, 1, (float)(edit % 4)));
        assert(engine_fx_track_set_enabled(engine, 0, limiter, edit % 3 != 0));
        SDL_Delay(2);
    }
    engine_stop(engine);
    engine_destroy(engine);
    assert(unlink(path) == 0);
}

// Runs deterministic production DSP and manager contracts without an audio
// device.
int main(void) {
    limiter_delay();
    limiter_behavior();
    compressor_behavior();
    compressor_transitions();
    sidechain_compressor_behavior();
    manager_alignment();
    preparation_failures();
    positive_history_transfer();
    engine_integration();
    assert(forbidden_heap_calls == 0);
    puts("dynamics_processing_test: success (delay, ceiling, linking, release, "
         "knee, detectors, makeup, alignment, revisions, mixer, bounce, live "
         "edits, preparation failures, zero render heap calls)");
    return 0;
}
