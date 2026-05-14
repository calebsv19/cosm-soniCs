#include "engine/engine_internal.h"

#include <stdbool.h>
#include <stdint.h>

static bool engine_midi_audition_is_worker_thread(const Engine* engine) {
    return engine && engine->worker_thread_id != 0 && SDL_ThreadID() == engine->worker_thread_id;
}

static bool engine_midi_audition_should_apply_direct(const Engine* engine) {
    return engine && (!engine->device_started || !engine->worker_thread || engine_midi_audition_is_worker_thread(engine));
}

static int engine_midi_audition_find_note(const Engine* engine, uint8_t note) {
    if (!engine) {
        return -1;
    }
    for (int i = 0; i < engine->midi_audition_notes.note_count; ++i) {
        if (engine->midi_audition_notes.notes[i].note == note) {
            return i;
        }
    }
    return -1;
}

static uint64_t engine_midi_audition_current_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    if (atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
        return engine->transport_frame;
    }
    return engine->transport_frame + engine->midi_audition_idle_frame;
}

void engine_midi_audition_apply_note_on(Engine* engine,
                                        int track_index,
                                        EngineInstrumentPresetId preset,
                                        EngineInstrumentParams params,
                                        uint8_t note,
                                        float velocity) {
    if (!engine || note > ENGINE_MIDI_NOTE_MAX) {
        return;
    }
    EngineInstrumentPresetId clamped_preset = engine_instrument_preset_clamp(preset);
    EngineInstrumentParams clamped_params = engine_instrument_params_sanitize(clamped_preset, params);
    if (velocity <= 0.0f) {
        velocity = 1.0f;
    }
    if (velocity > 1.0f) {
        velocity = 1.0f;
    }

    bool transport_playing = atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
    if (!transport_playing && engine->midi_audition_notes.note_count == 0) {
        engine->midi_audition_idle_frame = 0;
        audio_queue_clear(&engine->output_queue);
    }

    int existing = engine_midi_audition_find_note(engine, note);
    if (existing >= 0) {
        engine->midi_audition_track_index = track_index;
        engine->midi_audition_preset = clamped_preset;
        engine->midi_audition_params = clamped_params;
        engine_rebuild_sources(engine);
        return;
    }

    uint64_t start_frame = engine_midi_audition_current_frame(engine);
    EngineMidiNote active = {
        .start_frame = start_frame,
        .duration_frames = UINT64_MAX - start_frame,
        .note = note,
        .velocity = velocity
    };
    if (engine_midi_note_list_insert(&engine->midi_audition_notes, active, NULL)) {
        engine->midi_audition_track_index = track_index;
        engine->midi_audition_preset = clamped_preset;
        engine->midi_audition_params = clamped_params;
        engine_rebuild_sources(engine);
    }
}

void engine_midi_audition_apply_note_off(Engine* engine, uint8_t note) {
    if (!engine) {
        return;
    }
    int index = engine_midi_audition_find_note(engine, note);
    if (index < 0) {
        return;
    }
    if (engine_midi_note_list_remove(&engine->midi_audition_notes, index)) {
        if (engine->midi_audition_notes.note_count == 0) {
            engine->midi_audition_track_index = -1;
            engine->midi_audition_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
            engine->midi_audition_params = engine_instrument_default_params(engine->midi_audition_preset);
            if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
                engine->midi_audition_idle_frame = 0;
                audio_queue_clear(&engine->output_queue);
            }
        }
        engine_rebuild_sources(engine);
    }
}

void engine_midi_audition_apply_all_off(Engine* engine) {
    if (!engine || engine->midi_audition_notes.note_count == 0) {
        return;
    }
    engine_midi_note_list_free(&engine->midi_audition_notes);
    engine_midi_note_list_init(&engine->midi_audition_notes);
    engine->midi_audition_idle_frame = 0;
    engine->midi_audition_track_index = -1;
    engine->midi_audition_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    engine->midi_audition_params = engine_instrument_default_params(engine->midi_audition_preset);
    if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
        audio_queue_clear(&engine->output_queue);
    }
    engine_rebuild_sources(engine);
}

bool engine_midi_audition_note_on(Engine* engine,
                                  int track_index,
                                  EngineInstrumentPresetId preset,
                                  EngineInstrumentParams params,
                                  uint8_t note,
                                  float velocity) {
    if (!engine || note > ENGINE_MIDI_NOTE_MAX) {
        return false;
    }
    if (engine_midi_audition_should_apply_direct(engine)) {
        engine_midi_audition_apply_note_on(engine, track_index, preset, params, note, velocity);
        return true;
    }
    EngineInstrumentPresetId clamped_preset = engine_instrument_preset_clamp(preset);
    EngineCommand cmd = {
        .type = ENGINE_CMD_MIDI_AUDITION_NOTE_ON,
        .payload.midi_audition = {
            .track_index = track_index,
            .preset = clamped_preset,
            .params = engine_instrument_params_sanitize(clamped_preset, params),
            .note = note,
            .velocity = velocity
        }
    };
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
    EngineCommand cmd = {
        .type = ENGINE_CMD_MIDI_AUDITION_NOTE_OFF,
        .payload.midi_audition = {
            .note = note
        }
    };
    return engine_post_command(engine, &cmd);
}

void engine_midi_audition_all_notes_off(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine_midi_audition_should_apply_direct(engine)) {
        engine_midi_audition_apply_all_off(engine);
        return;
    }
    EngineCommand cmd = {
        .type = ENGINE_CMD_MIDI_AUDITION_ALL_OFF
    };
    (void)engine_post_command(engine, &cmd);
}
