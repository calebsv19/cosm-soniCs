#include "engine/instrument.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct EngineInstrumentSource {
    uint64_t timeline_start_frame;
    uint64_t clip_duration_frames;
    EngineMidiNote* notes;
    int note_count;
    int note_capacity;
    EngineInstrumentPresetId preset;
    EngineInstrumentParams params;
    int sample_rate;
    int channels;
};

static void instrument_reset_internal(EngineInstrumentSource* instrument, int sample_rate, int channels) {
    if (!instrument) {
        return;
    }
    instrument->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    instrument->channels = channels > 0 ? channels : 2;
}

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset) {
    if (preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return preset;
}

const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return "soft_square";
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        return "saw_lead";
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        return "simple_bass";
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return "pure_sine";
    }
}

const char* engine_instrument_preset_display_name(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return "Soft Square";
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        return "Saw Lead";
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        return "Simple Bass";
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return "Pure Sine";
    }
}

bool engine_instrument_preset_from_id_string(const char* id, EngineInstrumentPresetId* out_preset) {
    if (!id || !out_preset) {
        return false;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        if (strcmp(id, engine_instrument_preset_id_string(preset)) == 0) {
            *out_preset = preset;
            return true;
        }
    }
    return false;
}

int engine_instrument_preset_count(void) {
    return ENGINE_INSTRUMENT_PRESET_COUNT;
}

int engine_instrument_param_count(void) {
    return ENGINE_INSTRUMENT_PARAM_COUNT;
}

bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec) {
    if (!out_spec) {
        return false;
    }
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        *out_spec = (EngineInstrumentParamSpec){"level", "Level", "", 0.0f, 1.5f, 1.0f};
        return true;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        *out_spec = (EngineInstrumentParamSpec){"tone", "Tone", "", 0.0f, 1.0f, 0.5f};
        return true;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        *out_spec = (EngineInstrumentParamSpec){"attack_ms", "Attack", "ms", 0.0f, 250.0f, 4.0f};
        return true;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        *out_spec = (EngineInstrumentParamSpec){"release_ms", "Release", "ms", 0.0f, 500.0f, 18.0f};
        return true;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        return false;
    }
}

static float instrument_clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset) {
    EngineInstrumentParams params = {1.0f, 0.5f, 4.0f, 18.0f};
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        params.tone = 0.62f;
        params.attack_ms = 3.0f;
        params.release_ms = 24.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        params.level = 0.9f;
        params.tone = 0.78f;
        params.attack_ms = 2.0f;
        params.release_ms = 32.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        params.level = 1.05f;
        params.tone = 0.35f;
        params.attack_ms = 8.0f;
        params.release_ms = 80.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        break;
    }
    return engine_instrument_params_sanitize(preset, params);
}

EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params) {
    (void)preset;
    EngineInstrumentParamSpec spec = {0};
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_LEVEL, &spec)) {
        params.level = instrument_clamp_float(params.level, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_TONE, &spec)) {
        params.tone = instrument_clamp_float(params.tone, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_ATTACK_MS, &spec)) {
        params.attack_ms = instrument_clamp_float(params.attack_ms, spec.min_value, spec.max_value);
    }
    if (engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_RELEASE_MS, &spec)) {
        params.release_ms = instrument_clamp_float(params.release_ms, spec.min_value, spec.max_value);
    }
    return params;
}

float engine_instrument_params_get(EngineInstrumentParams params, EngineInstrumentParamId param) {
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        return params.level;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        return params.tone;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        return params.attack_ms;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        return params.release_ms;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        return 0.0f;
    }
}

EngineInstrumentParams engine_instrument_params_set(EngineInstrumentPresetId preset,
                                                    EngineInstrumentParams params,
                                                    EngineInstrumentParamId param,
                                                    float value) {
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        params.level = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        params.tone = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        params.attack_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        params.release_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        break;
    }
    return engine_instrument_params_sanitize(preset, params);
}

EngineInstrumentSource* engine_instrument_source_create(void) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)calloc(1, sizeof(EngineInstrumentSource));
    if (!instrument) {
        return NULL;
    }
    instrument->preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    instrument->params = engine_instrument_default_params(instrument->preset);
    instrument_reset_internal(instrument, 48000, 2);
    return instrument;
}

void engine_instrument_source_destroy(EngineInstrumentSource* instrument) {
    if (!instrument) {
        return;
    }
    free(instrument->notes);
    instrument->notes = NULL;
    instrument->note_count = 0;
    instrument->note_capacity = 0;
    free(instrument);
}

bool engine_instrument_source_set_midi_clip(EngineInstrumentSource* instrument,
                                            uint64_t timeline_start_frame,
                                            uint64_t clip_duration_frames,
                                            EngineInstrumentPresetId preset,
                                            EngineInstrumentParams params,
                                            const EngineMidiNote* notes,
                                            int note_count) {
    if (!instrument || note_count < 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (note_count > 0 && !notes) {
        return false;
    }
    if (note_count > instrument->note_capacity) {
        EngineMidiNote* resized = (EngineMidiNote*)realloc(instrument->notes,
                                                           sizeof(EngineMidiNote) * (size_t)note_count);
        if (!resized) {
            return false;
        }
        instrument->notes = resized;
        instrument->note_capacity = note_count;
    }
    if (note_count > 0) {
        memcpy(instrument->notes, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    }
    instrument->note_count = note_count;
    instrument->timeline_start_frame = timeline_start_frame;
    instrument->clip_duration_frames = clip_duration_frames;
    instrument->preset = engine_instrument_preset_clamp(preset);
    instrument->params = engine_instrument_params_sanitize(instrument->preset, params);
    return true;
}

void engine_instrument_source_reset(void* userdata, int sample_rate, int channels) {
    instrument_reset_internal((EngineInstrumentSource*)userdata, sample_rate, channels);
}

static double midi_note_frequency(uint8_t note) {
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

static uint64_t instrument_ms_to_frames(float ms, int sample_rate) {
    if (ms <= 0.0f || sample_rate <= 0) {
        return 0;
    }
    double frames = ((double)ms * (double)sample_rate) / 1000.0;
    if (frames < 1.0) {
        return 1;
    }
    if (frames > (double)UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t)(frames + 0.5);
}

static float note_envelope(uint64_t rel,
                           uint64_t duration,
                           const EngineInstrumentParams* params,
                           int sample_rate) {
    if (duration == 0 || rel >= duration) {
        return 0.0f;
    }
    uint64_t attack = params ? instrument_ms_to_frames(params->attack_ms, sample_rate) : 64;
    uint64_t release = params ? instrument_ms_to_frames(params->release_ms, sample_rate) : 64;
    float scale = 1.0f;
    if (attack > 0 && rel < attack) {
        scale *= (float)rel / (float)attack;
    }
    uint64_t remaining = duration - rel;
    if (release > 0 && remaining < release) {
        scale *= (float)remaining / (float)release;
    }
    return scale;
}

static float preset_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params, double phase, double freq) {
    double sine = sin(phase);
    double tone = params.tone;
    if (tone < 0.0) tone = 0.0;
    if (tone > 1.0) tone = 1.0;
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return (float)(tanh(sine * (1.2 + tone * 3.4)) * 0.16);
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD: {
        double cycle = phase / (2.0 * M_PI);
        double saw = 2.0 * (cycle - floor(cycle + 0.5));
        return (float)(saw * (0.08 + tone * 0.10) + sine * (0.06 - tone * 0.03));
    }
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS: {
        double sub_phase = phase * 0.5;
        double second = sin(phase * 2.0);
        (void)freq;
        return (float)(sin(sub_phase) * 0.22 + second * (0.02 + tone * 0.08));
    }
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        (void)freq;
        return (float)(sine * (0.16 + tone * 0.08));
    }
}

void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)userdata;
    if (!instrument || !interleaved || frames <= 0 || instrument->sample_rate <= 0 || instrument->channels <= 0) {
        return;
    }

    const int channels = instrument->channels;
    for (int i = 0; i < frames; ++i) {
        uint64_t global_frame = transport_frame + (uint64_t)i;
        if (global_frame < instrument->timeline_start_frame) {
            continue;
        }
        uint64_t clip_frame = global_frame - instrument->timeline_start_frame;
        if (clip_frame >= instrument->clip_duration_frames) {
            continue;
        }

        float sample = 0.0f;
        for (int n = 0; n < instrument->note_count; ++n) {
            const EngineMidiNote* note = &instrument->notes[n];
            if (clip_frame < note->start_frame) {
                continue;
            }
            uint64_t rel = clip_frame - note->start_frame;
            if (rel >= note->duration_frames) {
                continue;
            }
            double freq = midi_note_frequency(note->note);
            double phase = (2.0 * M_PI * freq * (double)rel) / (double)instrument->sample_rate;
            float env = note_envelope(rel, note->duration_frames, &instrument->params, instrument->sample_rate);
            sample += preset_sample(instrument->preset, instrument->params, phase, freq) *
                      instrument->params.level *
                      note->velocity *
                      env;
        }

        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        for (int ch = 0; ch < channels; ++ch) {
            interleaved[i * channels + ch] = sample;
        }
    }
}

void engine_instrument_source_ops(EngineGraphSourceOps* ops) {
    if (!ops) {
        return;
    }
    ops->render = engine_instrument_source_render;
    ops->reset = engine_instrument_source_reset;
}
