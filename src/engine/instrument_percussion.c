#include "engine/instrument_percussion.h"
#include <math.h>

// Classifies appended drum and 808 identities while leaving legacy rendering untouched.
bool engine_instrument_percussion_preset(EngineInstrumentPresetId preset) {
    return preset >= ENGINE_INSTRUMENT_PRESET_KICK_DRUM && preset <= ENGINE_INSTRUMENT_PRESET_HAT_KIT;
}

// Lets kick, snare and closed-hat hits finish naturally after a short trigger.
bool engine_instrument_percussion_one_shot(EngineInstrumentPresetId preset) {
    return (preset >= ENGINE_INSTRUMENT_PRESET_KICK_DRUM && preset <= ENGINE_INSTRUMENT_PRESET_CLOSED_HAT) || engine_instrument_percussion_hat(preset);
}

// Classifies the closed, open and combined hat instruments.
bool engine_instrument_percussion_hat(EngineInstrumentPresetId preset) {
    return preset == ENGINE_INSTRUMENT_PRESET_CLOSED_HAT || preset == ENGINE_INSTRUMENT_PRESET_OPEN_HAT || preset == ENGINE_INSTRUMENT_PRESET_HAT_KIT;
}

// Classifies only the two pitched 808 bodies.
bool engine_instrument_percussion_808(EngineInstrumentPresetId preset) {
    return preset == ENGINE_INSTRUMENT_PRESET_SUB_808 || preset == ENGINE_INSTRUMENT_PRESET_DRIVEN_808;
}

// Uses GM 42/44 for closed hits and 46 for open hits in the combined hat voice.
EngineInstrumentPresetId engine_instrument_percussion_note_voice(EngineInstrumentPresetId preset, uint8_t note) {
    if (preset != ENGINE_INSTRUMENT_PRESET_HAT_KIT) return preset;
    return note == 46 ? ENGINE_INSTRUMENT_PRESET_OPEN_HAT : ENGINE_INSTRUMENT_PRESET_CLOSED_HAT;
}

// Hashes absolute note-relative frames so seeks, block sizes and offline exports reproduce identical noise.
static double noise_at(uint64_t frame) {
    uint64_t x = frame + UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (double)(x >> 40) / 8388608.0 - 1;
}

// Defines physical decay as time to -60 dB, with a final smooth taper and exact silence.
static double decay(double seconds, double ms) {
    double length = ms * .001;
    if (length <= 0 || seconds >= length) return 0;
    double remaining = (length - seconds) / fmin(.01, length * .1);
    return exp(-6.907755278982137 * seconds / length) * fmin(1, remaining);
}

// Combines attack with the 808 note-off fade; one-shot drums deliberately ignore gate duration.
static double gate_scale(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    double seconds, double gate_seconds) {
    double attack = p.attack_ms * .001;
    double scale = attack > 0 ? fmin(1, seconds / attack) : 1;
    if (!engine_instrument_percussion_one_shot(preset) && seconds >= gate_seconds) {
        double release = p.release_ms * .001;
        scale *= release > 0 ? fmax(0, 1 - (seconds - gate_seconds) / release) : 0;
    }
    return scale;
}

// Displays the dominant body/noise envelope, including the real attack and 808 gate.
float engine_instrument_percussion_envelope(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    int rate, uint64_t frame, uint64_t gate) {
    if (rate <= 0 || !gate || !engine_instrument_percussion_preset(preset)) return 0;
    if (!engine_instrument_percussion_one_shot(preset) && frame >= gate &&
        frame - gate >= (uint64_t)llround(p.release_ms * rate / 1000.0)) return 0;
    double t = (double)frame / rate;
    double body = engine_instrument_percussion_hat(preset) ? 0 : decay(t, p.body_decay_ms);
    double noise = preset == ENGINE_INSTRUMENT_PRESET_SNARE_DRUM ? p.noise_mix :
        engine_instrument_percussion_hat(preset) ? 1 : 0;
    return (float)(fmax(body, noise * decay(t, p.noise_decay_ms)) * gate_scale(preset,p,t,(double)gate/rate));
}

// Integrates an exponential frequency sweep analytically to avoid the phase jumps of f(t)*t.
float engine_instrument_percussion_sample(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    double frequency, int rate, uint64_t frame, uint64_t gate) {
    if (rate <= 0 || !gate || frequency <= 0 || !isfinite(frequency)) return 0;
    if (!engine_instrument_percussion_one_shot(preset) && frame >= gate &&
        frame - gate >= (uint64_t)llround(p.release_ms * rate / 1000.0)) return 0;
    double t = (double)frame / rate;
    double scale = gate_scale(preset,p,t,(double)gate/rate);
    if (scale == 0) return 0;
    double tau = fmax(.001, p.pitch_decay_ms * .001);
    double ratio = exp2(p.pitch_env_semitones / 12.0);
    double instantaneous = frequency * (1 + (ratio - 1) * exp(-t/tau));
    double phase = 6.28318530717958647692 * frequency * (t + (ratio - 1)*tau*(-expm1(-t/tau)));
    return engine_instrument_percussion_sample_phase(preset, p, instantaneous, phase, rate, frame, gate);
}

// Applies the existing body, noise, transient and saturation stages to a phase-continuous voice.
float engine_instrument_percussion_sample_phase(EngineInstrumentPresetId preset, EngineInstrumentParams p,
    double instantaneous, double phase, int rate, uint64_t frame, uint64_t gate) {
    if (rate <= 0 || !gate || !isfinite(instantaneous) || !isfinite(phase)) return 0;
    if (!engine_instrument_percussion_one_shot(preset) && frame >= gate &&
        frame - gate >= (uint64_t)llround(p.release_ms * rate / 1000.0)) return 0;
    double t = (double)frame / rate;
    double scale = gate_scale(preset, p, t, (double)gate / rate);
    if (scale == 0) return 0;
    double body = 0;
    // Silence ultrasonic components rather than folding a swept high MIDI note into audible aliases.
    if (instantaneous < rate * .45) {
        body = sin(phase);
        if (instantaneous * 2 < rate * .45) body += .22 * p.tone * sin(phase*2);
        if (preset == ENGINE_INSTRUMENT_PRESET_SNARE_DRUM && instantaneous * 1.47 < rate * .45)
            body = .65 * body + .35 * sin(phase*1.47);
        body *= decay(t,p.body_decay_ms);
    }
    double white = noise_at(frame);
    double low = (white + noise_at(frame-1) + noise_at(frame-2) + noise_at(frame-3)) * .25;
    double colored = (1-p.tone)*low + p.tone*(white-low);
    double sample;
    if (engine_instrument_percussion_hat(preset))
        sample = .25 * colored * decay(t,p.noise_decay_ms);
    else {
        double mix = preset == ENGINE_INSTRUMENT_PRESET_SNARE_DRUM ? p.noise_mix : 0;
        sample = .32 * body * (1-mix) + .3 * mix * colored * decay(t,p.noise_decay_ms);
        sample += .12 * p.click_mix * (white-low) * decay(t,8);
        // Crossfades normalized saturation so drive controls timbre without a large loudness jump.
        sample = (1-p.drive)*sample + p.drive*.32*tanh(sample*12);
    }
    return (float)(sample*scale);
}
