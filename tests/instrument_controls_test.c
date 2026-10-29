#include "engine/instrument.h"
#include "test_assert.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(v, m) daw_test_expect("instrument_controls_test", (v), (m))

// Checks time mapping precision, exact endpoints and roundtrips across every declared control.
static void control_mapping(void) {
    for (int id = 0; id < ENGINE_INSTRUMENT_PARAM_COUNT; ++id) {
        EngineInstrumentParamSpec spec;
        CHECK(engine_instrument_param_spec((EngineInstrumentParamId)id, &spec), "parameter specification");
        CHECK(engine_instrument_param_from_normalized(id, 0) == spec.min_value &&
            engine_instrument_param_from_normalized(id, 1) == spec.max_value, "exact physical endpoints");
        float last = spec.min_value;
        for (int step = 0; step <= 1000; ++step) {
            float t = (float)step / 1000;
            float value = engine_instrument_param_from_normalized(id, t);
            CHECK(value >= last && value >= spec.min_value && value <= spec.max_value, "monotonic bounded mapping");
            CHECK(fabsf(engine_instrument_param_to_normalized(id, value) - t) < 0.00001f, "mapping roundtrip");
            last = value;
        }
    }
    CHECK(engine_instrument_param_from_normalized(ENGINE_INSTRUMENT_PARAM_ATTACK_MS, .5f) < 16,
        "half travel must preserve short attack precision");
    CHECK(engine_instrument_param_to_normalized(ENGINE_INSTRUMENT_PARAM_ATTACK_MS, NAN) == 0, "nonfinite control");
}

// Uses independent expected DSP dependency sets to catch advertised dormant controls.
static void applicability(void) {
    const unsigned base = 0x3f;
    const unsigned osc_mix = 1u << ENGINE_INSTRUMENT_PARAM_OSC_MIX;
    const unsigned detune = 1u << ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE;
    const unsigned sub = 1u << ENGINE_INSTRUMENT_PARAM_SUB_MIX;
    const unsigned drive = 1u << ENGINE_INSTRUMENT_PARAM_DRIVE;
    const unsigned expected[] = {base, base, base, base, 0xfff, base|osc_mix|detune|sub,
        base|osc_mix|drive, base|drive, base|sub, base|sub|drive, base|osc_mix|detune|sub|drive,
        base|osc_mix|detune|sub|drive, base|osc_mix|detune|sub|drive};
    for (int preset = 0; preset < ENGINE_INSTRUMENT_PRESET_COUNT; ++preset) {
        unsigned actual = 0;
        for (int i = 0; i < engine_instrument_preset_param_count(preset); ++i) {
            EngineInstrumentParamId id;
            CHECK(engine_instrument_preset_param_id_at(preset, i, &id), "visible parameter enumeration");
            CHECK(!(actual & (1u << id)), "unique visible controls");
            actual |= 1u << id;
        }
        CHECK(actual == expected[preset], "preset DSP dependency set");
        CHECK(engine_instrument_preset_group_active(preset, ENGINE_INSTRUMENT_PARAM_GROUP_MOD) ==
            (preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB), "modulation applicability");
    }
    CHECK(engine_instrument_preset_param_count(ENGINE_INSTRUMENT_PRESET_COUNT) == 0, "invalid preset");
}

// Proves diagnostic samples match actual source rendering across presets, formats and early note-off.
static void preview_parity(void) {
    const int rates[] = {44100, 48000, 96000};
    for (int r = 0; r < 3; ++r) for (int preset = 0; preset < ENGINE_INSTRUMENT_PRESET_COUNT; ++preset) {
        EngineInstrumentParams params = engine_instrument_default_params(preset);
        params.attack_ms = 4; params.decay_ms = 6; params.sustain = .37f; params.release_ms = 10;
        params.vibrato_depth = 24;
        for (int mode = 0; mode < 2; ++mode) {
            uint64_t gate = mode ? 900 : 75;
            EngineInstrumentSource* source = engine_instrument_source_create();
            EngineMidiNote note = {.start_frame = 0, .duration_frames = gate, .note = 60, .velocity = .73f};
            CHECK(source && engine_instrument_source_set_midi_clip(source, 0, 2400, preset, params,
                &note, 1, NULL, 0, NULL, 0), "preview source");
            engine_instrument_source_reset(source, rates[r], 1);
            float rendered[2400] = {0};
            engine_instrument_source_render(source, rendered, 2400, 0);
            for (int i = 0; i < 2400; ++i) {
                float sampled = engine_instrument_note_sample(preset, params, 60, .73f, rates[r], i, gate);
                CHECK(sampled == rendered[i], "diagnostic/source exact sample parity");
            }
            engine_instrument_source_destroy(source);
        }
    }
    EngineInstrumentParams params = engine_instrument_default_params(0);
    params.attack_ms = 20; params.decay_ms = 20; params.sustain = .4f; params.release_ms = 10;
    CHECK(fabsf(engine_instrument_envelope_level(params, 48000, 120, 240) - .125f) < 1e-6, "physical attack timing");
    CHECK(fabsf(engine_instrument_envelope_level(params, 48000, 480, 240) - .125f) < 1e-6, "early-off release level");
    CHECK(engine_instrument_envelope_level(params, 48000, 720, 240) == 0, "release endpoint");
}

// Checks rounded oscillator harmonic content against independent Fourier amplitudes and upper-note limits.
static void rounded_spectrum(void) {
    const double pi = 3.14159265358979323846;
    EngineInstrumentParams params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS);
    params.level = 1; params.attack_ms = 0; params.decay_ms = 0; params.sustain = 1;
    params.osc_mix = 0; params.sub_mix = 0; params.drive = 0;
    double amplitudes[2][3] = {{0}};
    for (int mode = 0; mode < 2; ++mode) {
        params.tone = (float)mode;
        for (int i = 0; i < 4410; ++i) {
            float sample = engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS,
                params, 69, 1, 44100, i, 44100);
            for (int h = 0; h < 3; ++h)
                amplitudes[mode][h] += sample * sin(2 * pi * 440 * (2*h+1) * i / 44100) * 2 / 4410;
        }
    }
    CHECK(fabs(amplitudes[0][0] - .18) < 1e-6 && fabs(amplitudes[0][1]) < 1e-6,
        "zero tone/drive must produce a pure sine");
    CHECK(fabs(amplitudes[1][0] - .18 * 8 / (pi*pi)) < 1e-6, "triangle fundamental amplitude");
    CHECK(fabs(amplitudes[1][1] / amplitudes[1][0] + 1.0/9) < 1e-6 &&
        fabs(amplitudes[1][2] / amplitudes[1][0] - 1.0/25) < 1e-6, "triangle odd harmonic shape");
    const int rates[] = {44100, 48000};
    double frequency = 440 * pow(2.0, (127.0 - 69) / 12);
    for (int r = 0; r < 2; ++r) for (int i = 0; i < 4096; ++i) {
        float sample = engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS,
            params, 127, 1, rates[r], i, rates[r]);
        double expected = .18 * 8 / (pi*pi) * sin(2*pi*frequency*i/rates[r]);
        CHECK(fabs(sample - expected) < 1e-7, "upper-note triangle must omit out-of-band harmonics");
    }
    for (int preset = ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS; preset <= ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD; ++preset) {
        EngineInstrumentPresetId parsed;
        CHECK(engine_instrument_preset_from_id_string(engine_instrument_preset_id_string(preset), &parsed) && parsed == preset,
            "new preset stable string identity");
    }
}

// Verifies every advertised rounded control changes real samples and extreme settings retain per-voice headroom.
static void rounded_controls(void) {
    EngineInstrumentParams base = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS);
    base.osc_mix = .3f;
    for (int index = 0; index < engine_instrument_preset_param_count(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS); ++index) {
        EngineInstrumentParamId id;
        EngineInstrumentParamSpec spec;
        CHECK(engine_instrument_preset_param_id_at(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS, index, &id) &&
            engine_instrument_param_spec(id, &spec), "rounded control enumeration");
        EngineInstrumentParams low = engine_instrument_params_set(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS, base, id, spec.min_value);
        EngineInstrumentParams high = engine_instrument_params_set(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS, base, id, spec.max_value);
        double difference = 0;
        for (int i = 0; i < 40000; i += 37) {
            float a = engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS, low, 60, .8f, 48000, i, 12000);
            float b = engine_instrument_note_sample(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS, high, 60, .8f, 48000, i, 12000);
            CHECK(isfinite(a) && isfinite(b) && fabsf(a) < .5f && fabsf(b) < .5f, "rounded extreme control headroom");
            difference += fabsf(a - b);
        }
        CHECK(difference > .001, "advertised rounded control must change rendered audio");
    }
}

// Validates the instrument presentation contract without opening audio devices or user projects.
int main(void) {
    control_mapping(); applicability(); preview_parity(); rounded_spectrum(); rounded_controls();
    puts("instrument_controls_test: success (mapping, DSP applicability, exact voice parity, ADSR timing)");
    return 0;
}
