#include "engine/instrument.h"
#include "engine/instrument_articulation.h"
#include "engine/instrument_percussion.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x, m) daw_test_expect("instrument_articulation_test", (x), (m))

// Supplies fixed test controls independently from the application's automation callback.
static EngineInstrumentParams fixed_params(void* context, uint64_t frame) {
    (void)frame;
    return *(EngineInstrumentParams*)context;
}

// Proves logarithmic glide and integrated phase against an independent dense numerical trajectory.
static void pitch_and_selection(void) {
    const int rate = 96000;
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_SUB_808);
    p.mono = 1; p.glide_ms = 120; p.pitch_env_semitones = 0;
    EngineMidiNote notes[] = {{12000, 40000, 48, .8f}, {0, 24000, 36, .8f}, {60000, 1000, 40, .8f}};
    EngineArticulationTimeline t = {0};
    CHECK(engine_articulation_reserve(&t, 3), "reserved events");
    engine_articulation_prepare(&t, notes, 3, rate, fixed_params, &p);
    CHECK(t.events[0].note_index == 1 && t.events[1].note_index == 0, "unsorted notes order deterministically");
    CHECK(t.events[1].legato && !t.events[2].legato, "overlap glides and separated notes retrigger");
    double low = 440 * pow(2, -33.0 / 12), high = low * 2;
    double phase = 0, previous = low;
    for (uint64_t frame = 1; frame < 50000; ++frame) {
        const EngineArticulationEvent* e = engine_articulation_event_at(&t, frame);
        double fraction = frame < 12000 ? 0 : fmin(1, (double)(frame - 12000) / (rate * .12));
        double expected = low * pow(high / low, fraction);
        phase += (previous + expected) * .5 / rate;
        previous = expected;
        double hz, cycles; engine_articulation_pitch(e, rate, frame, &hz, &cycles);
        CHECK(fabs(hz - expected) < 1e-8 && fabs(cycles - phase) < 1e-6, "independent pitch and continuous integrated phase");
    }
    p.glide_ms = 0;
    engine_articulation_retime(&t, rate, fixed_params, &p);
    double hz, cycles; engine_articulation_pitch(&t.events[1], rate, 12000, &hz, &cycles);
    CHECK(fabs(hz - high) < 1e-8 && fabs(cycles - low * .125) < 1e-8, "zero glide steps pitch but preserves phase");
    notes[0].start_frame = 0;
    engine_articulation_prepare(&t, notes, 3, rate, fixed_params, &p);
    CHECK(engine_articulation_event_at(&t, 0)->note_index == 1, "last authored simultaneous note wins");
    CHECK(!engine_articulation_event_at(&t, 0)->legato, "simultaneous notes do not start a fictitious glide");
    engine_articulation_free(&t);
}

// Checks production partition/seek parity for mono bass and mixed open/closed hat patterns.
static void render_partitions(void) {
    const int rates[] = {44100, 48000, 96000};
    const EngineInstrumentPresetId voices[] = {ENGINE_INSTRUMENT_PRESET_SUB_808, ENGINE_INSTRUMENT_PRESET_DRIVEN_808, ENGINE_INSTRUMENT_PRESET_HAT_KIT};
    for (int r = 0; r < 3; ++r) for (int v = 0; v < 3; ++v) {
        int rate = rates[r], length = rate;
        EngineInstrumentParams p = engine_instrument_default_params(voices[v]);
        p.mono = 1; p.glide_ms = 120;
        EngineMidiNote notes[] = {{0, rate / 3, v == 2 ? 46 : 36, .8f}, {rate / 4, rate / 3, v == 2 ? 42 : 48, .7f}, {rate * 3 / 4, 48, v == 2 ? 46 : 40, .8f}};
        EngineInstrumentSource* source = engine_instrument_source_create();
        CHECK(source && engine_instrument_source_set_midi_clip(source, 0, length, voices[v], p, notes, 3, NULL, 0, NULL, 0), "prepared production pattern");
        float* whole = calloc(length, sizeof(float)); float* split = calloc(length, sizeof(float));
        CHECK(whole && split, "test buffers");
        engine_instrument_source_reset(source, rate, 1); engine_instrument_source_render(source, whole, length, 0);
        engine_instrument_source_reset(source, rate, 1);
        for (int at = 0; at < length; at += 127) engine_instrument_source_render(source, split + at, length - at < 127 ? length - at : 127, at);
        CHECK(!memcmp(whole, split, length * sizeof(float)), "exact block partition parity");
        float seek[128] = {0}; int at = rate / 4 + 100;
        engine_instrument_source_reset(source, rate, 1); engine_instrument_source_render(source, seek, 128, at);
        CHECK(!memcmp(seek, whole + at, sizeof(seek)), "seek into glide or choke matches continuous render");
        for (int i = 0; i < length; ++i) CHECK(isfinite(whole[i]) && fabsf(whole[i]) < 1, "finite unclipped signal");
        if (v == 2) {
            CHECK(whole[rate / 4 + rate / 8] == 0, "closed hat chokes open tail and completes its own short decay");
            p.hat_choke = 0;
            CHECK(engine_instrument_source_set_midi_clip(source, 0, length, voices[v], p, notes, 3, NULL, 0, NULL, 0), "disable choke");
            memset(split, 0, length * sizeof(float)); engine_instrument_source_reset(source, rate, 1); engine_instrument_source_render(source, split, length, 0);
            CHECK(fabsf(split[rate / 4 + rate / 8]) > 1e-7, "unchoked open tail remains audible");
        } else {
            p.mono = 0;
            CHECK(engine_instrument_source_set_midi_clip(source, 0, length, voices[v], p, notes, 3, NULL, 0, NULL, 0), "polyphonic compatibility");
            memset(split, 0, length * sizeof(float)); engine_instrument_source_reset(source, rate, 1); engine_instrument_source_render(source, split, length, 0);
            CHECK(memcmp(whole, split, length * sizeof(float)), "mono mode audibly differs from layered notes");
        }
        free(whole); free(split); engine_instrument_source_destroy(source);
    }
}

// Verifies export does not retain a choke event belonging to an excluded later note.
static void export_choke_boundary(void) {
    EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_HAT_KIT);
    p.noise_decay_ms = 900;
    EngineMidiNote notes[] = {{0, 48, 46, .8f}, {12000, 48, 42, .8f}};
    EngineInstrumentSource* source = engine_instrument_source_create();
    CHECK(source && engine_instrument_source_set_midi_clip(source, 0, 48000, ENGINE_INSTRUMENT_PRESET_HAT_KIT, p, notes, 2, NULL, 0, NULL, 0), "export hat preparation");
    engine_instrument_source_reset(source, 48000, 1);
    engine_instrument_source_set_export_end(source, 10000, 24000);
    float actual[128] = {0};
    engine_instrument_source_render(source, actual, 128, 13000);
    for (int i = 0; i < 128; ++i)
        CHECK(actual[i] == engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_HAT_KIT, p, 46, .8f, 48000, 13000 + i, 48), "excluded closed hit cannot choke export tail");
    engine_instrument_source_destroy(source);
}

// Runs independent pitch and production articulation checks.
int main(void) {
    pitch_and_selection(); render_partitions(); export_choke_boundary();
    puts("instrument_articulation_test: success");
    return 0;
}
