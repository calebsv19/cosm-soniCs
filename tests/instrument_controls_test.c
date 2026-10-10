#include "engine/instrument.h"
#include "test_assert.h"
#include "engine/instrument_filter.h"
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
        base|osc_mix|detune|sub|drive, base|osc_mix|detune|sub|drive, 0x3f3ff, 0x3f3ff, 0x3f3ff, 0x3f3ff, 0x3f3ff};
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
            (preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB || engine_instrument_filter_preset(preset)), "modulation applicability");
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
            if (engine_instrument_filter_preset(preset)) {
                float preview[512];
                CHECK(engine_instrument_note_waveform(preset, params, 60, .73f, rates[r], 0, 511, gate, preview, 512), "filtered streamed diagnostic");
                for (int i = 0; i < 512; ++i) CHECK(preview[i] == rendered[i], "filter diagnostic/source exact parity");
            } else {
            for (int i = 0; i < 2400; ++i) {
                float sampled = engine_instrument_note_sample(preset, params, 60, .73f, rates[r], i, gate);
                CHECK(sampled == rendered[i], "diagnostic/source exact sample parity");
            }
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

// Measures the filter against an independent bilinear low-pass magnitude response and stresses parameter jumps.
static void filter_response(void) {
    const double pi = 3.14159265358979323846;
    const int rates[] = {44100, 48000, 96000};
    const int frequencies[] = {100, 1000, 8000};
    for (int r = 0; r < 3; ++r) for (int mode = 0; mode < 3; ++mode) {
        EngineInstrumentFilter filter = {0};
        EngineInstrumentParams p = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS);
        p.cutoff_hz = 1000; p.resonance_q = .707f;
        double sine_sum = 0, cosine_sum = 0;
        for (int i = 0; i < rates[r] * 2; ++i) {
            double phase = 2*pi*frequencies[mode]*i/rates[r];
            float value = engine_instrument_filter_process(&filter, (float)(.1*sin(phase)), p, rates[r], i);
            CHECK(isfinite(value), "finite filter response");
            if (i >= rates[r]) { sine_sum += value*sin(phase); cosine_sum += value*cos(phase); }
        }
        double measured = hypot(sine_sum, cosine_sum) * 2 / rates[r] / .1;
        double u = tan(pi*frequencies[mode]/rates[r]) / tan(pi*1000/rates[r]);
        double expected = 1 / sqrt((1-u*u)*(1-u*u) + u*u/(p.resonance_q*p.resonance_q));
        CHECK(fabs(measured - expected) < 1e-5, "independent low-pass transfer magnitude");
        for (int i = 0; i < 10000; ++i) {
            p.cutoff_hz = i%2 ? 80 : 18000; p.resonance_q = i%2 ? .5f : 2;
            float value = engine_instrument_filter_process(&filter, .2f, p, rates[r], 2*rates[r]+i);
            CHECK(isfinite(value) && fabsf(value) < 2, "smoothed cutoff/resonance stability");
        }
        float reset_value = engine_instrument_filter_process(&filter, .2f, p, rates[r], 123);
        EngineInstrumentFilter fresh = {0};
        CHECK(reset_value == engine_instrument_filter_process(&fresh, .2f, p, rates[r], 123), "seek resets filter deterministically");
    }
}

// Checks independent octave-domain decay and retriggered LFO expectations before coefficient smoothing.
static void filter_modulation(void) {
    EngineInstrumentParams p=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS);
    p.cutoff_hz=1000; p.filter_env_octaves=2; p.filter_decay_ms=250;
    EngineInstrumentFilter f={0}; engine_instrument_filter_process(&f,.1f,p,48000,0);
    CHECK(f.cutoff==4000,"two-octave envelope onset");
    engine_instrument_filter_process(&f,.1f,p,48000,12000);
    CHECK(fabs(f.cutoff-1000*exp2(2*exp(-1)))<.001,"independent exponential decay");
    p.filter_env_octaves=0; p.filter_lfo_octaves=1; p.filter_lfo_hz=2;
    engine_instrument_filter_process(&f,.1f,p,48000,6000); CHECK(fabsf(f.cutoff-2000)<.001f,"LFO quarter-cycle octave rise");
    engine_instrument_filter_process(&f,.1f,p,48000,18000); CHECK(fabsf(f.cutoff-500)<.001f,"LFO opposite octave fall");
    engine_instrument_filter_process(&f,.1f,p,48000,0); CHECK(f.cutoff==1000,"LFO retriggers at note onset");
    p.filter_env_octaves=-4; p.filter_lfo_octaves=0; engine_instrument_filter_process(&f,.1f,p,48000,0); CHECK(f.cutoff==80,"negative envelope clamps physical cutoff");
    for(int id=ENGINE_INSTRUMENT_PARAM_FILTER_ENV_OCTAVES;id<ENGINE_INSTRUMENT_PARAM_COUNT;++id) {
        EngineInstrumentParamId mapped; CHECK(engine_instrument_param_from_automation_target(engine_instrument_param_automation_target(id),&mapped) && mapped==id,"additive automation identity");
    }
}

// Validates the instrument presentation contract without opening audio devices or user projects.
int main(void) {
    control_mapping(); applicability(); preview_parity(); rounded_spectrum(); rounded_controls(); filter_response(); filter_modulation();
    puts("instrument_controls_test: success (mapping, DSP applicability, exact voice parity, ADSR timing)");
    return 0;
}
