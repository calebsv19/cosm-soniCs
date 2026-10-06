#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "engine/instrument_waveform.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool guarded;
static unsigned heap_calls;
// Counts heap calls made by the instrumented production instrument, MIDI list, and audition code.
void* daw_test_malloc(size_t size) {
    if (guarded)
        ++heap_calls;
    return malloc(size);
}
// Counts prepared-storage allocations attempted during live processing.
void* daw_test_calloc(size_t count, size_t size) {
    if (guarded)
        ++heap_calls;
    return calloc(count, size);
}
// Counts capacity growth attempted during live processing.
void* daw_test_realloc(void* pointer, size_t size) {
    if (guarded)
        ++heap_calls;
    return realloc(pointer, size);
}
// Counts ownership reclamation attempted during live processing.
void daw_test_free(void* pointer) {
    if (guarded && pointer)
        ++heap_calls;
    free(pointer);
}

// Configures a deterministic sine voice with a known gate and release duration.
static EngineInstrumentSource* voice(int rate, int channels, EngineInstrumentParams params, uint64_t gate) {
    EngineInstrumentSource* source = engine_instrument_source_create();
    EngineMidiNote note = {0, gate, 69, 1};
    assert(source &&
           engine_instrument_source_set_midi_clip(source, 0, 100000, ENGINE_INSTRUMENT_PRESET_PURE_SINE,
                                                  params, &note, 1, NULL, 0, NULL, 0));
    engine_instrument_source_reset(source, rate, channels);
    return source;
}

// Proves release starts at the actual attack/decay level and is independent of render partitioning.
static void envelopes(int rate) {
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    p.attack_ms = 20;
    p.decay_ms = 20;
    p.sustain = .4f;
    p.release_ms = 10;
    p.level = .2f;
    for (int kind = 0; kind < 3; ++kind) {
        int gate = rate * (kind == 0 ? 5 : kind == 1 ? 30 : 60) / 1000;
        int release = rate / 100;
        int count = gate + release + 50;
        float* whole = calloc((size_t)count * 2, sizeof(float));
        float* split = calloc((size_t)count * 2, sizeof(float));
        EngineInstrumentSource* source = voice(rate, 2, p, (uint64_t)gate);
        guarded = true;
        engine_instrument_source_render(source, whole, count, 0);
        engine_instrument_source_reset(source, rate, 2);
        for (int offset = 0; offset < count; offset += 17)
            engine_instrument_source_render(source, split + offset * 2,
                                            count - offset < 17 ? count - offset : 17, (uint64_t)offset);
        guarded = false;
        assert(memcmp(whole, split, (size_t)count * 2 * sizeof(float)) == 0);
        int attack = rate / 50, decay = rate / 50;
        float held = gate < attack           ? (float)gate / attack
                     : gate < attack + decay ? 1 + (p.sustain - 1) * (float)(gate - attack) / decay
                                             : p.sustain;
        for (int i = gate; i < count; ++i) {
            float envelope = i < gate + release ? held * (1 - (float)(i - gate) / release) : 0;
            float expected = (float)sin(2 * INSTRUMENT_WAVE_PI * 440 * i / rate) * (.16f + p.tone * .08f) *
                             p.level * envelope;
            assert(fabsf(whole[i * 2] - expected) < 2e-6f);
            assert(whole[i * 2] == whole[i * 2 + 1]);
        }
        assert(heap_calls == 0);
        engine_instrument_source_destroy(source);
        free(whole);
        free(split);
    }
}

// Measures folded high harmonics from the old and corrected saw at a coherent high note.
static void aliases(void) {
    const int rate = 48000, count = 48000;
    double naive_re = 0, naive_im = 0, fixed_re = 0, fixed_im = 0, triangle_re = 0, triangle_im = 0;
    for (int i = 0; i < count; ++i) {
        double phase = 2 * INSTRUMENT_WAVE_PI * 7000 * i / rate;
        double cycle = phase / (2 * INSTRUMENT_WAVE_PI) + .5;
        cycle -= floor(cycle);
        double old = 2 * cycle - 1, fixed = instrument_saw(phase, 7000, rate);
        double triangle = instrument_triangle(phase, 7000, rate);
        double angle = 2 * INSTRUMENT_WAVE_PI * 20000 * i / rate;
        naive_re += old * cos(angle);
        naive_im += old * sin(angle);
        fixed_re += fixed * cos(angle);
        fixed_im += fixed * sin(angle);
        triangle_re += triangle * cos(angle);
        triangle_im += triangle * sin(angle);
    }
    double old = 2 * hypot(naive_re, naive_im) / count, fixed = 2 * hypot(fixed_re, fixed_im) / count;
    assert(fixed < old * .4);
    assert(2 * hypot(triangle_re, triangle_im) / count < 1e-8);
    assert(instrument_saw(1, 24000, rate) == 0 && instrument_sine(1, 24000, rate) == 0);
    fprintf(stderr, "instrument_lifecycle_test: folded 20kHz saw component %.8f -> %.8f\n", old, fixed);
}

// Checks instrument volume and pan lanes against independently rendered unautomated samples.
static void signal_automation(void) {
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    p.attack_ms = 0;
    p.decay_ms = 0;
    p.sustain = 1;
    p.level = .2f;
    EngineInstrumentSource* source = voice(48000, 2, p, 10000);
    float base[512] = {0}, result[512] = {0};
    engine_instrument_source_render(source, base, 256, 0);
    EngineMidiNote note = {0, 10000, 69, 1};
    EngineAutomationPoint volume[] = {{0, -.25f}, {1000, -.25f}}, pan[] = {{0, .5f}, {1000, .5f}};
    EngineAutomationLane lanes[] = {{ENGINE_AUTOMATION_TARGET_VOLUME, volume, 2, 2},
                                    {ENGINE_AUTOMATION_TARGET_PAN, pan, 2, 2}};
    assert(engine_instrument_source_set_midi_clip(source, 0, 10000, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p,
                                                  &note, 1, lanes, 2, lanes, 2));
    engine_instrument_source_render(source, result, 256, 0);
    for (int i = 0; i < 256; ++i) {
        assert(result[i * 2] == 0);
        assert(fabsf(result[i * 2 + 1] - base[i * 2 + 1] * .5f) < 1e-7f);
    }
    engine_instrument_source_destroy(source);
}

// Exercises prepared live voices, retrigger, retirement, target changes, panic, and bounded stopped tails.
static void audition(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 128;
    Engine* engine = engine_create(&config);
    assert(engine && engine_add_track(engine) == 1);
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    p.release_ms = 10;
    EngineMidiNote* storage = engine->midi_audition_notes.notes;
    guarded = true;
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    engine->midi_audition_idle_frame = 1000;
    engine_midi_audition_apply_note_off(engine, 60);
    assert(engine->midi_audition_notes.note_count == 1 && storage[0].duration_frames == 1000);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    assert(engine->midi_audition_notes.note_count == 2);
    engine->midi_audition_idle_frame = 1480;
    engine_midi_audition_retire(engine);
    assert(engine->midi_audition_notes.note_count == 1);
    for (int i = 0; i < 1000; ++i) {
        engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p,
                                           (uint8_t)(i % 128), 1);
        assert(engine->midi_audition_notes.note_count <= 256);
    }
    assert(engine->midi_audition_notes.notes == storage);
    engine_midi_audition_apply_all_off(engine);
    assert(!engine->midi_audition_notes.note_count && !engine->midi_audition_tail_until);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    engine->midi_audition_idle_frame = 1000;
    engine_midi_audition_apply_note_off(engine, 60);
    engine->midi_audition_idle_frame = 1480;
    engine_midi_audition_retire(engine);
    assert(!engine->midi_audition_notes.note_count && engine->midi_audition_tail_until == 97480);
    engine->midi_audition_idle_frame = 97480;
    engine_midi_audition_retire(engine);
    assert(!engine->midi_audition_tail_until && engine->midi_audition_track_index == -1);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_SAW_LEAD, p, 64, 1);
    assert(engine->midi_audition_notes.note_count == 1 && storage[0].note == 64);
    engine_midi_audition_apply_all_off(engine);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    engine_midi_audition_apply_note_on(engine, 1, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 64, 1);
    assert(engine->midi_audition_notes.note_count == 1 && engine->midi_audition_track_index == 1);
    engine_midi_audition_apply_all_off(engine);
    atomic_store(&engine->transport_playing, true);
    engine->transport_frame = 1000;
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 60, 1);
    engine->transport_frame = 2000;
    engine_midi_audition_apply_note_off(engine, 60);
    engine->transport_frame = 2480;
    engine_midi_audition_retire(engine);
    assert(!engine->midi_audition_notes.note_count && !engine->midi_audition_tail_until);
    atomic_store(&engine->transport_playing, false);
    engine->transport_frame = 0;
    guarded = false;
    assert(heap_calls == 0);
    assert(engine->transport_frame == 0);
    engine_destroy(engine);
}

// Compares the stopped audition's live gain ramp with the same unscaled production voice.
static void stopped_gain_transition(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 256;
    Engine* engine = engine_create(&config);
    assert(engine);
    assert(engine_add_midi_clip_to_track(engine, 0, 0, 48000, NULL));
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    engine_midi_audition_apply_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 69, 1);
    float expected[480] = {0}, actual[480] = {0}, scratch[480] = {0};
    engine_instrument_source_render(engine->midi_audition_source, expected, 240, 0);
    atomic_store(&engine->worker_running,
                 true); // Deterministic worker-boundary publication without a thread.
    assert(engine_track_set_gain(engine, 0, 0));
    engine_mix_midi_audition_only(engine, 0, 240, actual, scratch, 2);
    for (int i = 0; i < 240; ++i)
        for (int ch = 0; ch < 2; ++ch)
            assert(fabsf(actual[i * 2 + ch] - expected[i * 2 + ch] * (1 - (i + 1) / 240.0f)) < 2e-6f);
    assert(actual[478] == 0 && actual[479] == 0);
    atomic_store(&engine->worker_running, false);
    engine_destroy(engine);
}

// Uses the actual dummy callback and render worker to prove releases eventually stop idle processing.
static void live_idle_retirement(void) {
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    config.sample_rate = 48000;
    config.block_size = 128;
    Engine* engine = engine_create(&config);
    assert(engine && engine_add_midi_clip_to_track(engine, 0, 0, 48000, NULL) && engine_start(engine));
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    p.release_ms = 10;
    assert(engine_midi_audition_note_on(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE, p, 69, 1));
    SDL_Delay(50);
    assert(engine_midi_audition_note_off(engine, 69));
    SDL_Delay(3000);
    uint64_t rendered = atomic_load(&engine->diag_render_blocks);
    assert(rendered > 100);
    SDL_Delay(100);
    assert(atomic_load(&engine->diag_render_blocks) == rendered);
    engine_stop(engine);
    assert(engine->transport_frame == 0 && !engine->midi_audition_notes.note_count);
    engine_destroy(engine);
}

// Runs the bounded S3.6 contracts against production rendering and audition implementations.
int main(void) {
    envelopes(44100);
    envelopes(48000);
    envelopes(96000);
    aliases();
    signal_automation();
    audition();
    stopped_gain_transition();
    live_idle_retirement();
    fprintf(stderr, "instrument_lifecycle_test: success (gate/release, early-off, partitions, alias "
                    "reduction, automation, bounded voices, retirement, panic, zero guarded heap calls)\n");
    return 0;
}
