#include "engine/engine_internal.h"

#include "engine/instrument.h"
#include "engine/midi.h"

static bool engine_midi_note_fits_duration(const EngineMidiNote* note, uint64_t duration_frames) {
    if (!engine_midi_note_is_valid(note)) {
        return false;
    }
    if (note->start_frame > duration_frames) {
        return false;
    }
    return note->duration_frames <= duration_frames - note->start_frame;
}

bool engine_midi_notes_fit_duration(const EngineMidiNoteList* notes, uint64_t duration_frames) {
    if (!engine_midi_note_list_validate(notes)) {
        return false;
    }
    for (int i = 0; i < notes->note_count; ++i) {
        if (!engine_midi_note_fits_duration(&notes->notes[i], duration_frames)) {
            return false;
        }
    }
    return true;
}

static EngineClip* engine_get_midi_clip_mutable(Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return NULL;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip;
}

bool engine_clip_midi_add_note(Engine* engine,
                               int track_index,
                               int clip_index,
                               EngineMidiNote note,
                               int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (!engine_midi_note_fits_duration(&note, clip->duration_frames)) {
        return false;
    }
    bool ok = engine_midi_note_list_insert(&clip->midi_notes, note, out_note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_update_note(Engine* engine,
                                  int track_index,
                                  int clip_index,
                                  int note_index,
                                  EngineMidiNote note,
                                  int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (!engine_midi_note_fits_duration(&note, clip->duration_frames)) {
        return false;
    }
    bool ok = engine_midi_note_list_update(&clip->midi_notes, note_index, note, out_note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_remove_note(Engine* engine, int track_index, int clip_index, int note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    bool ok = engine_midi_note_list_remove(&clip->midi_notes, note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_set_notes(Engine* engine,
                                int track_index,
                                int clip_index,
                                const EngineMidiNote* notes,
                                int note_count) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || note_count < 0) {
        return false;
    }
    if (note_count > 0 && !notes) {
        return false;
    }
    EngineMidiNoteList replacement;
    engine_midi_note_list_init(&replacement);
    bool ok = engine_midi_note_list_set(&replacement, notes, note_count);
    if (ok) {
        ok = engine_midi_notes_fit_duration(&replacement, clip->duration_frames);
    }
    if (ok) {
        engine_midi_note_list_free(&clip->midi_notes);
        clip->midi_notes = replacement;
        engine_midi_note_list_init(&replacement);
        engine_request_rebuild_sources(engine);
    }
    engine_midi_note_list_free(&replacement);
    return ok;
}

int engine_clip_midi_note_count(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return 0;
    }
    return clip->midi_notes.note_count;
}

const EngineMidiNote* engine_clip_midi_notes(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip->midi_notes.notes;
}

EngineInstrumentPresetId engine_clip_midi_instrument_preset(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return engine_instrument_preset_clamp(clip->instrument_preset);
}

bool engine_clip_midi_inherits_track_instrument(const EngineClip* clip) {
    return clip && clip->kind == ENGINE_CLIP_KIND_MIDI && clip->instrument_inherits_track;
}

bool engine_track_midi_instrument_enabled(const Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    return engine->tracks[track_index].midi_instrument_enabled;
}

EngineInstrumentPresetId engine_track_midi_instrument_preset(const Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return engine_instrument_preset_clamp(engine->tracks[track_index].midi_instrument_preset);
}

EngineInstrumentParams engine_track_midi_instrument_params(const Engine* engine, int track_index) {
    EngineInstrumentPresetId preset = engine_track_midi_instrument_preset(engine, track_index);
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return engine_instrument_default_params(preset);
    }
    return engine_instrument_params_sanitize(preset, engine->tracks[track_index].midi_instrument_params);
}

bool engine_track_midi_set_instrument_preset(Engine* engine,
                                             int track_index,
                                             EngineInstrumentPresetId preset) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    EngineInstrumentPresetId clamped = engine_instrument_preset_clamp(preset);
    if (track->midi_instrument_enabled &&
        track->midi_instrument_preset == clamped) {
        return true;
    }
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = clamped;
    track->midi_instrument_params = engine_instrument_default_params(clamped);
    engine_request_rebuild_sources(engine);
    return true;
}

static bool engine_instrument_params_equal(EngineInstrumentParams a, EngineInstrumentParams b) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamId param = (EngineInstrumentParamId)i;
        if (engine_instrument_params_get(a, param) != engine_instrument_params_get(b, param)) {
            return false;
        }
    }
    return true;
}

bool engine_track_midi_set_instrument_params(Engine* engine,
                                             int track_index,
                                             EngineInstrumentParams params) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    EngineInstrumentPresetId preset = engine_instrument_preset_clamp(track->midi_instrument_preset);
    EngineInstrumentParams clamped = engine_instrument_params_sanitize(preset, params);
    if (track->midi_instrument_enabled &&
        engine_instrument_params_equal(track->midi_instrument_params, clamped)) {
        return true;
    }
    track->midi_instrument_enabled = true;
    track->midi_instrument_params = clamped;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_midi_set_instrument_enabled(Engine* engine, int track_index, bool enabled) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    if (track->midi_instrument_enabled == enabled) {
        return true;
    }
    track->midi_instrument_enabled = enabled;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_midi_set_inherits_track_instrument(Engine* engine,
                                                    int track_index,
                                                    int clip_index,
                                                    bool inherits_track) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (clip->instrument_inherits_track == inherits_track) {
        return true;
    }
    clip->instrument_inherits_track = inherits_track;
    engine_request_rebuild_sources(engine);
    return true;
}

EngineInstrumentPresetId engine_clip_midi_effective_instrument_preset(const Engine* engine,
                                                                      int track_index,
                                                                      int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    if (clip->instrument_inherits_track) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    return engine_clip_midi_instrument_preset(clip);
}

EngineInstrumentParams engine_clip_midi_effective_instrument_params(const Engine* engine,
                                                                    int track_index,
                                                                    int clip_index) {
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(engine, track_index, clip_index);
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return engine_instrument_default_params(preset);
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return engine_track_midi_instrument_params(engine, track_index);
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI || clip->instrument_inherits_track) {
        return engine_track_midi_instrument_params(engine, track_index);
    }
    return engine_clip_midi_instrument_params(clip);
}

bool engine_clip_midi_set_instrument_preset(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentPresetId preset) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId clamped = engine_instrument_preset_clamp(preset);
    if (clip->instrument_preset == clamped && !clip->instrument_inherits_track) {
        return true;
    }
    clip->instrument_preset = clamped;
    clip->instrument_params = engine_instrument_default_params(clamped);
    clip->instrument_inherits_track = false;
    engine_request_rebuild_sources(engine);
    return true;
}

EngineInstrumentParams engine_clip_midi_instrument_params(const EngineClip* clip) {
    EngineInstrumentPresetId preset = engine_clip_midi_instrument_preset(clip);
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return engine_instrument_default_params(preset);
    }
    return engine_instrument_params_sanitize(preset, clip->instrument_params);
}

bool engine_clip_midi_set_instrument_params(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentParams params) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId preset = clip->instrument_inherits_track
                                          ? engine_clip_midi_effective_instrument_preset(engine,
                                                                                        track_index,
                                                                                        clip_index)
                                          : engine_clip_midi_instrument_preset(clip);
    EngineInstrumentParams clamped = engine_instrument_params_sanitize(preset, params);
    if (engine_instrument_params_equal(clip->instrument_params, clamped) && !clip->instrument_inherits_track) {
        return true;
    }
    clip->instrument_preset = preset;
    clip->instrument_params = clamped;
    clip->instrument_inherits_track = false;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_midi_set_instrument_param(Engine* engine,
                                           int track_index,
                                           int clip_index,
                                           EngineInstrumentParamId param,
                                           float value) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId preset = clip->instrument_inherits_track
                                          ? engine_clip_midi_effective_instrument_preset(engine,
                                                                                        track_index,
                                                                                        clip_index)
                                          : engine_clip_midi_instrument_preset(clip);
    EngineInstrumentParams base = clip->instrument_inherits_track
                                      ? engine_clip_midi_effective_instrument_params(engine, track_index, clip_index)
                                      : clip->instrument_params;
    EngineInstrumentParams params = engine_instrument_params_set(preset, base, param, value);
    return engine_clip_midi_set_instrument_params(engine, track_index, clip_index, params);
}
