#pragma once

#include "engine/automation.h"
#include "engine/graph.h"
#include "engine/midi.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct EngineInstrumentSource EngineInstrumentSource;

typedef enum {
    ENGINE_INSTRUMENT_PRESET_PURE_SINE = 0,
    ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE,
    ENGINE_INSTRUMENT_PRESET_SAW_LEAD,
    ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS,
    ENGINE_INSTRUMENT_PRESET_SYNTH_LAB,
    ENGINE_INSTRUMENT_PRESET_SOFT_PAD,
    ENGINE_INSTRUMENT_PRESET_PLUCK,
    ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD,
    ENGINE_INSTRUMENT_PRESET_WARM_KEYS,
    ENGINE_INSTRUMENT_PRESET_SUB_DRONE,
    // Adds rounded oscillator voices without renumbering existing saved preset IDs.
    ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS,
    ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK,
    ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD,
    ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS,
    ENGINE_INSTRUMENT_PRESET_FILTERED_PAD,
    ENGINE_INSTRUMENT_PRESET_SWEEP_BASS,
    ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK,
    ENGINE_INSTRUMENT_PRESET_MOVING_PAD,
    // Appends percussion identities without changing existing saved voices.
    ENGINE_INSTRUMENT_PRESET_KICK_DRUM,
    ENGINE_INSTRUMENT_PRESET_SNARE_DRUM,
    ENGINE_INSTRUMENT_PRESET_CLOSED_HAT,
    ENGINE_INSTRUMENT_PRESET_SUB_808,
    ENGINE_INSTRUMENT_PRESET_DRIVEN_808,
    ENGINE_INSTRUMENT_PRESET_OPEN_HAT,
    ENGINE_INSTRUMENT_PRESET_HAT_KIT,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_TRUMPET,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_CLARINET,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_SAXOPHONE,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_FLUTE,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_CELLO_SECTION,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_OBOE,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_FRENCH_HORN,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_BASSOON,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_TENOR_TROMBONE,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLA_SECTION,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_DOUBLE_BASS,
    ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN_SECTION,
    ENGINE_INSTRUMENT_PRESET_COUNT
} EngineInstrumentPresetId;

typedef enum {
    ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC = 0,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_DRUMS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_STRINGS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_BRASS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_WOODWINDS,
    ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT
} EngineInstrumentPresetCategoryId;

typedef enum {
    ENGINE_INSTRUMENT_PARAM_LEVEL = 0,
    ENGINE_INSTRUMENT_PARAM_TONE,
    ENGINE_INSTRUMENT_PARAM_ATTACK_MS,
    ENGINE_INSTRUMENT_PARAM_RELEASE_MS,
    ENGINE_INSTRUMENT_PARAM_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_SUSTAIN,
    ENGINE_INSTRUMENT_PARAM_OSC_MIX,
    ENGINE_INSTRUMENT_PARAM_OSC2_DETUNE,
    ENGINE_INSTRUMENT_PARAM_SUB_MIX,
    ENGINE_INSTRUMENT_PARAM_DRIVE,
    ENGINE_INSTRUMENT_PARAM_VIBRATO_RATE,
    ENGINE_INSTRUMENT_PARAM_VIBRATO_DEPTH,
    ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ,
    ENGINE_INSTRUMENT_PARAM_RESONANCE_Q,
    ENGINE_INSTRUMENT_PARAM_FILTER_ENV_OCTAVES,
    ENGINE_INSTRUMENT_PARAM_FILTER_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_FILTER_LFO_HZ,
    ENGINE_INSTRUMENT_PARAM_FILTER_LFO_OCTAVES,
    ENGINE_INSTRUMENT_PARAM_BODY_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_PITCH_ENV_SEMITONES,
    ENGINE_INSTRUMENT_PARAM_PITCH_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_NOISE_MIX,
    ENGINE_INSTRUMENT_PARAM_NOISE_DECAY_MS,
    ENGINE_INSTRUMENT_PARAM_CLICK_MIX,
    ENGINE_INSTRUMENT_PARAM_MONO,
    ENGINE_INSTRUMENT_PARAM_GLIDE_MS,
    ENGINE_INSTRUMENT_PARAM_HAT_CHOKE,
    ENGINE_INSTRUMENT_PARAM_DYNAMICS,
    ENGINE_INSTRUMENT_PARAM_VELOCITY_DEPTH,
    ENGINE_INSTRUMENT_PARAM_SUSTAIN_PEDAL,
    ENGINE_INSTRUMENT_PARAM_STRING_RESONANCE,
    ENGINE_INSTRUMENT_PARAM_RELEASE_NOISE,
    ENGINE_INSTRUMENT_PARAM_COUNT
} EngineInstrumentParamId;

typedef enum {
    ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT = 0,
    ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR,
    ENGINE_INSTRUMENT_PARAM_GROUP_TONE,
    ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE,
    ENGINE_INSTRUMENT_PARAM_GROUP_MOD,
    ENGINE_INSTRUMENT_PARAM_GROUP_COUNT
} EngineInstrumentParamGroupId;

// Describes the control-space mapping without changing stored physical parameter values.
typedef enum EngineInstrumentParamScale {
    ENGINE_INSTRUMENT_PARAM_SCALE_LINEAR = 0,
    ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME,
    ENGINE_INSTRUMENT_PARAM_SCALE_LOG_FREQUENCY
} EngineInstrumentParamScale;

// Describes one stable instrument parameter and its presentation mapping.
typedef struct {
    const char* id;
    const char* display_name;
    const char* unit;
    float min_value;
    float max_value;
    float default_value;
    EngineInstrumentParamGroupId group;
    int display_order;
    EngineInstrumentParamScale scale;
} EngineInstrumentParamSpec;

// Stores the complete physical sound settings, including dormant controls for portable recall.
typedef struct {
    float level;
    float tone;
    float attack_ms;
    float release_ms;
    float decay_ms;
    float sustain;
    float osc_mix;
    float osc2_detune;
    float sub_mix;
    float drive;
    float vibrato_rate;
    float vibrato_depth;
    float cutoff_hz;
    float resonance_q;
    float filter_env_octaves;
    float filter_decay_ms;
    float filter_lfo_hz;
    float filter_lfo_octaves;
    float body_decay_ms;
    float pitch_env_semitones;
    float pitch_decay_ms;
    float noise_mix;
    float noise_decay_ms;
    float click_mix;
    float mono;
    float glide_ms;
    float hat_choke;
    float dynamics;
    float velocity_depth;
    float sustain_pedal;
    float string_resonance;
    float release_noise;



} EngineInstrumentParams;

EngineInstrumentPresetId engine_instrument_preset_clamp(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_id_string(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_display_name(EngineInstrumentPresetId preset);
EngineInstrumentPresetCategoryId engine_instrument_preset_category(EngineInstrumentPresetId preset);
const char* engine_instrument_preset_category_display_name(EngineInstrumentPresetCategoryId category);
bool engine_instrument_preset_from_id_string(const char* id, EngineInstrumentPresetId* out_preset);
int engine_instrument_preset_count(void);
int engine_instrument_preset_category_count(void);
int engine_instrument_param_count(void);
int engine_instrument_param_group_count(void);
const char* engine_instrument_param_group_display_name(EngineInstrumentParamGroupId group);
bool engine_instrument_param_spec(EngineInstrumentParamId param, EngineInstrumentParamSpec* out_spec);
bool engine_instrument_param_from_id_string(const char* id, EngineInstrumentParamId* out_param);
int engine_instrument_preset_param_count(EngineInstrumentPresetId preset);
bool engine_instrument_preset_param_id_at(EngineInstrumentPresetId preset,
                                          int index,
                                          EngineInstrumentParamId* out_param);
EngineInstrumentParams engine_instrument_default_params(EngineInstrumentPresetId preset);
EngineInstrumentParams engine_instrument_params_sanitize(EngineInstrumentPresetId preset,
                                                         EngineInstrumentParams params);
float engine_instrument_params_get(EngineInstrumentParams params, EngineInstrumentParamId param);
EngineInstrumentParams engine_instrument_params_set(EngineInstrumentPresetId preset,
                                                    EngineInstrumentParams params,
                                                    EngineInstrumentParamId param,
                                                    float value);
bool engine_instrument_param_from_automation_target(EngineAutomationTarget target,
                                                    EngineInstrumentParamId* out_param);
EngineAutomationTarget engine_instrument_param_automation_target(EngineInstrumentParamId param);
EngineInstrumentParams engine_instrument_params_apply_automation_delta(EngineInstrumentPresetId preset,
                                                                       EngineInstrumentParams params,
                                                                       EngineInstrumentParamId param,
                                                                       float delta);
EngineInstrumentSource* engine_instrument_source_create(void);
// Changes a live piano pedal without replacing voices or restarting their release envelopes.
void engine_instrument_source_piano_pedal(EngineInstrumentSource*, bool, uint64_t);
// Reads a prepared live damper time for audition retirement.
uint64_t engine_instrument_source_piano_gate(const EngineInstrumentSource*, uint64_t, uint8_t);
void engine_instrument_source_destroy(EngineInstrumentSource* instrument);
bool engine_instrument_source_set_midi_clip(EngineInstrumentSource* instrument,
                                            uint64_t timeline_start_frame,
                                            uint64_t clip_duration_frames,
                                            EngineInstrumentPresetId preset,
                                            EngineInstrumentParams params,
                                            const EngineMidiNote* notes,
                                            int note_count,
                                            const EngineAutomationLane* track_automation_lanes,
                                            int track_automation_lane_count,
                                            const EngineAutomationLane* automation_lanes,
                                            int automation_lane_count);
void engine_instrument_source_reset(void* userdata, int sample_rate, int channels);
void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame);
void engine_instrument_source_ops(EngineGraphSourceOps* ops);

// Copies region-relative track choke fences and enables bounded natural tails for new hat voices.
bool engine_instrument_source_set_hat_chokes(EngineInstrumentSource*, const uint64_t* frames, int count);

// Reserves note descriptors off-thread for bounded live audition updates.
bool engine_instrument_source_reserve_notes(EngineInstrumentSource* instrument, int capacity);

// Closes captured note gates and permits a bounded release tail without changing authored lane endpoints.
void engine_instrument_source_set_export_end(EngineInstrumentSource* instrument, uint64_t end, uint64_t tail);

// Reports whether a parameter participates in the selected preset's DSP.
bool engine_instrument_preset_param_active(EngineInstrumentPresetId preset, EngineInstrumentParamId param);
// Reports whether a preset has any controls in the requested group.
bool engine_instrument_preset_group_active(EngineInstrumentPresetId preset, EngineInstrumentParamGroupId group);
// Maps physical values to bounded control positions using the parameter's declared scale.
float engine_instrument_param_to_normalized(EngineInstrumentParamId param, float value);
// Maps bounded control positions back to physical values without changing automation semantics.
float engine_instrument_param_from_normalized(EngineInstrumentParamId param, float position);
// Evaluates an isolated note with the same oscillator, envelope and modulation as the audio source.
float engine_instrument_note_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params,
    uint8_t note, float velocity, int sample_rate, uint64_t relative_frame, uint64_t gate_frames);
// Evaluates the real gate-aware ADSR, including early note-off, for diagnostic plots.
float engine_instrument_envelope_level(EngineInstrumentParams params, int sample_rate,
    uint64_t relative_frame, uint64_t gate_frames);

// Samples a bounded contiguous diagnostic stream, including filter history from note onset.
bool engine_instrument_note_waveform(EngineInstrumentPresetId preset, EngineInstrumentParams params,
    uint8_t note, float velocity, int rate, uint64_t start, uint64_t span, uint64_t gate, float* output, int count);
