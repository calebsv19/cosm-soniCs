#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "engine/instrument_samples.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Refreshes only the worker-owned audition source without traversing editable project clips.
static void engine_midi_audition_refresh_source(Engine* engine) {
    if (!engine || !engine->midi_audition_source)
        return;
    (void)engine_instrument_source_set_midi_clip(engine->midi_audition_source, 0, UINT64_MAX,
                                                 engine->midi_audition_preset, engine->midi_audition_params,
                                                 engine->midi_audition_notes.notes,
                                                 engine->midi_audition_notes.note_count, NULL, 0, NULL, 0);
    bool piano=engine->midi_audition_preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO;
    if (!piano || !engine->midi_audition_piano_configured)
        engine_instrument_source_reset(engine->midi_audition_source, engine->config.sample_rate, engine_graph_get_channels(engine->graph));
    if(piano) {
        engine->midi_audition_piano_configured=true;
        engine_instrument_source_piano_pedal(engine->midi_audition_source,engine->midi_audition_pedal_down,
            engine->transport_frame+engine->midi_audition_idle_frame);
    }
}

// Reports whether the caller owns live audition state.
static bool engine_midi_audition_is_worker_thread(const Engine* engine) {
    return engine && engine->worker_thread_id != 0 && SDL_ThreadID() == engine->worker_thread_id;
}

// Allows direct updates only without a live worker or on that worker.
static bool engine_midi_audition_should_apply_direct(const Engine* engine) {
    return engine && (!engine->device_started || !engine->worker_thread ||
                      engine_midi_audition_is_worker_thread(engine));
}

// Finds only held notes so a retrigger can coexist with its older release.
static int engine_midi_audition_find_note(const Engine* engine, uint8_t note) {
    if (!engine) {
        return -1;
    }
    for (int i = 0; i < engine->midi_audition_notes.note_count; ++i) {
        if (engine->midi_audition_notes.notes[i].note == note &&
            engine->midi_audition_notes.notes[i].duration_frames ==
                UINT64_MAX - engine->midi_audition_notes.notes[i].start_frame) {
            return i;
        }
    }
    return -1;
}

// Uses the render clock while playing and the independent idle offset while stopped.
static uint64_t engine_midi_audition_current_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    if (atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
        return engine->transport_frame;
    }
    return engine->transport_frame + engine->midi_audition_idle_frame;
}

// Builds either an existing held audition voice or a sample-counted finite preview.
static void apply_gated(Engine* engine, int track_index, EngineInstrumentPresetId preset,
                                        EngineInstrumentParams params, uint8_t note, float velocity, uint64_t gate) {
    if (!engine || note > ENGINE_MIDI_NOTE_MAX) {
        return;
    }
    EngineInstrumentPresetId clamped_preset = engine_instrument_preset_clamp(preset);
    EngineInstrumentParams clamped_params = engine_instrument_params_sanitize(clamped_preset, params);
    if (!isfinite(velocity) || velocity <= 0.0f) {
        velocity = 1.0f;
    }
    if (velocity > 1.0f) {
        velocity = 1.0f;
    }

    bool transport_playing = atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
    if (!transport_playing && engine->midi_audition_notes.note_count == 0 &&
        !engine->midi_audition_tail_until) {
        engine->midi_audition_idle_frame = 0;
        engine_transport_reset_history(engine);
        engine_clock_discontinuity(engine, 0, true, false);
    }

    if (engine->midi_audition_track_index != track_index || engine->midi_audition_preset != clamped_preset ||
        memcmp(&engine->midi_audition_params, &clamped_params, sizeof(clamped_params)) != 0) {
        bool pending=engine->midi_audition_pedal_down && !engine->midi_audition_notes.note_count;
        engine_midi_audition_apply_all_off(engine);
        if(pending) engine->midi_audition_pedal_down=true;
    }
    engine_midi_audition_apply_note_off(engine, note);
    engine_midi_audition_retire(engine);
    if (engine->midi_audition_notes.note_count == engine->midi_audition_notes.note_capacity) {
        // At most 128 held pitches exist; reclaim the oldest released voice at capacity.
        for (int i = 0; i < engine->midi_audition_notes.note_count; ++i) {
            EngineMidiNote* voice = &engine->midi_audition_notes.notes[i];
            if (voice->duration_frames != UINT64_MAX - voice->start_frame) {
                engine_midi_note_list_remove(&engine->midi_audition_notes, i);
                break;
            }
        }
    }
    engine->midi_audition_tail_until = 0;
    uint64_t start_frame = engine_midi_audition_current_frame(engine);
    EngineMidiNote active = {.start_frame = start_frame,
                             .duration_frames = gate ? gate : UINT64_MAX - start_frame,
                             .note = note,
                             .velocity = velocity};
    if (engine->midi_audition_notes.note_count < engine->midi_audition_notes.note_capacity) {
        int at = engine->midi_audition_notes.note_count++;
        while (at > 0) {
            EngineMidiNote previous = engine->midi_audition_notes.notes[at - 1];
            if (previous.start_frame < active.start_frame ||
                (previous.start_frame == active.start_frame && previous.note <= active.note))
                break;
            engine->midi_audition_notes.notes[at--] = previous;
        }
        engine->midi_audition_notes.notes[at] = active;
        engine->midi_audition_track_index = track_index;
        engine->midi_audition_preset = clamped_preset;
        engine->midi_audition_params = clamped_params;
        engine_midi_audition_refresh_source(engine);
    }
}

// Preserves the existing held-key audition contract.
void engine_midi_audition_apply_note_on(Engine* e, int t, EngineInstrumentPresetId p, EngineInstrumentParams params, uint8_t note, float velocity) {
    apply_gated(e, t, p, params, note, velocity, 0);
}
// Applies a validated finite gate on the owning render worker.
void engine_midi_audition_apply_preview(Engine* e, int t, EngineInstrumentPresetId p, EngineInstrumentParams params, uint8_t note, float velocity, uint64_t gate) {
    apply_gated(e, t, p, params, note, velocity, gate);
}
void engine_midi_audition_apply_note_off(Engine* engine, uint8_t note) {
    if (!engine) {
        return;
    }
    int index = engine_midi_audition_find_note(engine, note);
    if (index < 0) {
        return;
    }
    EngineMidiNote* voice = &engine->midi_audition_notes.notes[index];
    uint64_t now = engine_midi_audition_current_frame(engine);
    voice->duration_frames = now > voice->start_frame ? now - voice->start_frame : 1;
    engine_midi_audition_refresh_source(engine);
}

// Retires completed releases and allows at most two seconds of stopped effect tail.
void engine_midi_audition_retire(Engine* engine) {
    if (!engine)
        return;
    uint64_t now = engine_midi_audition_current_frame(engine);
    uint64_t release =
        (uint64_t)llround((engine_instrument_sampled_preset(engine->midi_audition_preset) ? fmaxf(4,engine->midi_audition_params.release_ms) : engine->midi_audition_params.release_ms) * 0.001 * engine->config.sample_rate);
    bool changed = false;
    for (int i = engine->midi_audition_notes.note_count - 1; i >= 0; --i) {
        EngineMidiNote* voice = &engine->midi_audition_notes.notes[i];
        if (voice->duration_frames == UINT64_MAX - voice->start_frame || now < voice->start_frame)
            continue;
        uint64_t age = now - voice->start_frame;
        uint64_t gate=voice->duration_frames;
        if(engine->midi_audition_preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO) {
            gate=engine_instrument_source_piano_gate(engine->midi_audition_source,voice->start_frame,voice->note);
            release=(uint64_t)engine->config.sample_rate*2;
            if(gate==UINT64_MAX-voice->start_frame) {
                if(age<(uint64_t)engine->config.sample_rate*40) continue;
                gate=voice->duration_frames;
            }
        }
        if (age >= gate && age - gate >= release) {
            engine_midi_note_list_remove(&engine->midi_audition_notes, i);
            changed = true;
        }
    }
    if (changed) {
        engine_midi_audition_refresh_source(engine);
        if (!engine->midi_audition_notes.note_count && !atomic_load(&engine->transport_playing))
            engine->midi_audition_tail_until =
                engine_clock_advance(now, (uint64_t)engine->config.sample_rate * 2, 0, 0);
    }
    if (engine->midi_audition_tail_until && now >= engine->midi_audition_tail_until) {
        // Preserve queued audio while clearing DSP that must not leak into later project playback.
        engine->midi_audition_tail_until = 0;
        engine->midi_audition_track_index = -1;
        engine_transport_reset_history(engine);
    }
}

// Immediately clears all audition voices without freeing worker-owned prepared storage.
void engine_midi_audition_apply_all_off(Engine* engine) {
    if (!engine) return;
    engine->midi_audition_pedal_down=false;
    engine->midi_audition_piano_configured=false;
    engine_instrument_source_piano_pedal(engine->midi_audition_source,false,engine_midi_audition_current_frame(engine));
    if (!engine->midi_audition_notes.note_count && !engine->midi_audition_tail_until) return;
    engine_instrument_source_piano_pedal(engine->midi_audition_source,false,engine_midi_audition_current_frame(engine));
    engine->midi_audition_notes.note_count = 0;
    engine->midi_audition_tail_until = 0;
    engine->midi_audition_idle_frame = 0;
    engine->midi_audition_track_index = -1;
    if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
        engine_clock_discontinuity(engine, 0, true, false);
        engine_transport_reset_history(engine);
    }
    engine_midi_audition_refresh_source(engine);
    engine_instrument_source_reset(engine->midi_audition_source,engine->config.sample_rate,engine_graph_get_channels(engine->graph));
}

bool engine_midi_audition_note_on(Engine* engine, int track_index, EngineInstrumentPresetId preset,
                                  EngineInstrumentParams params, uint8_t note, float velocity) {
    if (!engine || note > ENGINE_MIDI_NOTE_MAX || track_index < 0 || track_index >= engine->track_count ||
        (engine->tracks[track_index].is_return || engine->tracks[track_index].is_group) || SDL_ThreadID() != engine->control_thread_id) {
        return false;
    }
    engine_preview_stop(engine);
    if (engine_midi_audition_should_apply_direct(engine)) {
        engine_midi_audition_apply_note_on(engine, track_index, preset, params, note, velocity);
        return true;
    }
    EngineInstrumentPresetId clamped_preset = engine_instrument_preset_clamp(preset);
    EngineCommand cmd = {
        .type = ENGINE_CMD_MIDI_AUDITION_NOTE_ON,
        .payload.midi_audition = {.track_index = track_index,
                                  .track_runtime_id = engine->tracks[track_index].runtime_id,
                                  .preset = clamped_preset,
                                  .params = engine_instrument_params_sanitize(clamped_preset, params),
                                  .note = note,
                                  .velocity = velocity}};
    return engine_post_command(engine, &cmd);
}

bool engine_midi_audition_note_off(Engine* engine, uint8_t note) {
    if (!engine || note > ENGINE_MIDI_NOTE_MAX) {
        return false;
    }
    if (engine_midi_audition_should_apply_direct(engine)) {
        engine_midi_audition_apply_note_off(engine, note);
        return true;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_MIDI_AUDITION_NOTE_OFF, .payload.midi_audition = {.note = note}};
    return engine_post_command(engine, &cmd);
}

// Mutates only worker-owned piano sustain state without invalidating sounding voices.
void engine_midi_audition_apply_pedal(Engine* engine,bool down) {
    if(engine) engine->midi_audition_pedal_down=down;
    if(engine) engine_instrument_source_piano_pedal(engine->midi_audition_source,down,engine_midi_audition_current_frame(engine));
}
// Uses safety admission for pedal-up so a saturated command queue cannot leave sustain stuck.
bool engine_midi_audition_pedal(Engine* engine,bool down) {
    if(!engine || !engine_is_control_thread(engine)) return false;
    if(engine_midi_audition_should_apply_direct(engine)) { engine_midi_audition_apply_pedal(engine,down);return true; }
    EngineCommand cmd={.type=ENGINE_CMD_MIDI_AUDITION_PEDAL,.payload.midi_audition={.velocity=down?1:0}};
    return engine_post_command(engine,&cmd);
}

void engine_midi_audition_all_notes_off(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine_midi_audition_should_apply_direct(engine)) {
        engine_midi_audition_apply_all_off(engine);
        return;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_MIDI_AUDITION_ALL_OFF};
    (void)engine_post_command(engine, &cmd);
}

// Uses the normal bounded command queue while retaining a finite sample-counted note duration.
bool engine_midi_audition_note_preview(Engine* e, int t, EngineInstrumentPresetId p, EngineInstrumentParams params,
    uint8_t note, float velocity, uint64_t gate) {
    if (!e || !engine_is_control_thread(e) || t < 0 || t >= e->track_count || note > 127 ||
        p < 0 || p >= ENGINE_INSTRUMENT_PRESET_COUNT || !isfinite(velocity) || velocity <= 0 || velocity > 1 ||
        !gate || gate > (uint64_t)e->config.sample_rate * 2) return false;
    engine_preview_stop(e);
    if (engine_midi_audition_should_apply_direct(e)) { engine_midi_audition_apply_preview(e, t, p, params, note, velocity, gate); return true; }
    EngineCommand cmd = {.type = ENGINE_CMD_MIDI_AUDITION_NOTE_ON, .payload.midi_audition = {
        .track_index = t, .track_runtime_id = e->tracks[t].runtime_id, .preset = p,
        .params = engine_instrument_params_sanitize(p, params), .note = note, .velocity = velocity, .gate_frames = gate}};
    return engine_post_command(e, &cmd);
}
