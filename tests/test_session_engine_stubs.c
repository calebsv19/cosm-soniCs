#include "test_session_engine_stubs.h"

#include "session.h"
#include "daw/data_paths.h"
#include "engine/engine.h"
#include "ui/library_browser.h"

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void daw_test_session_engine_stubs_link_anchor(void) {
}

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    (void)engine;
    return NULL;
}

const char* daw_data_paths_library_root(const DawDataPaths* paths) {
    if (paths && paths->input_root[0] != '\0') {
        return paths->input_root;
    }
    return DAW_DATA_PATH_DEFAULT_INPUT_ROOT;
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
    (void)directory;
    if (!browser) {
        return;
    }
    SDL_zero(*browser);
}

void library_browser_scan(LibraryBrowser* browser, MediaRegistry* registry) {
    (void)browser;
    (void)registry;
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
