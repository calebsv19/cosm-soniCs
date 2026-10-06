#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include "test_assert.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(v, m) daw_test_expect("mixer_publication_test", (v), (m))
static int allocation, fail_at;
static bool guard;
static int guarded_calls;
// Injects scalar preparation failures and detects worker-side ownership allocations.
static void* probe_calloc(size_t count, size_t bytes) {
    if (guard) ++guarded_calls;
    if (++allocation == fail_at) return NULL;
    return calloc(count, bytes);
}
// Detects plan retirement accidentally freeing on the render worker.
static void probe_free(void* pointer) {
    if (guard) ++guarded_calls;
    free(pointer);
}
#define calloc probe_calloc
#define free probe_free
#include "../src/engine/engine_source_plan.c"
#undef free
#undef calloc

// Models one worker boundary without a physical device and verifies retirement stays deferred.
static void adopt(Engine* engine) {
    guard = true;
    engine_source_plan_apply(engine);
    guard = false;
    CHECK(guarded_calls == 0, "heap call during adoption");
}

// Compares scalar adoption against complete preparation through transitions and structural edits.
int main(void) {
    const char* path = "tmp/mixer_publication.wav";
    float samples[8192];
    for (int i = 0; i < 8192; ++i) samples[i] = 0.2f;
    CHECK(wav_write_f32(path, samples, 4096, 2, 48000), "fixture");
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* fast = engine_create(&config);
    Engine* reference = engine_create(&config);
    CHECK(fast && reference, "engines");
    int clip;
    CHECK(engine_add_clip_to_track(fast, 0, path, 0, &clip), "fast source");
    CHECK(engine_add_clip_to_track(reference, 0, path, 0, &clip), "reference source");
    EngineEqCurve curve = {0};
    curve.low_cut.enabled = true; curve.low_cut.freq_hz = 100;
    CHECK(engine_set_track_eq_curve(fast, 0, &curve) && engine_set_track_eq_curve(reference, 0, &curve), "EQ");
    fast->device_started = reference->device_started = true;
    fast->worker_thread = reference->worker_thread = (SDL_Thread*)(uintptr_t)1;
    atomic_store(&fast->worker_running, true); atomic_store(&reference->worker_running, true);
    EngineSourcePlan* identity = fast->active_source_plan;
    for (int step = 0; step < 16; ++step) {
        float gain = step & 1 ? 0.3f : 0.8f;
        float pan = step % 3 == 0 ? -0.4f : 0.2f;
        if (step == 4 || step == 8) {
            CHECK(engine_insert_track(fast, 0) && engine_insert_track(reference, 0), "structural before scalar");
        }
        int track = fast->track_count - 1;
        CHECK(engine_track_set_gain(fast, track, gain) && engine_track_set_pan(fast, track, pan), "scalar edits");
        CHECK(engine_track_set_muted(fast, track, step == 3) && engine_track_set_solo(fast, track, step == 5), "mute/solo");
        reference->tracks[track].gain = gain; reference->tracks[track].pan = pan;
        reference->tracks[track].muted = step == 3; reference->tracks[track].solo = step == 5;
        CHECK(engine_request_rebuild_sources(reference), "reference full capture");
        if (step == 6) {
            CHECK(engine_remove_track(fast, 0) && engine_remove_track(reference, 0), "structural after scalar");
        }
        adopt(fast); adopt(reference);
        if (step < 4) CHECK(fast->active_source_plan == identity, "scalar edit rebuilt sources");
        float a[256], b[256], scratch[256];
        engine_mix_tracks(fast, step * 128, 128, a, scratch, 2);
        engine_mix_tracks(reference, step * 128, 128, b, scratch, 2);
        CHECK(memcmp(a, b, sizeof(a)) == 0, "scalar/full sample parity or EQ history");
        engine_source_plan_collect(fast); engine_source_plan_collect(reference);
    }
    int track = fast->track_count - 1;
    CHECK(engine_track_set_pan(fast, track, 0.7f), "pending accepted edit");
    EngineSourcePlan* pending = atomic_load(&fast->pending_source_plan);
    float previous = fast->tracks[track].gain;
    for (int stage = 1; stage <= 2; ++stage) {
        allocation = 0; fail_at = stage;
        CHECK(!engine_track_set_gain(fast, track, 0.123f), "preparation failure accepted");
        CHECK(fast->tracks[track].gain == previous && atomic_load(&fast->pending_source_plan) == pending,
              "rejection changed model or pending publication");
    }
    fail_at = 0;
    printf("scalar payload: %zu bytes for %d tracks\n", sizeof(EngineSourcePlan) + fast->track_count * sizeof(EngineScalarTrack), fast->track_count);
    adopt(fast);
    CHECK(engine_render_mix_state(fast)->tracks[track].pan == 0.7f, "accepted pending edit lost");
    for (int i = 0; i < 2; ++i) {
        Engine* engine = i ? reference : fast;
        engine->worker_thread = NULL; engine->device_started = false;
        atomic_store(&engine->worker_running, false);
        engine_destroy(engine);
    }
    unlink(path);
    puts("mixer_publication_test: success (coalescing, structural ordering, exact full-revision parity, histories, two failures, deferred retirement)");
    return 0;
}
