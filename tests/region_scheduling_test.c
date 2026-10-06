#include "engine/graph.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int fail_after = -1, guarded, heap_calls;
// Counts render heap calls and injects control-side allocation failures.
static int allocation_fails(void) {
    if (guarded) ++heap_calls;
    if (fail_after < 0) return 0;
    if (!fail_after) return 1;
    --fail_after;
    return 0;
}
// Wraps graph allocations while preserving real memory ownership.
void* daw_test_malloc(size_t n) { return allocation_fails() ? NULL : malloc(n); }
// Wraps zeroed graph allocations for failure and render guards.
void* daw_test_calloc(size_t n, size_t size) { return allocation_fails() ? NULL : calloc(n, size); }
// Wraps graph growth while retaining old allocations on rejection.
void* daw_test_realloc(void* p, size_t n) { return allocation_fails() ? NULL : realloc(p, n); }
// Records unexpected render frees without changing release semantics.
void daw_test_free(void* p) { if (guarded) ++heap_calls; free(p); }

// Describes a stateless region with observable callback invocation and exact sample bounds.
typedef struct Region { uint64_t start, length; float value; int calls; } Region;
// Produces deterministic samples including the legacy unsigned timeline-wrap behavior.
static void render_region(void* user, float* out, int frames, uint64_t start) {
    Region* r = user; ++r->calls;
    for (int i = 0; i < frames; ++i) {
        uint64_t f = start + (uint64_t)i;
        float v = f >= r->start && f - r->start < r->length ? r->value : 0;
        out[2 * i] = v; out[2 * i + 1] = -.5f * v;
    }
}
static const EngineGraphSourceOps ops = {.render = render_region};

// Builds bounded or reference callbacks in deliberately interleaved track/source order.
static EngineGraph* build(Region* regions, int bounded, float gain) {
    EngineGraph* g = engine_graph_create(48000, 2, 128); assert(g);
    int tracks[] = {7, 0, 7, 1000, 7, 0, -1};
    for (int i = 0; i < 7; ++i) {
        assert(engine_graph_add_source_identified(g, &ops, &regions[i], gain, tracks[i], i));
        if (bounded) engine_graph_bound_last_source(g, regions[i].start, regions[i].length);
    }
    if (bounded) assert(engine_graph_prepare_identity_lookup(g));
    return g;
}

// Compares exact summation, seeks, endpoints, track isolation, ramps, resets, and uint64 wrap.
static void parity(void) {
    Region a[] = {{17, 13, 1e5f, 0}, {0, 900, .125f, 0}, {17, 600, -1e5f, 0},
                  {300, 100, .5f, 0}, {17, 300, .2f, 0}, {45, 0, .5f, 0}, {0, 40, .3f, 0}};
    Region b[7]; memcpy(b, a, sizeof(a));
    EngineGraph* baseline = build(a, 0, 1), *indexed = build(b, 1, 1);
    EngineGraph* old_a = build(a, 0, 0), *old_b = build(b, 1, 0);
    int mapping[1001]; for (int i = 0; i < 1001; ++i) mapping[i] = i;
    engine_graph_transfer_gains(baseline, old_a, mapping, 1001);
    engine_graph_transfer_gains(indexed, old_b, mapping, 1001);
    engine_graph_destroy(old_a); engine_graph_destroy(old_b);
    uint64_t starts[] = {9000, 9001, 0, 16, 17, 29, 30, 299, 300, 0, UINT64_MAX - 8, 10};
    int sizes[] = {64, 64, 17, 1, 13, 1, 97, 128, 9, 128, 32, 97};
    int filters[] = {7, 0, 1000, 4, -1};
    for (int t = 0; t < 5; ++t) for (int k = 0; k < 12; ++k) {
        float x[256], y[256];
        guarded = 1;
        engine_graph_render_track(baseline, x, sizes[k], starts[k], filters[t]);
        engine_graph_render_track(indexed, y, sizes[k], starts[k], filters[t]);
        guarded = 0;
        assert(!memcmp(x, y, (size_t)sizes[k] * 2 * sizeof(float)));
        if (k == 8) { engine_graph_reset(baseline); engine_graph_reset(indexed); }
        if (k == 9) { engine_graph_reset_control_ramps(baseline); engine_graph_reset_control_ramps(indexed); }
    }
    assert(!heap_calls);
    assert(b[0].calls < a[0].calls && b[5].calls < a[5].calls);
    int prior = b[3].calls;
    float out[256]; engine_graph_render_track(indexed, out, 128, 300, 7);
    assert(b[3].calls == prior); // Other tracks never run.
    engine_graph_clear_sources(indexed);
    assert(engine_graph_add_source(indexed, &ops, &b[1], 1, 42));
    engine_graph_render_track(indexed, out, 128, 0, 42); assert(out[1] == -.0625f);
    engine_graph_render_track(indexed, out, 128, 0, 7); assert(out[0] == 0);
    engine_graph_destroy(baseline); engine_graph_destroy(indexed);
}

// Proves both source-array and track-index allocation failures leave additions retryable.
static void failure(void) {
    for (int fail = 0; fail < 2; ++fail) {
        EngineGraph* g = engine_graph_create(48000, 2, 128); assert(g);
        Region r = {0, 128, .25f, 0};
        fail_after = fail;
        assert(!engine_graph_add_source(g, &ops, &r, 1, 9));
        fail_after = -1;
        float out[256]; engine_graph_render(g, out, 128, 0); assert(out[0] == 0 && !r.calls);
        assert(engine_graph_add_source(g, &ops, &r, 1, 9));
        engine_graph_render_track(g, out, 128, 0, 9); assert(out[0] == .25f && r.calls == 1);
        engine_graph_destroy(g);
    }
}

// Runs the graph scheduling contract without a device or timing-dependent assertions.
int main(void) {
    parity(); failure();
    puts("region_scheduling_test: success (bounds, overlap/order, track lookup, ramps, seeks, resets, wrap, rollback, zero render heap calls)");
    return 0;
}
