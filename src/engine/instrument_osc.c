#include "engine/instrument.h"
#include "engine/note_ending.h"
#include "engine/instrument_samples.h"
#include "engine/piano_expression.h"
#include "engine/instrument_waveform.h"
#include "engine/instrument_filter.h"
#include "engine/instrument_percussion.h"
#include "engine/instrument_articulation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Keeps invariant pitch and reusable candidate indices beside each prepared note descriptor.
typedef struct EnginePreparedNote {
    EngineMidiNote note;
    int original_index; // Retains authored identity through private export compaction.
    double frequency;
    int candidate_index;
    uint64_t choke_frame;
    EngineInstrumentFilter filter;
    float sampler_dynamics;
    uint64_t piano_gate;
} EnginePreparedNote;

// Computes an invariant MIDI base frequency during note preparation.
static double midi_note_frequency(uint8_t note);

// Owns one authored MIDI region or prepared audition voice list and its automation.
struct EngineInstrumentSource {
    uint64_t timeline_start_frame;
    uint64_t clip_duration_frames;
    uint64_t render_duration_frames;
    uint64_t export_end_frame;
    bool track_hat_routing;
    EnginePreparedNote* notes;
    EngineArticulationTimeline articulation;
    int candidate_count;
    int note_count;
    int note_capacity;
    EngineAutomationLane* track_automation_lanes;
    int track_automation_lane_count;
    int track_automation_lane_capacity;
    EngineAutomationLane* automation_lanes;
    int automation_lane_count;
    int automation_lane_capacity;
    EngineInstrumentPresetId preset;
    EngineInstrumentParams params;
    EnginePianoExpression piano;
    bool live_pedal_down;
    int sample_rate;
    int channels;
};

// Supplies prepared articulation with the same region and track automation as normal rendering.
static EngineInstrumentParams instrument_articulation_params(void* context, uint64_t frame);

// Prepares piano dampers outside rendering using the same authored parameter evaluator.
static void instrument_prepare_piano(EngineInstrumentSource* instrument);

// Extends supported hat tails beyond region edges without changing legacy closed-hat playback.
static void instrument_hat_tail_bounds(EngineInstrumentSource* instrument) {
    if ((!instrument->track_hat_routing && !engine_instrument_sampled_preset(instrument->preset)) || instrument->export_end_frame != UINT64_MAX) return;
    uint64_t tail = (uint64_t)instrument->sample_rate * 4;
    instrument->render_duration_frames = instrument->clip_duration_frames > UINT64_MAX - tail ? UINT64_MAX : instrument->clip_duration_frames + tail;
}

static void instrument_reset_internal(EngineInstrumentSource* instrument, int sample_rate, int channels) {
    if (!instrument) {
        return;
    }
    instrument->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    instrument->channels = channels > 0 ? channels : 2;
    instrument_hat_tail_bounds(instrument);
    if (instrument->preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO) {
        engine_piano_expression_reset(&instrument->piano, instrument->sample_rate);
        instrument_prepare_piano(instrument);
    }
    for (int i = 0; i < instrument->note_count; ++i) {
        engine_instrument_filter_reset(&instrument->notes[i].filter);
        instrument->notes[i].sampler_dynamics = -1;
    }
    if (engine_instrument_percussion_808(instrument->preset) && instrument->params.mono >= .5f)
        engine_articulation_retime(&instrument->articulation, instrument->sample_rate, instrument_articulation_params, instrument);
}

static void instrument_clear_automation_lanes(EngineAutomationLane** lanes, int* lane_count, int* lane_capacity) {
    if (!lanes || !*lanes) {
        return;
    }
    int count = lane_count ? *lane_count : 0;
    for (int i = 0; i < count; ++i) {
        engine_automation_lane_free(&(*lanes)[i]);
    }
    free(*lanes);
    *lanes = NULL;
    if (lane_count) {
        *lane_count = 0;
    }
    if (lane_capacity) {
        *lane_capacity = 0;
    }
}

static bool instrument_copy_automation_lanes(EngineAutomationLane** dst_lanes,
                                             int* dst_lane_count,
                                             int* dst_lane_capacity,
                                             const EngineAutomationLane* lanes,
                                             int lane_count) {
    if (!dst_lanes || !dst_lane_count || !dst_lane_capacity) {
        return false;
    }
    instrument_clear_automation_lanes(dst_lanes, dst_lane_count, dst_lane_capacity);
    if (!lanes || lane_count <= 0) {
        return true;
    }
    *dst_lanes = (EngineAutomationLane*)calloc((size_t)lane_count, sizeof(EngineAutomationLane));
    if (!*dst_lanes) {
        return false;
    }
    *dst_lane_capacity = lane_count;
    *dst_lane_count = lane_count;
    for (int i = 0; i < lane_count; ++i) {
        engine_automation_lane_init(&(*dst_lanes)[i], lanes[i].target);
        if (!engine_automation_lane_copy(&lanes[i], &(*dst_lanes)[i])) {
            instrument_clear_automation_lanes(dst_lanes, dst_lane_count, dst_lane_capacity);
            return false;
        }
    }
    return true;
}

EngineInstrumentSource* engine_instrument_source_create(void) {
    (void)engine_instrument_samples_init();
    (void)engine_piano_expression_init();
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)calloc(1, sizeof(EngineInstrumentSource));
    if (!instrument) {
        return NULL;
    }
    instrument->preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    instrument->params = engine_instrument_default_params(instrument->preset);
    instrument_reset_internal(instrument, 48000, 2);
    return instrument;
}

// Reserves voice descriptors before live audition starts; subsequent bounded updates reuse this storage.
bool engine_instrument_source_reserve_notes(EngineInstrumentSource* instrument, int capacity) {
    if (!instrument || capacity < 0) return false;
    if (capacity <= instrument->note_capacity) return true;
    if (!engine_articulation_reserve(&instrument->articulation, capacity)) return false;
    EnginePreparedNote* notes = realloc(instrument->notes, (size_t)capacity * sizeof(*notes));
    if (!notes) return false;
    instrument->notes = notes; instrument->note_capacity = capacity;
    return true;
}

void engine_instrument_source_destroy(EngineInstrumentSource* instrument) {
    if (!instrument) {
        return;
    }
    engine_articulation_free(&instrument->articulation);
    free(instrument->notes);
    instrument->notes = NULL;
    instrument->note_count = 0;
    instrument->note_capacity = 0;
    instrument_clear_automation_lanes(&instrument->track_automation_lanes,
                                      &instrument->track_automation_lane_count,
                                      &instrument->track_automation_lane_capacity);
    instrument_clear_automation_lanes(&instrument->automation_lanes,
                                      &instrument->automation_lane_count,
                                      &instrument->automation_lane_capacity);
    free(instrument);
}

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
                                            int automation_lane_count) {
    if (!instrument || note_count < 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (note_count > 0 && !notes) {
        return false;
    }
    if (engine_instrument_sampled_preset(preset) && !engine_instrument_sample_bank_ready(preset)) return false;
    if (preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO && !engine_piano_expression_init()) return false;
    if (!engine_instrument_source_reserve_notes(instrument, note_count)) return false;
    bool live_piano = clip_duration_frames == UINT64_MAX && preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO && instrument->preset == preset;
    EngineMidiNote old_notes[256]; uint64_t old_gates[256];
    int old_count = live_piano && instrument->note_count <= 256 ? instrument->note_count : 0;
    for (int i=0;i<old_count;++i) { old_notes[i]=instrument->notes[i].note;old_gates[i]=instrument->notes[i].piano_gate; }
    for (int n = 0; n < note_count; ++n) {
        instrument->notes[n].note = notes[n];
        instrument->notes[n].original_index = n;
        instrument->notes[n].piano_gate = notes[n].duration_frames;
        if (live_piano && (instrument->live_pedal_down || params.sustain_pedal >= .5f)) instrument->notes[n].piano_gate = UINT64_MAX-notes[n].start_frame;
        for (int j=0;j<old_count;++j) if (notes[n].start_frame==old_notes[j].start_frame && notes[n].note==old_notes[j].note &&
            old_notes[j].duration_frames!=UINT64_MAX-old_notes[j].start_frame) instrument->notes[n].piano_gate=old_gates[j];
        instrument->notes[n].choke_frame = UINT64_MAX;
        instrument->notes[n].sampler_dynamics = -1;
        instrument->notes[n].frequency = midi_note_frequency(notes[n].note);
        engine_instrument_filter_reset(&instrument->notes[n].filter);
    }
    instrument->note_count = note_count;
    instrument->timeline_start_frame = timeline_start_frame;
    instrument->clip_duration_frames = clip_duration_frames;
    instrument->render_duration_frames = clip_duration_frames;
    instrument->export_end_frame = UINT64_MAX;
    instrument->track_hat_routing = false;
    instrument->preset = engine_instrument_preset_clamp(preset);
    instrument->params = engine_instrument_params_sanitize(instrument->preset, params);
    if (!instrument_copy_automation_lanes(&instrument->track_automation_lanes,
                                          &instrument->track_automation_lane_count,
                                          &instrument->track_automation_lane_capacity,
                                          track_automation_lanes,
                                          track_automation_lane_count)) {
        return false;
    }
    if (!instrument_copy_automation_lanes(&instrument->automation_lanes,
                                            &instrument->automation_lane_count,
                                            &instrument->automation_lane_capacity,
                                            automation_lanes,
                                            automation_lane_count)) return false;
    instrument_hat_tail_bounds(instrument);
    if (preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO && !live_piano) {
        engine_piano_expression_reset(&instrument->piano, instrument->sample_rate);
        instrument_prepare_piano(instrument);
    }
    instrument->articulation.count = 0;
    bool articulated = (engine_instrument_percussion_808(instrument->preset) && instrument->params.mono >= .5f) ||
        (engine_instrument_percussion_hat(instrument->preset) && instrument->params.hat_choke >= .5f);
    if (articulated) engine_articulation_prepare(&instrument->articulation, notes, note_count, instrument->sample_rate,
        instrument_articulation_params, instrument);
    for (int i = 0; i + 1 < instrument->articulation.count; ++i) {
        int index = instrument->articulation.events[i].note_index;
        instrument->notes[index].choke_frame = instrument->articulation.events[i + 1].note.start_frame;
    }
    return true;
}

// Copies immutable fences before publication; rendering never borrows the track event list.
bool engine_instrument_source_set_hat_chokes(EngineInstrumentSource* instrument, const uint64_t* frames, int count) {
    if (!instrument || count != instrument->note_count || (count && !frames)) return false;
    if (!engine_instrument_percussion_hat(instrument->preset)) return true;
    if (instrument->params.hat_choke >= .5f)
        for (int n = 0; n < count; ++n) instrument->notes[n].choke_frame = frames[n];
    instrument->track_hat_routing = instrument->preset != ENGINE_INSTRUMENT_PRESET_CLOSED_HAT || instrument->params.hat_choke >= .5f;
    instrument_hat_tail_bounds(instrument);
    return true;
}

// Gates only the captured instrument at the export boundary while preserving authored automation timing.
void engine_instrument_source_set_export_end(EngineInstrumentSource* instrument, uint64_t end, uint64_t tail) {
    if (!instrument || end <= instrument->timeline_start_frame) return;
    uint64_t cut = end - instrument->timeline_start_frame;
    if (cut > instrument->clip_duration_frames) return;
    int kept = 0;
    for (int i = 0; i < instrument->note_count; ++i) {
        EnginePreparedNote note = instrument->notes[i];
        if (note.note.start_frame >= cut) continue;
        if (note.note.duration_frames > cut - note.note.start_frame) note.note.duration_frames = cut - note.note.start_frame;
        if (note.choke_frame >= cut) note.choke_frame = UINT64_MAX;
        instrument->notes[kept++] = note;
    }
    instrument->note_count = kept;
    int events = 0;
    for (int i = 0; i < instrument->articulation.count; ++i) {
        EngineArticulationEvent e = instrument->articulation.events[i];
        if (e.note.start_frame >= cut) continue;
        if (e.note.duration_frames > cut - e.note.start_frame) e.note.duration_frames = cut - e.note.start_frame;
        instrument->articulation.events[events++] = e;
    }
    instrument->articulation.count = events;
    instrument->render_duration_frames = cut + tail;
    instrument->export_end_frame = end;
    if (instrument->preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO) instrument_prepare_piano(instrument);
}

// Captures released notes while down and sets their damper release to the actual pedal-up clock.
void engine_instrument_source_piano_pedal(EngineInstrumentSource* s,bool down,uint64_t frame) {
    if(!s || s->preset!=ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO || s->clip_duration_frames!=UINT64_MAX) return;
    if(s->live_pedal_down && !down) for(int n=0;n<s->note_count;++n) {
        EnginePreparedNote* p=&s->notes[n];
        if(p->piano_gate==UINT64_MAX-p->note.start_frame && p->note.duration_frames!=UINT64_MAX-p->note.start_frame)
            p->piano_gate=frame>p->note.start_frame?frame-p->note.start_frame:0;
    }
    s->live_pedal_down=down;
}
// Looks up the retained damper separately from the original key release.
uint64_t engine_instrument_source_piano_gate(const EngineInstrumentSource* s,uint64_t start,uint8_t note) {
    for(int i=0;s && i<s->note_count;++i) if(s->notes[i].note.start_frame==start && s->notes[i].note.note==note) return s->notes[i].piano_gate;
    return 0;
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

// Evaluates the held attack/decay/sustain level at a sample relative to note-on.
static float held_envelope(uint64_t rel, const EngineInstrumentParams* params, int rate) {
    uint64_t attack = instrument_ms_to_frames(params->attack_ms, rate);
    uint64_t decay = instrument_ms_to_frames(params->decay_ms, rate);
    if (attack && rel < attack) return (float)rel / attack;
    if (decay && rel - attack < decay) return 1 + (params->sustain - 1) * ((float)(rel-attack)/decay);
    return params->sustain;
}

// Releases from the gate's actual envelope level, including note-off during attack or decay.
static float note_envelope(uint64_t rel, uint64_t duration, const EngineInstrumentParams* params, int rate) {
    if (!duration) return 0;
    if (rel < duration) return held_envelope(rel, params, rate);
    uint64_t release = instrument_ms_to_frames(params->release_ms, rate);
    uint64_t elapsed = rel-duration;
    if (!release || elapsed >= release) return 0;
    return held_envelope(duration, params, rate) * (1 - (float)elapsed/release);
}

// Evaluates the existing preset with rate-aware saw, triangle, and sinusoidal components.
static float preset_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params, double phase, double freq, int sample_rate) {
    double sine = instrument_sine(phase, freq, sample_rate);
    double tone = params.tone;
    if (tone < 0.0) tone = 0.0;
    if (tone > 1.0) tone = 1.0;
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
        return (float)(tanh(sine * (1.2 + tone * 3.4)) * 0.16);
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD: {
        double saw = instrument_saw(phase, freq, sample_rate);
        return (float)(saw * (0.08 + tone * 0.10) + sine * (0.06 - tone * 0.03));
    }
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS: {
        double sub_phase = phase * 0.5;
        double second = instrument_sine(phase*2, freq*2, sample_rate);
        (void)freq;
        return (float)(instrument_sine(sub_phase, freq*.5, sample_rate) * 0.22 + second * (0.02 + tone * 0.08));
    }
    case ENGINE_INSTRUMENT_PRESET_SWEEP_BASS:
    case ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK:
    case ENGINE_INSTRUMENT_PRESET_MOVING_PAD:
    case ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS:
    case ENGINE_INSTRUMENT_PRESET_FILTERED_PAD:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK:
    case ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD: {
        // Aligns triangle and sine zero crossings so brightness blends retain a coherent fundamental.
        double triangle = instrument_triangle(phase + M_PI * .5, freq, sample_rate);
        double primary = sine * (1.0 - tone) + triangle * tone;
        double ratio = pow(2.0, (double)params.osc2_detune / 1200.0);
        double secondary = instrument_sine(phase * ratio, freq * ratio, sample_rate) * (1.0 - tone) +
            instrument_triangle(phase * ratio + M_PI * .5, freq * ratio, sample_rate) * tone;
        double blend = primary * (1.0 - params.osc_mix) + secondary * params.osc_mix +
            instrument_sine(phase * .5, freq * .5, sample_rate) * params.sub_mix * .5;
        return (float)(.18 * (blend + params.drive * (tanh(3.0 * blend) / tanh(3.0) - blend)));
    }
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB: {
        double detune_ratio = pow(2.0, (double)params.osc2_detune / 1200.0);
        double phase2 = phase * detune_ratio;
        double osc1 = sine * (0.14 + tone * 0.05);
        double saw2 = instrument_saw(phase2, freq * detune_ratio, sample_rate);
        double osc_mix = params.osc_mix;
        if (osc_mix < 0.0) osc_mix = 0.0;
        if (osc_mix > 1.0) osc_mix = 1.0;
        double sub_mix = params.sub_mix;
        if (sub_mix < 0.0) sub_mix = 0.0;
        if (sub_mix > 1.0) sub_mix = 1.0;
        double sub = instrument_sine(phase*.5, freq*.5, sample_rate) * 0.18 * sub_mix;
        double blend = osc1 * (1.0 - osc_mix) + saw2 * (0.10 + tone * 0.08) * osc_mix + sub;
        double drive = 1.0 + params.drive * 5.0;
        return (float)(tanh(blend * drive) / (1.0 + params.drive * 0.8));
    }
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD: {
        double detune_ratio = pow(2.0, (double)params.osc2_detune / 1200.0);
        double phase2 = phase * detune_ratio;
        double soft = tanh(sine * (1.0 + tone * 1.8)) * 0.10;
        double second = instrument_sine(phase2, freq*detune_ratio, sample_rate) * 0.08 * params.osc_mix;
        double sub = instrument_sine(phase*.5, freq*.5, sample_rate) * 0.10 * params.sub_mix;
        return (float)(soft + second + sub);
    }
    case ENGINE_INSTRUMENT_PRESET_PLUCK: {
        double triangle = instrument_triangle(phase, freq, sample_rate);
        double bright = triangle * (0.08 + tone * 0.08);
        double body = sine * 0.09 * (1.0 - params.osc_mix * 0.5);
        double drive = 1.0 + params.drive * 3.0;
        return (float)(tanh((bright + body) * drive) / (1.0 + params.drive * 0.5));
    }
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD: {
        double saw = instrument_saw(phase, freq, sample_rate);
        double pulse = tanh(sine * (2.0 + tone * 5.0)) * 0.07;
        double drive = 1.0 + params.drive * 4.5;
        return (float)(tanh((saw * (0.10 + tone * 0.08) + pulse) * drive) /
                       (1.0 + params.drive * 0.6));
    }
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS: {
        double harmonic = instrument_sine(phase*2, freq*2, sample_rate) * (0.03 + tone * 0.05);
        double rounded = tanh(sine * (1.0 + tone * 1.4)) * 0.13;
        double sub = instrument_sine(phase*.5, freq*.5, sample_rate) * 0.06 * params.sub_mix;
        return (float)(rounded + harmonic + sub);
    }
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE: {
        double sub = instrument_sine(phase*.5, freq*.5, sample_rate) * (0.18 + params.sub_mix * 0.08);
        double body = sine * (0.04 + tone * 0.04);
        double second = instrument_sine(phase*.25, freq*.25, sample_rate) * 0.05;
        double drive = 1.0 + params.drive * 3.0;
        (void)freq;
        return (float)(tanh((sub + body + second) * drive) / (1.0 + params.drive * 0.7));
    }
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    default:
        (void)freq;
        return (float)(sine * (0.16 + tone * 0.08));
    }
}

// Evaluates one voice using a precomputed base frequency and the existing synthesis arithmetic.
static float instrument_voice_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params,
    double frequency, float velocity, int rate, uint64_t relative_frame, uint64_t duration, float accumulated) {
    if (engine_instrument_sampled_preset(preset)) {
        int note = (int)lround(69 + 12*log2(frequency/440));
        return accumulated + engine_instrument_sample_bank_note(preset, (uint8_t)note, velocity, params, rate, relative_frame, duration);
    }
    if (engine_instrument_percussion_preset(preset))
        return accumulated + engine_instrument_percussion_sample(preset, params, frequency, rate, relative_frame, duration) * params.level * velocity;
    double freq = frequency;
    if (preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB && params.vibrato_depth > 0.0f && params.vibrato_rate > 0.0f) {
        double seconds = (double)relative_frame / (double)rate;
        double cents = sin(2.0 * M_PI * (double)params.vibrato_rate * seconds) * (double)params.vibrato_depth;
        freq *= pow(2.0, cents / 1200.0);
    }
    double phase = (2.0 * M_PI * freq * (double)relative_frame) / (double)rate;
    float env = note_envelope(relative_frame, duration, &params, rate);
    return accumulated + preset_sample(preset, params, phase, freq, rate) * params.level * velocity * env;
}

// Samples one sanitized isolated voice without allocation, automation, mixing or engine mutation.
float engine_instrument_note_sample(EngineInstrumentPresetId preset, EngineInstrumentParams params,
    uint8_t note, float velocity, int rate, uint64_t relative_frame, uint64_t gate_frames) {
    if (rate <= 0 || note > 127 || !isfinite(velocity) || velocity < 0 || velocity > 1 ||
        preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) return 0;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (!isfinite(engine_instrument_params_get(params, (EngineInstrumentParamId)i))) return 0;
    params = engine_instrument_params_sanitize(preset, params);
    if (engine_instrument_filter_preset(preset)) {
        float value = 0;
        engine_instrument_note_waveform(preset, params, note, velocity, rate, relative_frame, 0, gate_frames, &value, 1);
        return value;
    }
    EngineInstrumentPresetId voice = engine_instrument_percussion_note_voice(preset, note);
    if (preset == ENGINE_INSTRUMENT_PRESET_HAT_KIT && voice == ENGINE_INSTRUMENT_PRESET_CLOSED_HAT) params.noise_decay_ms = fminf(params.noise_decay_ms, 80);
    float sample=instrument_voice_sample(voice, params, midi_note_frequency(note), velocity, rate, relative_frame, gate_frames, 0);
    if (voice == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO)
        sample += engine_piano_key_release(note,velocity,params.release_noise,params.level,rate,relative_frame,gate_frames);
    return sample;
}

// Samples the source's actual ADSR with physical millisecond timing and note-off release level.
float engine_instrument_envelope_level(EngineInstrumentParams params, int rate, uint64_t relative_frame, uint64_t gate_frames) {
    if (rate <= 0 || !isfinite(params.attack_ms) || !isfinite(params.decay_ms) ||
        !isfinite(params.sustain) || !isfinite(params.release_ms)) return 0;
    params = engine_instrument_params_sanitize(ENGINE_INSTRUMENT_PRESET_PURE_SINE, params);
    return note_envelope(relative_frame, gate_frames, &params, rate);
}

static EngineInstrumentParams instrument_params_at_frame(const EngineInstrumentSource* instrument,
                                                         uint64_t global_frame,
                                                         uint64_t clip_frame) {
    EngineInstrumentParams params = instrument->params;
    if (instrument->track_automation_lanes && instrument->track_automation_lane_count > 0) {
        for (int i = 0; i < instrument->track_automation_lane_count; ++i) {
            const EngineAutomationLane* lane = &instrument->track_automation_lanes[i];
            EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_COUNT;
            if (!engine_instrument_param_from_automation_target(lane->target, &param)) {
                continue;
            }
            float delta = engine_automation_lane_eval(lane, global_frame, UINT64_MAX);
            params = engine_instrument_params_apply_automation_delta(instrument->preset, params, param, delta);
        }
    }
    if (!instrument->automation_lanes || instrument->automation_lane_count <= 0) {
        return engine_instrument_params_sanitize(instrument->preset, params);
    }
    for (int i = 0; i < instrument->automation_lane_count; ++i) {
        const EngineAutomationLane* lane = &instrument->automation_lanes[i];
        EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_COUNT;
        if (!engine_instrument_param_from_automation_target(lane->target, &param)) {
            continue;
        }
        float delta = engine_automation_lane_eval(lane, clip_frame, instrument->clip_duration_frames);
        params = engine_instrument_params_apply_automation_delta(instrument->preset, params, param, delta);
    }
    return engine_instrument_params_sanitize(instrument->preset, params);
}

// Latches pitch-drop controls at each legato chain onset and glide at each transition.
static EngineInstrumentParams instrument_articulation_params(void* context, uint64_t frame) {
    EngineInstrumentSource* instrument = context;
    uint64_t global = frame > UINT64_MAX - instrument->timeline_start_frame ? UINT64_MAX : instrument->timeline_start_frame + frame;
    return instrument_params_at_frame(instrument, global, frame);
}

// Stores independent key-off and damper-off times so mechanics remain audible under sustain.
static void instrument_prepare_piano(EngineInstrumentSource* instrument) {
    if (instrument->clip_duration_frames == UINT64_MAX) return;
    uint64_t end=instrument->clip_duration_frames;
    if(instrument->export_end_frame!=UINT64_MAX) end=instrument->export_end_frame-instrument->timeline_start_frame;
    for(int n=0;n<instrument->note_count;++n) {
        EnginePreparedNote* p=&instrument->notes[n];
        uint64_t off=p->note.duration_frames>UINT64_MAX-p->note.start_frame?UINT64_MAX:p->note.start_frame+p->note.duration_frames;
        uint64_t damper=engine_piano_damper_frame(off,end,instrument->timeline_start_frame,
            instrument->track_automation_lanes,instrument->track_automation_lane_count,
            instrument->automation_lanes,instrument->automation_lane_count,instrument_articulation_params,instrument);
        p->piano_gate=damper>=p->note.start_frame?damper-p->note.start_frame:0;
    }
}

// Reads an existing gain/pan automation lane using the same normalized values as audio clips.
static float instrument_signal_lane(const EngineAutomationLane* lanes, int count, EngineAutomationTarget target,
                                    uint64_t frame, uint64_t length) {
    for (int i=0;i<count;++i) if (lanes[i].target==target) return engine_automation_lane_eval(&lanes[i],frame,length);
    return 0;
}

// Selects a conservative block-local candidate list without changing original note accumulation order.
static void instrument_select_candidates(EngineInstrumentSource* instrument, uint64_t start, int frames) {
    instrument->candidate_count = 0;
    bool wrap = start > UINT64_MAX - (uint64_t)(frames - 1);
    uint64_t last = start + (uint64_t)(frames - 1);
    uint64_t first_clip = start < instrument->timeline_start_frame ? 0 : start - instrument->timeline_start_frame;
    if (!wrap && last < instrument->timeline_start_frame) return;
    uint64_t last_clip = last - instrument->timeline_start_frame;
    EngineInstrumentParamSpec release_spec;
    (void)engine_instrument_param_spec(ENGINE_INSTRUMENT_PARAM_RELEASE_MS, &release_spec);
    uint64_t maximum_release = instrument_ms_to_frames(release_spec.max_value, instrument->sample_rate);
    if (instrument->preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO) maximum_release = (uint64_t)instrument->sample_rate * 4;
    if (engine_instrument_percussion_one_shot(instrument->preset)) maximum_release = instrument_ms_to_frames(4000, instrument->sample_rate);
    for (int n = 0; n < instrument->note_count; ++n) {
        const EngineMidiNote* note = &instrument->notes[n].note;
        if (!wrap) {
            if (note->start_frame > last_clip) continue;
            if (first_clip >= note->start_frame) {
                uint64_t age = first_clip - note->start_frame;
                uint64_t gate=instrument->preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO ? instrument->notes[n].piano_gate : note->duration_frames;
                if (age >= gate && age - gate >= maximum_release) continue;
            }
        }
        instrument->notes[instrument->candidate_count++].candidate_index = n;
    }
}

// Renders note gates and release envelopes within the authored region, including existing gain/pan lanes.
void engine_instrument_source_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)userdata;
    if (!instrument || !interleaved || frames <= 0 || instrument->sample_rate <= 0 || instrument->channels <= 0) {
        return;
    }

    instrument_select_candidates(instrument, transport_frame, frames);
    const int channels = instrument->channels;
    for (int i = 0; i < frames; ++i) {
        uint64_t global_frame = transport_frame + (uint64_t)i;
        if (global_frame < instrument->timeline_start_frame) {
            continue;
        }
        uint64_t clip_frame = global_frame - instrument->timeline_start_frame;
        if (clip_frame >= instrument->render_duration_frames) {
            continue;
        }

        uint64_t automation_global = global_frame < instrument->export_end_frame ? global_frame : instrument->export_end_frame - 1;
        uint64_t automation_clip = automation_global - instrument->timeline_start_frame;
        EngineInstrumentParams frame_params = instrument_params_at_frame(instrument, automation_global, automation_clip);
        float sample = 0.0f;
        float piano_noise = 0;
        bool piano = instrument->preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO;
        bool piano_held[88] = {false};
        if (piano && instrument->clip_duration_frames == UINT64_MAX && instrument->live_pedal_down) frame_params.sustain_pedal = 1;
        if (piano && (clip_frame >= instrument->clip_duration_frames || global_frame >= instrument->export_end_frame)) frame_params.sustain_pedal = 0;
        bool mono = engine_instrument_percussion_808(instrument->preset) && instrument->params.mono >= .5f;
        if (mono) sample = engine_articulation_808_sample(&instrument->articulation, instrument->preset, frame_params, instrument->sample_rate, clip_frame);
        for (int candidate = 0; !mono && candidate < instrument->candidate_count; ++candidate) {
            int n = instrument->notes[candidate].candidate_index;
            const EngineMidiNote* note = &instrument->notes[n].note;
            if ((instrument->track_hat_routing && note->start_frame >= instrument->clip_duration_frames) || clip_frame < note->start_frame) {
                continue;
            }
            bool choke = engine_instrument_percussion_hat(instrument->preset) && instrument->params.hat_choke >= .5f;
            if (choke && clip_frame >= instrument->notes[n].choke_frame) continue;
            uint64_t rel = clip_frame - note->start_frame;
            uint64_t release = instrument_ms_to_frames(engine_instrument_sampled_preset(instrument->preset) ? fmaxf(4,frame_params.release_ms) : frame_params.release_ms, instrument->sample_rate);
            if (piano) {
                uint64_t gate=instrument->notes[n].piano_gate;
                if(note->note>=21 && note->note<=108 && rel<note->duration_frames) piano_held[note->note-21]=true;
                sample += engine_instrument_sample_bank_note(instrument->preset,note->note,note->velocity,frame_params,instrument->sample_rate,rel,gate);
                piano_noise += engine_piano_key_release(note->note,note->velocity,frame_params.release_noise,frame_params.level,
                    instrument->sample_rate,rel,note->duration_frames);
                continue;
            }
            if (!engine_instrument_percussion_one_shot(instrument->preset) && rel >= note->duration_frames && rel - note->duration_frames >= release) continue;
            if (engine_instrument_sampled_preset(instrument->preset)) {
                float desired = fminf(1,fmaxf(0,frame_params.dynamics+(note->velocity-.5f)*frame_params.velocity_depth));
                float* smoothed=&instrument->notes[n].sampler_dynamics;
                if (*smoothed < 0) *smoothed=desired;
                *smoothed += (desired-*smoothed)/(1+.02f*instrument->sample_rate);
                EngineInstrumentParams voice_params=frame_params;
                voice_params.dynamics=*smoothed; voice_params.velocity_depth=0;
                sample += engine_instrument_sample_bank_note(instrument->preset,note->note,note->velocity,voice_params,instrument->sample_rate,rel,note->duration_frames);
            } else if (engine_instrument_filter_preset(instrument->preset)) {
                double frequency = instrument->notes[n].frequency;
                double phase = (2.0 * M_PI * frequency * (double)rel) / instrument->sample_rate;
                float raw = preset_sample(instrument->preset, frame_params, phase, frequency, instrument->sample_rate);
                float filtered = engine_instrument_filter_process(&instrument->notes[n].filter, raw,
                    frame_params, instrument->sample_rate, rel);
                sample += filtered * frame_params.level * note->velocity * note_envelope(rel, note->duration_frames, &frame_params, instrument->sample_rate);
            } else {
                if (!engine_instrument_percussion_hat(instrument->preset) || (!choke && instrument->preset == ENGINE_INSTRUMENT_PRESET_CLOSED_HAT)) {
                    sample = instrument_voice_sample(instrument->preset, frame_params, instrument->notes[n].frequency,
                        note->velocity, instrument->sample_rate, rel, note->duration_frames, sample);
                    continue;
                }
                EngineInstrumentPresetId voice = engine_instrument_percussion_note_voice(instrument->preset, note->note);
                EngineInstrumentParams voice_params = frame_params;
                if (instrument->preset == ENGINE_INSTRUMENT_PRESET_HAT_KIT && voice == ENGINE_INSTRUMENT_PRESET_CLOSED_HAT)
                    voice_params.noise_decay_ms = fminf(voice_params.noise_decay_ms, 80);
                float hit = instrument_voice_sample(voice, voice_params, instrument->notes[n].frequency,
                    note->velocity, instrument->sample_rate, rel, note->duration_frames, 0);
                if (choke && instrument->notes[n].choke_frame != UINT64_MAX) {
                    uint64_t remaining = instrument->notes[n].choke_frame - clip_frame;
                    hit *= fminf(1, (float)remaining / fmaxf(1, instrument->sample_rate * .002f));
                }
                sample += hit;
            }
        }

        if (piano) sample += piano_noise + engine_piano_expression_process(&instrument->piano,frame_params,sample,piano_held,clip_frame);

        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        float volume = instrument_signal_lane(instrument->automation_lanes, instrument->automation_lane_count,
            ENGINE_AUTOMATION_TARGET_VOLUME, automation_clip, instrument->clip_duration_frames);
        volume += instrument_signal_lane(instrument->track_automation_lanes, instrument->track_automation_lane_count,
            ENGINE_AUTOMATION_TARGET_VOLUME, automation_global, UINT64_MAX);
        float pan = instrument_signal_lane(instrument->automation_lanes, instrument->automation_lane_count,
            ENGINE_AUTOMATION_TARGET_PAN, automation_clip, instrument->clip_duration_frames);
        pan += instrument_signal_lane(instrument->track_automation_lanes, instrument->track_automation_lane_count,
            ENGINE_AUTOMATION_TARGET_PAN, automation_global, UINT64_MAX);
        bool physical_gain = false;
        for (int l = 0; l < instrument->automation_lane_count; ++l)
            if (instrument->automation_lanes[l].target == ENGINE_AUTOMATION_TARGET_VOLUME &&
                instrument->automation_lanes[l].domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL) physical_gain = true;
        sample *= physical_gain ? fmaxf(0, 1 + volume) : fminf(2, fmaxf(0, 1 + volume));
        pan = fminf(1, fmaxf(-1, pan));
        for (int ch = 0; ch < channels; ++ch) {
            float balance = channels < 2 ? 1 : ch==0 && pan>0 ? 1-pan : ch==1 && pan<0 ? 1+pan : 1;
            interleaved[i * channels + ch] = sample * balance;
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

// Streams diagnostics from note onset so the filtered preview shares the exact source recurrence.
bool engine_instrument_note_waveform(EngineInstrumentPresetId preset, EngineInstrumentParams params,
    uint8_t note, float velocity, int rate, uint64_t start, uint64_t span, uint64_t gate, float* output, int count) {
    if (!output || count < 1 || count > 512 || rate <= 0 || rate > 384000 || note > 127 ||
        start > (uint64_t)rate * 2 || span > (uint64_t)rate || preset < 0 || preset >= ENGINE_INSTRUMENT_PRESET_COUNT) return false;
    if (!isfinite(velocity) || velocity < 0 || velocity > 1) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (!isfinite(engine_instrument_params_get(params, i))) return false;
    params = engine_instrument_params_sanitize(preset, params);
    EngineInstrumentFilter filter = {0};
    double frequency = midi_note_frequency(note);
    uint64_t frame = 0;
    for (int i = 0; i < count; ++i) {
        uint64_t wanted = start + (uint64_t)llround((double)span * i / (count > 1 ? count - 1 : 1));
        if (!engine_instrument_filter_preset(preset)) {
            output[i] = engine_instrument_note_sample(preset, params, note, velocity, rate, wanted, gate);
            continue;
        }
        float value = 0;
        // Repeated display positions must retain the same evaluated sample.
        if (i && wanted < frame) { output[i] = output[i-1]; continue; }
        while (frame <= wanted) {
            double phase = (2.0 * M_PI * frequency * (double)frame) / rate;
            float raw = preset_sample(preset, params, phase, frequency, rate);
            value = engine_instrument_filter_process(&filter, raw, params, rate, frame);
            ++frame;
        }
        output[i] = value * params.level * velocity * note_envelope(wanted, gate, &params, rate);
    }
    return true;
}

// Evaluates targets with the same export freeze and piano pedal-up override as production rendering.
static EngineInstrumentParams ending_params(EngineInstrumentSource* s,uint64_t relative) {
    uint64_t global=relative>UINT64_MAX-s->timeline_start_frame?UINT64_MAX:s->timeline_start_frame+relative;
    bool forced=s->preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO && (relative>=s->clip_duration_frames || global>=s->export_end_frame);
    if(global>=s->export_end_frame)global=s->export_end_frame-1;
    EngineInstrumentParams params=instrument_params_at_frame(s,global,global-s->timeline_start_frame);if(forced)params.sustain_pedal=0;return params;
}

// Reads actual prepared piano/choke/mono fences using the production automation evaluator.
bool engine_instrument_source_note_ending(const EngineInstrumentSource* source,int index,EngineNoteEnding* out) {
    if(!source || !out || index<0 || index>=source->note_count)return false;EngineInstrumentSource* s=(EngineInstrumentSource*)source;const EnginePreparedNote* p=&s->notes[index];
    *out=(EngineNoteEnding){.original_index=p->original_index,.start=p->note.start_frame,.key_off=p->note.start_frame+p->note.duration_frames,.source_end=s->render_duration_frames,.choke=UINT64_MAX,.handoff=UINT64_MAX};
    out->sampled=engine_instrument_sampled_preset(s->preset);out->piano=s->preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO;out->one_shot=engine_instrument_percussion_one_shot(s->preset);out->mono=engine_instrument_percussion_808(s->preset) && s->params.mono>=.5f;
    out->damper_off=out->piano?p->note.start_frame+p->piano_gate:out->key_off;
    if(engine_instrument_percussion_hat(s->preset) && s->params.hat_choke>=.5f)out->choke=p->choke_frame;
    out->chain_start=out->start;
    for(int n=0;out->mono && n<s->articulation.count;++n)if(s->articulation.events[n].note_index==p->original_index){const EngineArticulationEvent* event=&s->articulation.events[n];out->legato=event->legato;out->chain_start=event->chain_start;if(n+1<s->articulation.count){out->handoff=s->articulation.events[n+1].note.start_frame;out->next_legato=s->articulation.events[n+1].legato;}break;}
    out->onset_params=ending_params(s,out->start);out->key_off_params=ending_params(s,out->damper_off);out->boundary_params=ending_params(s,s->clip_duration_frames?s->clip_duration_frames-1:0);return true;
}
