#include "engine/engine_internal.h"

#include "engine/instrument.h"
#include "engine/midi.h"
#include <math.h>

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

// Looks up an existing MIDI clip on its owning control thread.
static EngineClip* engine_get_midi_clip_mutable(Engine* engine, int track_index, int clip_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count) {
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

// Publishes replacement notes while preserving the original allocation on rejection.
static bool engine_midi_commit_notes(Engine* engine, EngineClip* clip, EngineMidiNoteList* replacement) {
    EngineMidiNoteList previous = clip->midi_notes;
    clip->midi_notes = *replacement;
    if (!engine_request_rebuild_sources(engine)) {
        clip->midi_notes = previous;
        engine_midi_note_list_free(replacement);
        return false;
    }
    engine_midi_note_list_free(&previous);
    engine_midi_note_list_init(replacement);
    return true;
}

// Prepares the add note edit before exposing changed notes or output indices.
bool engine_clip_midi_add_note(Engine* engine, int track_index, int clip_index, EngineMidiNote note, int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || !engine_midi_note_fits_duration(&note, clip->duration_frames)) return false;
    EngineMidiNoteList replacement = {0};
    if (!engine_midi_note_list_set(&replacement, clip->midi_notes.notes, clip->midi_notes.note_count)) return false;
    int result = 0;
    if (!engine_midi_note_list_insert(&replacement, note, &result)) {
        engine_midi_note_list_free(&replacement);
        return false;
    }
    if (!engine_midi_commit_notes(engine, clip, &replacement)) return false;
    if (out_note_index) *out_note_index = result;
    return true;
}

// Prepares the update note edit before exposing changed notes or output indices.
bool engine_clip_midi_update_note(Engine* engine, int track_index, int clip_index, int note_index, EngineMidiNote note, int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || !engine_midi_note_fits_duration(&note, clip->duration_frames)) return false;
    EngineMidiNoteList replacement = {0};
    if (!engine_midi_note_list_set(&replacement, clip->midi_notes.notes, clip->midi_notes.note_count)) return false;
    int result = 0;
    if (!engine_midi_note_list_update(&replacement, note_index, note, &result)) {
        engine_midi_note_list_free(&replacement);
        return false;
    }
    if (!engine_midi_commit_notes(engine, clip, &replacement)) return false;
    if (out_note_index) *out_note_index = result;
    return true;
}

// Prepares the remove note edit before exposing changed notes or output indices.
bool engine_clip_midi_remove_note(Engine* engine, int track_index, int clip_index, int note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || note_index < 0 || note_index >= clip->midi_notes.note_count) return false;
    EngineMidiNoteList replacement = {0};
    if (!engine_midi_note_list_set(&replacement, clip->midi_notes.notes, clip->midi_notes.note_count)) return false;
    if (!engine_midi_note_list_remove(&replacement, note_index)) {
        engine_midi_note_list_free(&replacement);
        return false;
    }
    if (!engine_midi_commit_notes(engine, clip, &replacement)) return false;
    return true;
}

// Replaces all notes with a validated independent list and retains old notes on rejection.
bool engine_clip_midi_set_notes(Engine* engine, int track_index, int clip_index,
                                const EngineMidiNote* notes, int note_count) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) return false;
    EngineMidiNoteList replacement = {0};
    if (!engine_midi_note_list_set(&replacement, notes, note_count)) return false;
    if (!engine_midi_notes_fit_duration(&replacement, clip->duration_frames)) {
        engine_midi_note_list_free(&replacement);
        return false;
    }
    return engine_midi_commit_notes(engine, clip, &replacement);
}

// Looks up a control-owned track without implicit structural changes.
static EngineTrack* engine_midi_track_for_edit(Engine* engine, int index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || index < 0 || index >= engine->track_count) return NULL;
    return &engine->tracks[index];
}

// Restores track instrument metadata when a complete render revision cannot be prepared.
static bool engine_midi_commit_track(Engine* engine, EngineTrack* track, const EngineTrack* previous) {
    if (engine_request_rebuild_sources(engine)) return true;
    track->midi_instrument_enabled = previous->midi_instrument_enabled;
    track->midi_instrument_preset = previous->midi_instrument_preset;
    track->midi_instrument_params = previous->midi_instrument_params;
    return false;
}

// Restores clip instrument metadata when a complete render revision cannot be prepared.
static bool engine_midi_commit_clip(Engine* engine, EngineClip* clip, const EngineClip* previous) {
    if (engine_request_rebuild_sources(engine)) return true;
    clip->instrument_inherits_track = previous->instrument_inherits_track;
    clip->instrument_preset = previous->instrument_preset;
    clip->instrument_params = previous->instrument_params;
    return false;
}

// Rejects non-finite instrument values before sanitization or publication.
static bool engine_midi_params_finite(EngineInstrumentParams params) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (!isfinite(engine_instrument_params_get(params, (EngineInstrumentParamId)i))) return false;
    return true;
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

// Publishes a track preset and its defaults or restores the previous instrument settings.
bool engine_track_midi_set_instrument_preset(Engine* engine,
                                             int track_index,
                                             EngineInstrumentPresetId preset) {
    EngineTrack* track = engine_midi_track_for_edit(engine, track_index);
    if (!track) {
        return false;
    }
    EngineInstrumentPresetId clamped = engine_instrument_preset_clamp(preset);
    EngineTrack previous = *track;
    if (track->midi_instrument_enabled &&
        track->midi_instrument_preset == clamped) {
        return true;
    }
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = clamped;
    track->midi_instrument_params = engine_instrument_default_params(clamped);
    return engine_midi_commit_track(engine, track, &previous);
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

// Publishes finite track instrument parameters or restores the previous settings.
bool engine_track_midi_set_instrument_params(Engine* engine,
                                             int track_index,
                                             EngineInstrumentParams params) {
    EngineTrack* track = engine_midi_track_for_edit(engine, track_index);
    if (!track || !engine_midi_params_finite(params)) {
        return false;
    }
    EngineInstrumentPresetId preset = engine_instrument_preset_clamp(track->midi_instrument_preset);
    EngineInstrumentParams clamped = engine_instrument_params_sanitize(preset, params);
    if (track->midi_instrument_enabled &&
        engine_instrument_params_equal(track->midi_instrument_params, clamped)) {
        return true;
    }
    EngineTrack previous = *track;
    track->midi_instrument_enabled = true;
    track->midi_instrument_params = clamped;
    return engine_midi_commit_track(engine, track, &previous);
}

// Publishes the track instrument enabled state or restores it on rejection.
bool engine_track_midi_set_instrument_enabled(Engine* engine, int track_index, bool enabled) {
    EngineTrack* track = engine_midi_track_for_edit(engine, track_index);
    if (!track) {
        return false;
    }
    if (track->midi_instrument_enabled == enabled) {
        return true;
    }
    EngineTrack previous = *track;
    track->midi_instrument_enabled = enabled;
    return engine_midi_commit_track(engine, track, &previous);
}

// Publishes clip inheritance changes without retaining a rejected setting.
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
    EngineClip previous = *clip;
    clip->instrument_inherits_track = inherits_track;
    return engine_midi_commit_clip(engine, clip, &previous);
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

// Publishes a clip preset override and defaults or restores the prior override.
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
    EngineClip previous = *clip;
    clip->instrument_preset = clamped;
    clip->instrument_params = engine_instrument_default_params(clamped);
    clip->instrument_inherits_track = false;
    return engine_midi_commit_clip(engine, clip, &previous);
}

EngineInstrumentParams engine_clip_midi_instrument_params(const EngineClip* clip) {
    EngineInstrumentPresetId preset = engine_clip_midi_instrument_preset(clip);
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return engine_instrument_default_params(preset);
    }
    return engine_instrument_params_sanitize(preset, clip->instrument_params);
}

// Publishes a finite clip parameter override or restores its prior inheritance and settings.
bool engine_clip_midi_set_instrument_params(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentParams params) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || !engine_midi_params_finite(params)) {
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
    EngineClip previous = *clip;
    clip->instrument_preset = preset;
    clip->instrument_params = clamped;
    clip->instrument_inherits_track = false;
    return engine_midi_commit_clip(engine, clip, &previous);
}

// Validates one instrument parameter before applying the transactional override.
bool engine_clip_midi_set_instrument_param(Engine* engine,
                                           int track_index,
                                           int clip_index,
                                           EngineInstrumentParamId param,
                                           float value) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || param < 0 || param >= ENGINE_INSTRUMENT_PARAM_COUNT || !isfinite(value)) {
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
