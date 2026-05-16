#include "session.h"
#include "engine/engine.h"

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

static const char* kTestOutputPath = "build/tests/sample_session.json";

typedef struct LibraryBrowser LibraryBrowser;

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    (void)engine;
    return NULL;
}

bool engine_transport_is_playing(const Engine* engine) {
    (void)engine;
    return false;
}

uint64_t engine_get_transport_frame(const Engine* engine) {
    (void)engine;
    return 0;
}

const EngineTrack* engine_get_tracks(const Engine* engine) {
    (void)engine;
    return NULL;
}

const char* engine_clip_get_media_id(const EngineClip* clip) {
    (void)clip;
    return NULL;
}

const char* engine_clip_get_media_path(const EngineClip* clip) {
    (void)clip;
    return NULL;
}

int engine_get_track_count(const Engine* engine) {
    (void)engine;
    return 0;
}

Engine* engine_create(const EngineRuntimeConfig* cfg) {
    (void)cfg;
    return (Engine*)0x1;
}

void engine_destroy(Engine* engine) {
    (void)engine;
}

void engine_stop(Engine* engine) {
    (void)engine;
}

bool engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame) {
    (void)engine;
    (void)enabled;
    (void)start_frame;
    (void)end_frame;
    return true;
}

bool engine_transport_stop(Engine* engine) {
    (void)engine;
    return true;
}

bool engine_transport_seek(Engine* engine, uint64_t frame) {
    (void)engine;
    (void)frame;
    return true;
}

int engine_add_track(Engine* engine) {
    static int next_track = 0;
    (void)engine;
    return next_track++;
}

bool engine_track_set_name(Engine* engine, int track_index, const char* name) {
    (void)engine;
    (void)track_index;
    (void)name;
    return true;
}

bool engine_track_set_gain(Engine* engine, int track_index, float gain) {
    (void)engine;
    (void)track_index;
    (void)gain;
    return true;
}

bool engine_track_set_muted(Engine* engine, int track_index, bool muted) {
    (void)engine;
    (void)track_index;
    (void)muted;
    return true;
}

bool engine_track_set_solo(Engine* engine, int track_index, bool solo) {
    (void)engine;
    (void)track_index;
    (void)solo;
    return true;
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    static int next_clip = 0;
    (void)engine;
    (void)track_index;
    (void)filepath;
    (void)start_frame;
    if (out_clip_index) {
        *out_clip_index = next_clip++;
    }
    return true;
}

bool engine_add_midi_clip_to_track(Engine* engine,
                                   int track_index,
                                   uint64_t start_frame,
                                   uint64_t duration_frames,
                                   int* out_clip_index) {
    static int next_clip = 1000;
    (void)engine;
    (void)track_index;
    (void)start_frame;
    (void)duration_frames;
    if (out_clip_index) {
        *out_clip_index = next_clip++;
    }
    return true;
}

bool engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)offset_frames;
    (void)duration_frames;
    return true;
}

EngineClipKind engine_clip_get_kind(const EngineClip* clip) {
    return clip ? clip->kind : ENGINE_CLIP_KIND_AUDIO;
}

bool engine_clip_midi_add_note(Engine* engine,
                               int track_index,
                               int clip_index,
                               EngineMidiNote note,
                               int* out_note_index) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)note;
    if (out_note_index) {
        *out_note_index = 0;
    }
    return true;
}

int engine_clip_midi_note_count(const EngineClip* clip) {
    return clip ? clip->midi_notes.note_count : 0;
}

const EngineMidiNote* engine_clip_midi_notes(const EngineClip* clip) {
    return clip ? clip->midi_notes.notes : NULL;
}

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset) {
    if (preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return preset;
}

const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        return "synth_lab";
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return "soft_pad";
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return "pluck";
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return "bright_lead";
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return "warm_keys";
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return "sub_drone";
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
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        return "Custom Synth";
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return "Soft Pad";
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return "Pluck";
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return "Bright Lead";
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return "Warm Keys";
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return "Sub Drone";
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

EngineInstrumentPresetCategoryId engine_instrument_preset_category(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS;
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD;
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS;
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS;
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK;
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        return ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC;
    }
}

const char* engine_instrument_preset_category_display_name(EngineInstrumentPresetCategoryId category) {
    switch (category) {
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC:
        return "Basic";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS:
        return "Bass";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD:
        return "Lead";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS:
        return "Keys";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS:
        return "Pads";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK:
        return "Pluck";
    case ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT:
    default:
        return "";
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

int engine_instrument_preset_category_count(void) {
    return ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
}

int engine_instrument_param_count(void) {
    return ENGINE_INSTRUMENT_PARAM_COUNT;
}

int engine_instrument_param_group_count(void) {
    return ENGINE_INSTRUMENT_PARAM_GROUP_COUNT;
}

const char* engine_instrument_param_group_display_name(EngineInstrumentParamGroupId group) {
    switch (group) {
    case ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT:
        return "Output";
    case ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR:
        return "Osc";
    case ENGINE_INSTRUMENT_PARAM_GROUP_TONE:
        return "Tone";
    case ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE:
        return "Env";
    case ENGINE_INSTRUMENT_PARAM_GROUP_MOD:
        return "Mod";
    case ENGINE_INSTRUMENT_PARAM_GROUP_COUNT:
    default:
        return "";
    }
}

bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec) {
    if (!out_spec) {
        return false;
    }
    switch (param) {
    case ENGINE_INSTRUMENT_PARAM_LEVEL:
        *out_spec = (EngineInstrumentParamSpec){"level", "Level", "", 0.0f, 1.5f, 1.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_TONE:
        *out_spec = (EngineInstrumentParamSpec){"tone", "Tone", "", 0.0f, 1.0f, 0.5f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_ATTACK_MS:
        *out_spec = (EngineInstrumentParamSpec){"attack_ms", "Attack", "ms", 0.0f, 250.0f, 4.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_RELEASE_MS:
        *out_spec = (EngineInstrumentParamSpec){"release_ms", "Release", "ms", 0.0f, 500.0f, 18.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 3};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        *out_spec = (EngineInstrumentParamSpec){"decay_ms", "Decay", "ms", 0.0f, 800.0f, 120.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        *out_spec = (EngineInstrumentParamSpec){"sustain", "Sustain", "", 0.0f, 1.0f, 0.78f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE, 2};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        *out_spec = (EngineInstrumentParamSpec){"osc_mix", "Osc Mix", "", 0.0f, 1.0f, 0.45f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        *out_spec = (EngineInstrumentParamSpec){"osc2_detune", "Detune", "ct", -24.0f, 24.0f, 7.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        *out_spec = (EngineInstrumentParamSpec){"sub_mix", "Sub", "", 0.0f, 1.0f, 0.22f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR, 2};
        return true;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        *out_spec = (EngineInstrumentParamSpec){"drive", "Drive", "", 0.0f, 1.0f, 0.18f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_TONE, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_rate", "Rate", "Hz", 0.0f, 12.0f, 5.2f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 0};
        return true;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        *out_spec = (EngineInstrumentParamSpec){"vibrato_depth", "Depth", "ct", 0.0f, 60.0f, 0.0f,
                                                ENGINE_INSTRUMENT_PARAM_GROUP_MOD, 1};
        return true;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        return false;
    }
}

bool engine_instrument_param_from_id_string(const char* id, EngineInstrumentParamId* out_param) {
    if (!id || !out_param) {
        return false;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec = {0};
        if (engine_instrument_param_spec((EngineInstrumentParamId)i, &spec) &&
            spec.id && strcmp(id, spec.id) == 0) {
            *out_param = (EngineInstrumentParamId)i;
            return true;
        }
    }
    return false;
}

int engine_instrument_preset_param_count(EngineInstrumentPresetId preset) {
    (void)preset;
    return ENGINE_INSTRUMENT_PARAM_COUNT;
}

bool engine_instrument_preset_param_id_at(EngineInstrumentPresetId preset,
                                          int index,
                                          EngineInstrumentParamId* out_param) {
    (void)preset;
    if (!out_param || index < 0 || index >= ENGINE_INSTRUMENT_PARAM_COUNT) {
        return false;
    }
    *out_param = (EngineInstrumentParamId)index;
    return true;
}

EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset) {
    EngineInstrumentParams params = {
        .level = 1.0f,
        .tone = 0.5f,
        .attack_ms = 4.0f,
        .release_ms = 18.0f,
        .decay_ms = 120.0f,
        .sustain = 1.0f,
        .osc_mix = 0.0f,
        .osc2_detune = 0.0f,
        .sub_mix = 0.0f,
        .drive = 0.0f,
        .vibrato_rate = 5.2f,
        .vibrato_depth = 0.0f
    };
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
        params.level = 0.72f;
        params.tone = 0.64f;
        params.attack_ms = 4.0f;
        params.release_ms = 60.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
        params.level = 0.86f;
        params.tone = 0.30f;
        params.attack_ms = 8.0f;
        params.release_ms = 100.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
        params.level = 0.72f;
        params.tone = 0.54f;
        params.attack_ms = 8.0f;
        params.decay_ms = 180.0f;
        params.sustain = 0.68f;
        params.release_ms = 140.0f;
        params.osc_mix = 0.42f;
        params.osc2_detune = 5.0f;
        params.sub_mix = 0.16f;
        params.drive = 0.12f;
        params.vibrato_rate = 5.2f;
        params.vibrato_depth = 4.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
        params.level = 0.62f;
        params.tone = 0.44f;
        params.attack_ms = 160.0f;
        params.decay_ms = 420.0f;
        params.sustain = 0.86f;
        params.release_ms = 420.0f;
        params.osc_mix = 0.22f;
        params.osc2_detune = 4.0f;
        params.sub_mix = 0.10f;
        params.drive = 0.02f;
        params.vibrato_depth = 3.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
        params.level = 0.72f;
        params.tone = 0.58f;
        params.attack_ms = 2.0f;
        params.decay_ms = 150.0f;
        params.sustain = 0.14f;
        params.release_ms = 110.0f;
        params.osc_mix = 0.28f;
        params.osc2_detune = 2.0f;
        params.drive = 0.04f;
        break;
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
        params.level = 0.64f;
        params.tone = 0.72f;
        params.attack_ms = 3.0f;
        params.decay_ms = 150.0f;
        params.sustain = 0.66f;
        params.release_ms = 150.0f;
        params.osc_mix = 0.64f;
        params.osc2_detune = 7.0f;
        params.sub_mix = 0.04f;
        params.drive = 0.14f;
        params.vibrato_depth = 5.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
        params.level = 0.70f;
        params.tone = 0.38f;
        params.attack_ms = 12.0f;
        params.decay_ms = 240.0f;
        params.sustain = 0.58f;
        params.release_ms = 220.0f;
        params.osc_mix = 0.14f;
        params.osc2_detune = -3.0f;
        params.sub_mix = 0.08f;
        params.drive = 0.03f;
        params.vibrato_depth = 1.5f;
        break;
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        params.level = 0.60f;
        params.tone = 0.22f;
        params.attack_ms = 120.0f;
        params.decay_ms = 520.0f;
        params.sustain = 0.92f;
        params.release_ms = 480.0f;
        params.osc_mix = 0.10f;
        params.osc2_detune = -5.0f;
        params.sub_mix = 0.70f;
        params.drive = 0.06f;
        params.vibrato_rate = 2.2f;
        params.vibrato_depth = 2.0f;
        break;
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
    case ENGINE_INSTRUMENT_PRESET_COUNT:
    default:
        break;
    }
    return params;
}

EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params) {
    (void)preset;
    if (params.level < 0.0f) params.level = 0.0f;
    if (params.level > 1.5f) params.level = 1.5f;
    if (params.tone < 0.0f) params.tone = 0.0f;
    if (params.tone > 1.0f) params.tone = 1.0f;
    if (params.attack_ms < 0.0f) params.attack_ms = 0.0f;
    if (params.attack_ms > 250.0f) params.attack_ms = 250.0f;
    if (params.release_ms < 0.0f) params.release_ms = 0.0f;
    if (params.release_ms > 500.0f) params.release_ms = 500.0f;
    if (params.decay_ms < 0.0f) params.decay_ms = 0.0f;
    if (params.decay_ms > 800.0f) params.decay_ms = 800.0f;
    if (params.sustain < 0.0f) params.sustain = 0.0f;
    if (params.sustain > 1.0f) params.sustain = 1.0f;
    if (params.osc_mix < 0.0f) params.osc_mix = 0.0f;
    if (params.osc_mix > 1.0f) params.osc_mix = 1.0f;
    if (params.osc2_detune < -24.0f) params.osc2_detune = -24.0f;
    if (params.osc2_detune > 24.0f) params.osc2_detune = 24.0f;
    if (params.sub_mix < 0.0f) params.sub_mix = 0.0f;
    if (params.sub_mix > 1.0f) params.sub_mix = 1.0f;
    if (params.drive < 0.0f) params.drive = 0.0f;
    if (params.drive > 1.0f) params.drive = 1.0f;
    if (params.vibrato_rate < 0.0f) params.vibrato_rate = 0.0f;
    if (params.vibrato_rate > 12.0f) params.vibrato_rate = 12.0f;
    if (params.vibrato_depth < 0.0f) params.vibrato_depth = 0.0f;
    if (params.vibrato_depth > 60.0f) params.vibrato_depth = 60.0f;
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
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        return params.decay_ms;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        return params.sustain;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        return params.osc_mix;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        return params.osc2_detune;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        return params.sub_mix;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        return params.drive;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        return params.vibrato_rate;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        return params.vibrato_depth;
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
    case ENGINE_INSTRUMENT_PARAM_DECAY_MS:
        params.decay_ms = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_SUSTAIN:
        params.sustain = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_OSC_MIX:
        params.osc_mix = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE:
        params.osc2_detune = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_SUB_MIX:
        params.sub_mix = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_DRIVE:
        params.drive = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE:
        params.vibrato_rate = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH:
        params.vibrato_depth = value;
        break;
    case ENGINE_INSTRUMENT_PARAM_COUNT:
    default:
        break;
    }
    return engine_instrument_params_sanitize(preset, params);
}

EngineInstrumentPresetId engine_clip_midi_instrument_preset(const EngineClip* clip) {
    return clip ? engine_instrument_preset_clamp(clip->instrument_preset) : ENGINE_INSTRUMENT_PRESET_PURE_SINE;
}

EngineInstrumentParams engine_clip_midi_instrument_params(const EngineClip* clip) {
    return clip ? engine_instrument_params_sanitize(clip->instrument_preset, clip->instrument_params)
                : engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
}

bool engine_clip_midi_inherits_track_instrument(const EngineClip* clip) {
    return clip ? clip->instrument_inherits_track : false;
}

bool engine_track_midi_instrument_enabled(const Engine* engine, int track_index) {
    (void)engine;
    (void)track_index;
    return false;
}

EngineInstrumentPresetId engine_track_midi_instrument_preset(const Engine* engine, int track_index) {
    (void)engine;
    (void)track_index;
    return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
}

EngineInstrumentParams engine_track_midi_instrument_params(const Engine* engine, int track_index) {
    (void)engine;
    (void)track_index;
    return engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
}

bool engine_track_midi_set_instrument_preset(Engine* engine,
                                             int track_index,
                                             EngineInstrumentPresetId preset) {
    (void)engine;
    (void)track_index;
    (void)preset;
    return true;
}

bool engine_track_midi_set_instrument_params(Engine* engine,
                                             int track_index,
                                             EngineInstrumentParams params) {
    (void)engine;
    (void)track_index;
    (void)params;
    return true;
}

bool engine_track_midi_set_instrument_enabled(Engine* engine, int track_index, bool enabled) {
    (void)engine;
    (void)track_index;
    (void)enabled;
    return true;
}

bool engine_track_midi_get_instrument_automation_lanes(const Engine* engine,
                                                       int track_index,
                                                       const EngineAutomationLane** out_lanes,
                                                       int* out_lane_count) {
    (void)engine;
    (void)track_index;
    if (out_lanes) {
        *out_lanes = NULL;
    }
    if (out_lane_count) {
        *out_lane_count = 0;
    }
    return true;
}

bool engine_track_midi_set_instrument_automation_lanes(Engine* engine,
                                                       int track_index,
                                                       const EngineAutomationLane* lanes,
                                                       int lane_count) {
    (void)engine;
    (void)track_index;
    (void)lanes;
    (void)lane_count;
    return true;
}

bool engine_track_midi_set_instrument_automation_lane_points(Engine* engine,
                                                             int track_index,
                                                             EngineAutomationTarget target,
                                                             const EngineAutomationPoint* points,
                                                             int count) {
    (void)engine;
    (void)track_index;
    (void)target;
    (void)points;
    (void)count;
    return true;
}

bool engine_clip_midi_set_inherits_track_instrument(Engine* engine,
                                                    int track_index,
                                                    int clip_index,
                                                    bool inherits_track) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)inherits_track;
    return true;
}

EngineInstrumentPresetId engine_clip_midi_effective_instrument_preset(const Engine* engine,
                                                                      int track_index,
                                                                      int clip_index) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
}

EngineInstrumentParams engine_clip_midi_effective_instrument_params(const Engine* engine,
                                                                    int track_index,
                                                                    int clip_index) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    return engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
}

bool engine_clip_midi_set_instrument_preset(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentPresetId preset) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)preset;
    return true;
}

bool engine_clip_midi_set_instrument_params(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentParams params) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)params;
    return true;
}

bool engine_clip_midi_set_instrument_param(Engine* engine,
                                           int track_index,
                                           int clip_index,
                                           EngineInstrumentParamId param,
                                           float value) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)param;
    (void)value;
    return true;
}

bool engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)gain;
    return true;
}

bool engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)name;
    return true;
}

bool engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)fade_in_frames;
    (void)fade_out_frames;
    return true;
}

bool engine_clip_set_automation_lane_points(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineAutomationTarget target,
                                            const EngineAutomationPoint* points,
                                            int count) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)target;
    (void)points;
    (void)count;
    return true;
}

bool engine_clip_set_automation_lanes(Engine* engine,
                                      int track_index,
                                      int clip_index,
                                      const EngineAutomationLane* lanes,
                                      int lane_count) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)lanes;
    (void)lane_count;
    return true;
}

bool engine_remove_track(Engine* engine, int track_index) {
    (void)engine;
    (void)track_index;
    return true;
}

bool engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot) {
    (void)engine;
    if (out_snapshot) {
        SDL_zero(*out_snapshot);
    }
    return true;
}

bool engine_fx_track_snapshot(const Engine* engine, int track_index, FxMasterSnapshot* out_snapshot) {
    (void)engine;
    (void)track_index;
    if (out_snapshot) {
        SDL_zero(*out_snapshot);
    }
    return true;
}

bool engine_fx_registry_get_desc(const Engine* engine, FxTypeId type, FxDesc* out_desc) {
    (void)engine;
    (void)type;
    if (out_desc) {
        SDL_zero(*out_desc);
    }
    return false;
}

bool engine_fx_registry_get_param_specs(const Engine* engine,
                                        FxTypeId type,
                                        const EffectParamSpec** out_specs,
                                        uint32_t* out_count) {
    (void)engine;
    (void)type;
    if (out_specs) {
        *out_specs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    return false;
}

FxInstId engine_fx_master_add(Engine* engine, FxTypeId type) {
    (void)engine;
    (void)type;
    return (FxInstId)1;
}

bool engine_fx_master_remove(Engine* engine, FxInstId id) {
    (void)engine;
    (void)id;
    return true;
}

bool engine_fx_master_set_param(Engine* engine, FxInstId id, uint32_t param_index, float value) {
    (void)engine;
    (void)id;
    (void)param_index;
    (void)value;
    return true;
}

bool engine_fx_master_set_enabled(Engine* engine, FxInstId id, bool enabled) {
    (void)engine;
    (void)id;
    (void)enabled;
    return true;
}

void library_browser_init(LibraryBrowser* browser, const char* directory) {
    (void)browser;
    (void)directory;
}

void library_browser_scan(LibraryBrowser* browser) {
    (void)browser;
}

void tempo_state_clamp(TempoState* tempo) {
    (void)tempo;
}

const TempoEvent* tempo_map_event_at_beat(const TempoMap* map, double beat) {
    (void)map;
    (void)beat;
    return NULL;
}

const TimeSignatureEvent* time_signature_map_event_at_beat(const TimeSignatureMap* map, double beat) {
    (void)map;
    (void)beat;
    return NULL;
}

static bool write_replaced_file(const char* source_path,
                                const char* target_path,
                                const char* needle,
                                const char* replacement) {
    FILE* source = fopen(source_path, "rb");
    if (!source) {
        return false;
    }
    if (fseek(source, 0, SEEK_END) != 0) {
        fclose(source);
        return false;
    }
    long size = ftell(source);
    if (size < 0 || fseek(source, 0, SEEK_SET) != 0) {
        fclose(source);
        return false;
    }
    char* data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(source);
        return false;
    }
    size_t read_count = fread(data, 1, (size_t)size, source);
    fclose(source);
    if (read_count != (size_t)size) {
        free(data);
        return false;
    }
    data[size] = '\0';

    char* match = strstr(data, needle);
    if (!match) {
        free(data);
        return false;
    }
    size_t prefix_len = (size_t)(match - data);
    size_t needle_len = strlen(needle);
    size_t replacement_len = strlen(replacement);
    size_t suffix_len = (size_t)size - prefix_len - needle_len;
    size_t out_len = prefix_len + replacement_len + suffix_len;
    char* out = (char*)malloc(out_len + 1u);
    if (!out) {
        free(data);
        return false;
    }
    memcpy(out, data, prefix_len);
    memcpy(out + prefix_len, replacement, replacement_len);
    memcpy(out + prefix_len + replacement_len, match + needle_len, suffix_len);
    out[out_len] = '\0';

    FILE* target = fopen(target_path, "wb");
    if (!target) {
        free(out);
        free(data);
        return false;
    }
    size_t write_count = fwrite(out, 1, out_len, target);
    fclose(target);
    free(out);
    free(data);
    return write_count == out_len;
}

static int run_factory_preset_session_matrix_test(void) {
    const char* path = "build/tests/factory_presets_session.json";
    SessionDocument doc;
    session_document_init(&doc);
    doc.engine.sample_rate = 48000;
    doc.engine.block_size = 128;
    doc.track_count = 1;
    doc.tracks = (SessionTrack*)calloc(1, sizeof(SessionTrack));
    if (!doc.tracks) {
        SDL_Log("session_serialization_test: failed to allocate preset matrix track");
        session_document_free(&doc);
        return 20;
    }
    SessionTrack* track = &doc.tracks[0];
    strncpy(track->name, "Factory Presets", sizeof(track->name) - 1);
    track->gain = 1.0f;
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_SOFT_PAD;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    track->midi_instrument_params.level = 0.61f;
    track->midi_instrument_params.tone = 0.37f;
    track->clip_count = ENGINE_INSTRUMENT_PRESET_COUNT;
    track->clips = (SessionClip*)calloc((size_t)track->clip_count, sizeof(SessionClip));
    if (!track->clips) {
        SDL_Log("session_serialization_test: failed to allocate preset matrix clips");
        session_document_free(&doc);
        return 21;
    }

    for (int i = 0; i < track->clip_count; ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        SessionClip* clip = &track->clips[i];
        clip->kind = ENGINE_CLIP_KIND_MIDI;
        snprintf(clip->name, sizeof(clip->name), "Preset %d", i);
        clip->start_frame = (uint64_t)i * 48000u;
        clip->duration_frames = 48000;
        clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
        clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
        clip->gain = 1.0f;
        clip->instrument_preset = preset;
        clip->instrument_params = engine_instrument_default_params(preset);
        clip->instrument_inherits_track = (i == 0);
        clip->instrument_params.level = 0.50f + 0.03f * (float)i;
        clip->instrument_params.tone = 0.20f + 0.04f * (float)i;
        clip->instrument_params.attack_ms = 2.0f + (float)i;
        clip->instrument_params.release_ms = 40.0f + 3.0f * (float)i;
        clip->midi_note_count = 1;
        clip->midi_notes = (EngineMidiNote*)calloc(1, sizeof(EngineMidiNote));
        if (!clip->midi_notes) {
            SDL_Log("session_serialization_test: failed to allocate preset matrix note");
            session_document_free(&doc);
            return 22;
        }
        clip->midi_notes[0] = (EngineMidiNote){0, 12000, (uint8_t)(48 + i), 0.7f};
    }

    if (!session_document_write_file(&doc, path)) {
        SDL_Log("session_serialization_test: failed to write preset matrix");
        session_document_free(&doc);
        return 23;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(path, &loaded)) {
        SDL_Log("session_serialization_test: failed to read preset matrix");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 24;
    }
    if (loaded.track_count != 1 ||
        loaded.tracks[0].clip_count != ENGINE_INSTRUMENT_PRESET_COUNT) {
        SDL_Log("session_serialization_test: preset matrix count mismatch");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 25;
    }
    if (!loaded.tracks[0].midi_instrument_enabled ||
        loaded.tracks[0].midi_instrument_preset != ENGINE_INSTRUMENT_PRESET_SOFT_PAD ||
        fabsf(loaded.tracks[0].midi_instrument_params.level - 0.61f) > 0.01f ||
        fabsf(loaded.tracks[0].midi_instrument_params.tone - 0.37f) > 0.01f) {
        SDL_Log("session_serialization_test: track MIDI instrument default mismatch");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 26;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        const SessionClip* clip = &loaded.tracks[0].clips[i];
        if (clip->kind != ENGINE_CLIP_KIND_MIDI ||
            clip->instrument_preset != (EngineInstrumentPresetId)i ||
            clip->instrument_inherits_track != (i == 0) ||
            fabsf(clip->instrument_params.level - (0.50f + 0.03f * (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.tone - (0.20f + 0.04f * (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.attack_ms - (2.0f + (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.release_ms - (40.0f + 3.0f * (float)i)) > 0.01f ||
            clip->midi_note_count != 1 ||
            !clip->midi_notes ||
            clip->midi_notes[0].note != (uint8_t)(48 + i)) {
            SDL_Log("session_serialization_test: preset matrix clip %d mismatch", i);
            session_document_free(&doc);
            session_document_free(&loaded);
            return 27;
        }
    }
    session_document_free(&doc);
    session_document_free(&loaded);
    return 0;
}

static int run_unknown_preset_fallback_test(void) {
    const char* path = "build/tests/sample_session_unknown_preset.json";
    if (!write_replaced_file(kTestOutputPath, path, "\"saw_lead\"", "\"future_missing_preset\"")) {
        SDL_Log("session_serialization_test: failed to write unknown preset fixture");
        return 30;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(path, &loaded)) {
        SDL_Log("session_serialization_test: failed to read unknown preset fixture");
        session_document_free(&loaded);
        return 31;
    }
    if (loaded.track_count != 1 || loaded.tracks[0].clip_count != 2) {
        SDL_Log("session_serialization_test: unknown preset fixture count mismatch");
        session_document_free(&loaded);
        return 32;
    }
    const SessionClip* clip = &loaded.tracks[0].clips[1];
    if (clip->kind != ENGINE_CLIP_KIND_MIDI ||
        clip->instrument_preset != ENGINE_INSTRUMENT_PRESET_PURE_SINE ||
        fabsf(clip->instrument_params.level - 0.82f) > 0.01f ||
        fabsf(clip->instrument_params.tone - 0.73f) > 0.01f ||
        fabsf(clip->instrument_params.attack_ms - 12.0f) > 0.01f ||
        fabsf(clip->instrument_params.release_ms - 120.0f) > 0.01f ||
        clip->midi_note_count != 2 ||
        !clip->midi_notes ||
        clip->midi_notes[0].note != 60 ||
        clip->midi_notes[1].note != 64) {
        SDL_Log("session_serialization_test: unknown preset fallback mismatch");
        session_document_free(&loaded);
        return 33;
    }
    session_document_free(&loaded);
    return 0;
}

int main(void) {
    if (mkdir("build", 0755) != 0 && errno != EEXIST) {
        SDL_Log("session_serialization_test: failed to create build directory");
        return 1;
    }
    if (mkdir("build/tests", 0755) != 0 && errno != EEXIST) {
        SDL_Log("session_serialization_test: failed to create build/tests directory");
        return 1;
    }

    SessionDocument doc;
    session_document_init(&doc);

    doc.engine.sample_rate = 48000;
    doc.engine.block_size = 128;
    doc.engine.default_fade_in_ms = 5.0f;
    doc.engine.default_fade_out_ms = 15.0f;
    doc.engine.fade_preset_count = 3;
    doc.engine.fade_preset_ms[0] = 0.0f;
    doc.engine.fade_preset_ms[1] = 12.5f;
    doc.engine.fade_preset_ms[2] = 55.0f;
    for (int i = doc.engine.fade_preset_count; i < CONFIG_FADE_PRESET_MAX; ++i) {
        doc.engine.fade_preset_ms[i] = 0.0f;
    }
    doc.engine.enable_engine_logs = true;
    doc.engine.enable_cache_logs = false;
    doc.engine.enable_timing_logs = true;

    doc.loop.enabled = false;
    doc.loop.start_frame = 0;
    doc.loop.end_frame = 0;

    doc.timeline.visible_seconds = 8.0f;
    doc.timeline.vertical_scale = 1.0f;
    doc.timeline.show_all_grid_lines = false;
    doc.timeline.playhead_frame = 0;
    doc.selected_track_index = 0;
    doc.selected_clip_index = 1;
    doc.midi_editor.panel_mode = 1;
    doc.midi_editor.instrument_active_group = ENGINE_INSTRUMENT_PARAM_GROUP_MOD;

    doc.layout.transport_ratio = 0.3f;
    doc.layout.library_ratio = 0.25f;
    doc.layout.mixer_ratio = 0.45f;

    strncpy(doc.library.directory, "assets/audio", sizeof(doc.library.directory) - 1);
    doc.library.directory[sizeof(doc.library.directory) - 1] = '\0';
    doc.library.selected_index = 0;

    doc.transport_playing = false;
    doc.transport_frame = 0;

    doc.track_count = 1;
    doc.tracks = (SessionTrack*)calloc(1, sizeof(SessionTrack));
    if (!doc.tracks) {
        SDL_Log("session_serialization_test: failed to allocate track array");
        session_document_free(&doc);
        return 1;
    }

    SessionTrack* track = &doc.tracks[0];
    strncpy(track->name, "Test Track", sizeof(track->name) - 1);
    track->name[sizeof(track->name) - 1] = '\0';
    track->gain = 0.8f;
    track->muted = false;
    track->solo = false;
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_SOFT_PAD;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    track->midi_instrument_params.level = 0.66f;
    track->midi_instrument_automation_lane_count = 1;
    track->midi_instrument_automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!track->midi_instrument_automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate track MIDI automation lanes");
        session_document_free(&doc);
        return 2;
    }
    track->midi_instrument_automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL;
    track->midi_instrument_automation_lanes[0].point_count = 2;
    track->midi_instrument_automation_lanes[0].points =
        (SessionAutomationPoint*)calloc(2, sizeof(SessionAutomationPoint));
    if (!track->midi_instrument_automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate track MIDI automation points");
        session_document_free(&doc);
        return 2;
    }
    track->midi_instrument_automation_lanes[0].points[0].frame = 0;
    track->midi_instrument_automation_lanes[0].points[0].value = -0.4f;
    track->midi_instrument_automation_lanes[0].points[1].frame = 192000;
    track->midi_instrument_automation_lanes[0].points[1].value = 0.35f;
    track->clip_count = 2;
    track->clips = (SessionClip*)calloc(2, sizeof(SessionClip));
    if (!track->clips) {
        SDL_Log("session_serialization_test: failed to allocate clip array");
        session_document_free(&doc);
        return 2;
    }

    SessionClip* clip = &track->clips[0];
    strncpy(clip->name, "Test Clip", sizeof(clip->name) - 1);
    clip->name[sizeof(clip->name) - 1] = '\0';
    strncpy(clip->media_path, "assets/audio/test.wav", sizeof(clip->media_path) - 1);
    clip->media_path[sizeof(clip->media_path) - 1] = '\0';
    clip->start_frame = 0;
    clip->duration_frames = 48000;
    clip->offset_frames = 0;
    clip->fade_in_frames = 1200;
    clip->fade_out_frames = 2400;
    clip->automation_lane_count = 1;
    clip->automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!clip->automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate automation lanes");
        session_document_free(&doc);
        return 2;
    }
    clip->automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_VOLUME;
    clip->automation_lanes[0].point_count = 1;
    clip->automation_lanes[0].points = (SessionAutomationPoint*)calloc(1, sizeof(SessionAutomationPoint));
    if (!clip->automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate automation points");
        session_document_free(&doc);
        return 2;
    }
    clip->automation_lanes[0].points[0].frame = 24000;
    clip->automation_lanes[0].points[0].value = 0.5f;
    clip->gain = 1.0f;
    clip->selected = false;

    SessionClip* midi_clip = &track->clips[1];
    midi_clip->kind = ENGINE_CLIP_KIND_MIDI;
    strncpy(midi_clip->name, "Test MIDI Region", sizeof(midi_clip->name) - 1);
    midi_clip->name[sizeof(midi_clip->name) - 1] = '\0';
    midi_clip->start_frame = 96000;
    midi_clip->duration_frames = 192000;
    midi_clip->offset_frames = 0;
    midi_clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    midi_clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    midi_clip->gain = 0.75f;
    midi_clip->instrument_preset = ENGINE_INSTRUMENT_PRESET_SAW_LEAD;
    midi_clip->instrument_params = engine_instrument_default_params(midi_clip->instrument_preset);
    midi_clip->instrument_params.level = 0.82f;
    midi_clip->instrument_params.tone = 0.73f;
    midi_clip->instrument_params.attack_ms = 12.0f;
    midi_clip->instrument_params.release_ms = 120.0f;
    midi_clip->instrument_params.decay_ms = 180.0f;
    midi_clip->instrument_params.sustain = 0.64f;
    midi_clip->instrument_params.osc_mix = 0.55f;
    midi_clip->instrument_params.osc2_detune = 9.0f;
    midi_clip->instrument_params.sub_mix = 0.20f;
    midi_clip->instrument_params.drive = 0.33f;
    midi_clip->instrument_params.vibrato_rate = 5.5f;
    midi_clip->instrument_params.vibrato_depth = 11.0f;
    midi_clip->midi_note_count = 2;
    midi_clip->midi_notes = (EngineMidiNote*)calloc(2, sizeof(EngineMidiNote));
    if (!midi_clip->midi_notes) {
        SDL_Log("session_serialization_test: failed to allocate MIDI notes");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->midi_notes[0] = (EngineMidiNote){0, 24000, 60, 0.9f};
    midi_clip->midi_notes[1] = (EngineMidiNote){48000, 12000, 64, 0.6f};
    midi_clip->automation_lane_count = 1;
    midi_clip->automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!midi_clip->automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate MIDI automation lanes");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE;
    midi_clip->automation_lanes[0].point_count = 2;
    midi_clip->automation_lanes[0].points = (SessionAutomationPoint*)calloc(2, sizeof(SessionAutomationPoint));
    if (!midi_clip->automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate MIDI automation points");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->automation_lanes[0].points[0].frame = 0;
    midi_clip->automation_lanes[0].points[0].value = -0.25f;
    midi_clip->automation_lanes[0].points[1].frame = 96000;
    midi_clip->automation_lanes[0].points[1].value = 0.5f;

    SDL_Log("session_serialization_test: writing %s", kTestOutputPath);
    bool ok = session_document_write_file(&doc, kTestOutputPath);
    if (!ok) {
        session_document_free(&doc);
        SDL_Log("session_serialization_test: failed to write session file");
        return 3;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(kTestOutputPath, &loaded)) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: failed to read session file");
        return 4;
    }

    if (loaded.track_count != 1 || loaded.tracks[0].clip_count != 2) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised counts mismatch");
        return 5;
    }
    if (fabsf(loaded.engine.default_fade_in_ms - doc.engine.default_fade_in_ms) > 0.01f ||
        fabsf(loaded.engine.default_fade_out_ms - doc.engine.default_fade_out_ms) > 0.01f ||
        loaded.engine.fade_preset_count != doc.engine.fade_preset_count) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine fade config mismatch");
        return 6;
    }
    for (int i = 0; i < loaded.engine.fade_preset_count; ++i) {
        if (fabsf(loaded.engine.fade_preset_ms[i] - doc.engine.fade_preset_ms[i]) > 0.01f) {
            session_document_free(&doc);
            session_document_free(&loaded);
            SDL_Log("session_serialization_test: fade preset %d mismatch", i);
            return 7;
        }
    }
    if (loaded.engine.enable_engine_logs != doc.engine.enable_engine_logs ||
        loaded.engine.enable_cache_logs != doc.engine.enable_cache_logs ||
        loaded.engine.enable_timing_logs != doc.engine.enable_timing_logs) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine logging flags mismatch");
        return 8;
    }
    if (loaded.selected_track_index != 0 ||
        loaded.selected_clip_index != 1 ||
        loaded.midi_editor.panel_mode != 1 ||
        loaded.midi_editor.instrument_active_group != ENGINE_INSTRUMENT_PARAM_GROUP_MOD) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: MIDI editor view state mismatch");
        return 8;
    }
    SessionTrack* lt = &loaded.tracks[0];
    SessionClip* lc = &lt->clips[0];
    if (!lt->midi_instrument_enabled ||
        lt->midi_instrument_preset != ENGINE_INSTRUMENT_PRESET_SOFT_PAD ||
        fabsf(lt->midi_instrument_params.level - 0.66f) > 0.01f ||
        lt->midi_instrument_automation_lane_count != 1 ||
        !lt->midi_instrument_automation_lanes ||
        lt->midi_instrument_automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL ||
        lt->midi_instrument_automation_lanes[0].point_count != 2 ||
        !lt->midi_instrument_automation_lanes[0].points ||
        lt->midi_instrument_automation_lanes[0].points[1].frame != 192000 ||
        fabsf(lt->midi_instrument_automation_lanes[0].points[1].value - 0.35f) > 0.01f) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: track MIDI automation mismatch");
        return 9;
    }
    if (lc->kind != ENGINE_CLIP_KIND_AUDIO ||
        strcmp(lt->name, "Test Track") != 0 || strcmp(lc->name, "Test Clip") != 0 ||
        strcmp(lc->media_path, "assets/audio/test.wav") != 0 || lc->duration_frames != 48000 ||
        lc->fade_in_frames != 1200 || lc->fade_out_frames != 2400) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised content mismatch");
        return 9;
    }
    if (lc->automation_lane_count != 1 ||
        lc->automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_VOLUME ||
        lc->automation_lanes[0].point_count != 1 ||
        lc->automation_lanes[0].points[0].frame != 24000) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: automation lanes mismatch");
        return 9;
    }
    SessionClip* lm = &lt->clips[1];
    if (lm->kind != ENGINE_CLIP_KIND_MIDI ||
        strcmp(lm->name, "Test MIDI Region") != 0 ||
        lm->media_path[0] != '\0' ||
        lm->media_id[0] != '\0' ||
        lm->start_frame != 96000 ||
        lm->duration_frames != 192000 ||
        lm->instrument_preset != ENGINE_INSTRUMENT_PRESET_SAW_LEAD ||
        fabsf(lm->instrument_params.level - 0.82f) > 0.01f ||
        fabsf(lm->instrument_params.tone - 0.73f) > 0.01f ||
        fabsf(lm->instrument_params.attack_ms - 12.0f) > 0.01f ||
        fabsf(lm->instrument_params.release_ms - 120.0f) > 0.01f ||
        fabsf(lm->instrument_params.decay_ms - 180.0f) > 0.01f ||
        fabsf(lm->instrument_params.sustain - 0.64f) > 0.01f ||
        fabsf(lm->instrument_params.osc_mix - 0.55f) > 0.01f ||
        fabsf(lm->instrument_params.osc2_detune - 9.0f) > 0.01f ||
        fabsf(lm->instrument_params.sub_mix - 0.20f) > 0.01f ||
        fabsf(lm->instrument_params.drive - 0.33f) > 0.01f ||
        fabsf(lm->instrument_params.vibrato_rate - 5.5f) > 0.01f ||
        fabsf(lm->instrument_params.vibrato_depth - 11.0f) > 0.01f ||
        lm->midi_note_count != 2 ||
        !lm->midi_notes ||
        lm->midi_notes[0].note != 60 ||
        lm->midi_notes[1].note != 64 ||
        fabsf(lm->midi_notes[0].velocity - 0.9f) > 0.01f ||
        lm->automation_lane_count != 1 ||
        !lm->automation_lanes ||
        lm->automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE ||
        lm->automation_lanes[0].point_count != 2 ||
        !lm->automation_lanes[0].points ||
        lm->automation_lanes[0].points[1].frame != 96000 ||
        fabsf(lm->automation_lanes[0].points[1].value - 0.5f) > 0.01f) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: MIDI clip mismatch");
        return 9;
    }

    session_document_free(&doc);
    session_document_free(&loaded);

    FILE* file = fopen(kTestOutputPath, "rb");
    if (!file) {
        SDL_Log("session_serialization_test: output file missing");
        return 10;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        SDL_Log("session_serialization_test: failed to seek output file");
        fclose(file);
        return 11;
    }
    long size = ftell(file);
    fclose(file);
    if (size <= 0) {
        SDL_Log("session_serialization_test: output file empty");
        return 12;
    }

    int factory_result = run_factory_preset_session_matrix_test();
    if (factory_result != 0) {
        return factory_result;
    }
    int fallback_result = run_unknown_preset_fallback_test();
    if (fallback_result != 0) {
        return fallback_result;
    }

    SDL_Log("session_serialization_test: success (%ld bytes)", size);
    return 0;
}
