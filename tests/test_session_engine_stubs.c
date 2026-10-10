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

// Instrument identities, physical settings and defaults are linked from the production catalog.
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

// Serialization-only fixtures do not run a control/audio thread pair.
bool engine_is_control_thread(const Engine* engine) { return engine != NULL; }
// Supplies neutral EQ metadata for serialization-only fixtures.
bool engine_get_eq_curve(const Engine* engine, int track_index, EngineEqCurve* out) {
    (void)track_index;
    if (!engine || !out) return false;
    *out = (EngineEqCurve){0};
    return true;
}

// Supplies the display cursor for serializer-only fixtures without a live endpoint.
uint64_t engine_get_presentation_frame(const Engine* engine) { return engine_get_transport_frame(engine); }
