#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static int guarded, heap_calls, fail_after = -1;
// Records any heap operation inside a render boundary and supports reserve rejection.
static int fail_allocation(void) {
    if (guarded) ++heap_calls;
    if (fail_after < 0) return 0;
    if (!fail_after) return 1;
    --fail_after; return 0;
}
// Wraps prepared source initialization without changing allocator semantics.
static void* probe_calloc(size_t n, size_t s) { return fail_allocation() ? NULL : calloc(n, s); }
// Rejects capacity growth while retaining the existing source on failure.
static void* probe_realloc(void* p, size_t n) { return fail_allocation() ? NULL : realloc(p, n); }
// Counts instrument frees inside guarded rendering.
static void probe_free(void* p) { if (guarded) ++heap_calls; free(p); }
#define calloc probe_calloc
#define realloc probe_realloc
#define free probe_free
#include "../src/engine/instrument_osc.c"
#undef calloc
#undef realloc
#undef free
#include "fixtures/instrument_unscheduled_reference.inc"

// Compares scheduled rendering to the retained pre-scheduling scan over all presets and automation states.
static void parity(int rate, int channels, EngineInstrumentPresetId preset) {
    EngineInstrumentSource* s = engine_instrument_source_create(); assert(s);
    EngineMidiNote notes[128];
    for (int n = 0; n < 128; ++n) notes[n] = (EngineMidiNote){
        n < 16 ? (uint64_t)((15 - n) % 9) * 160 : (uint64_t)(n + 1) * 50000,
        (uint64_t)(n % 13 + 1) * 42, (uint8_t)(36 + n % 60), .08f};
    notes[0] = (EngineMidiNote){0, (uint64_t)rate, 69, .2f};
    EngineAutomationPoint points[] = {{0, -.8f}, {2000, 1}, {4000, -.5f}, {8000, .5f}};
    EngineAutomationLane clip[] = {
        {ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_MS, points, 4, 4},
        {ENGINE_AUTOMATION_TARGET_VOLUME, points, 4, 4},
        {ENGINE_AUTOMATION_TARGET_PAN, points, 4, 4}};
    EngineAutomationLane track = {ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_DEPTH, points, 4, 4};
    EngineInstrumentParams params = engine_instrument_default_params(preset);
    assert(engine_instrument_source_set_midi_clip(s, 123, 10000000, preset, params, notes, 128, &track, 1, clip, 3));
    engine_instrument_source_reset(s, rate, channels);
    uint64_t positions[] = {0, 120, 123, 160, 500, 2000, 4000, 12000, 9999999, 500, UINT64_MAX - 50};
    int sizes[] = {128, 7, 64, 511, 128, 128, 257, 128, 128, 512, 128};
    double energy = 0;
    for (int phase = 0; phase < 2; ++phase) {
        if (phase) engine_instrument_source_set_export_end(s, 2123, 24000);
        for (int k = 0; k < 11; ++k) {
            float a[1024], b[1024];
            for (int n = 0; n < 1024; ++n) a[n] = b[n] = .1234f;
            guarded = 1;
            instrument_reference_render(s, a, sizes[k], positions[k]);
            engine_instrument_source_render(s, b, sizes[k], positions[k]);
            guarded = 0;
            assert(!memcmp(a, b, (size_t)sizes[k] * channels * sizeof(float)));
            if (k == 4) assert(s->candidate_count < 20);
            if (k == 5) for (int n = 0; n < sizes[k] * channels; ++n) energy += fabs(a[n]);
            if (k == 7) engine_instrument_source_reset(s, rate, channels);
        }
    }
    assert(energy > 0 && !heap_calls);
    fail_after = 0;
    assert(!engine_instrument_source_reserve_notes(s, 256));
    fail_after = -1;
    assert(s->note_capacity == 128);
    engine_instrument_source_destroy(s);
}

// Verifies candidate scheduling across formats while leaving DSP formulas under existing tests.
int main(void) {
    int rates[] = {44100, 48000, 96000};
    for (int r = 0; r < 3; ++r) for (int channels = 1; channels <= 2; ++channels)
        for (int p = 0; p < ENGINE_INSTRUMENT_PRESET_COUNT; ++p) parity(rates[r], channels, p);
    puts("midi_scheduling_test: success (all presets, exact prior-scan parity, automation/release growth, seeks, export, formats, wrap, reserve failure, zero render heap calls)");
    return 0;
}
