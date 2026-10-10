#include "engine/engine_internal.h"
// Restores track metadata and removes a MIDI region with one publication, retaining both on failure.
bool engine_remove_midi_clip_restore_track(Engine* engine, int t, int c, bool enabled, EngineInstrumentPresetId preset, EngineInstrumentParams params) {
    if (!engine || !engine_is_control_thread(engine) || t < 0 || t >= engine->track_count || c < 0 ||
        c >= engine->tracks[t].clip_count || engine->tracks[t].clips[c].kind != ENGINE_CLIP_KIND_MIDI) return false;
    EngineTrack* track = &engine->tracks[t];
    bool previous_enabled = track->midi_instrument_enabled;
    EngineInstrumentPresetId previous_preset = track->midi_instrument_preset;
    EngineInstrumentParams previous_params = track->midi_instrument_params;
    track->midi_instrument_enabled = enabled; track->midi_instrument_preset = preset; track->midi_instrument_params = params;
    if (engine_remove_clip(engine, t, c)) return true;
    track->midi_instrument_enabled = previous_enabled; track->midi_instrument_preset = previous_preset; track->midi_instrument_params = previous_params;
    return false;
}
