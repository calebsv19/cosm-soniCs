#pragma once

#include "config.h"
#include "effects/effects_manager.h"
#include "engine/automation.h"
#include "engine/engine.h"
#include "engine/fade_curve.h"
#include "engine/midi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SESSION_DOCUMENT_VERSION 20
#define SESSION_PATH_MAX 512
#define SESSION_NAME_MAX 128
#define SESSION_FX_NAME_MAX 64
#define SESSION_MEDIA_ID_MAX 33

// Captures serialized automation point data for session persistence.
typedef struct {
    uint64_t frame;
    float value;
} SessionAutomationPoint;

// Captures serialized automation lane data for session persistence.
typedef struct {
    EngineAutomationTarget target;
    int point_count;
    SessionAutomationPoint* points;
} SessionAutomationLane;

// Captures serialized clip data for session persistence.
typedef struct {
    EngineClipKind kind;
    char media_id[SESSION_MEDIA_ID_MAX];
    char media_path[SESSION_PATH_MAX];
    char name[SESSION_NAME_MAX];
    uint64_t start_frame;
    uint64_t duration_frames;
    uint64_t offset_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    SessionAutomationLane* automation_lanes;
    int automation_lane_count;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
    EngineMidiNote* midi_notes;
    int midi_note_count;
    float gain;
    bool selected;
} SessionClip;

typedef struct {
    bool enabled;
    uint64_t start_frame;
    uint64_t end_frame;
} SessionLoopState;

typedef struct {
    float visible_seconds;
    float window_start_seconds;
    float vertical_scale;
    bool show_all_grid_lines;
    bool view_in_beats;
    int follow_mode;
    uint64_t playhead_frame;
} SessionTimelineView;

typedef struct {
    int view_mode;
    int selected_index;
    int open_index;
    int list_detail_mode;
    int eq_view_mode;
    int meter_scope_mode;
    int meter_lufs_mode;
    int meter_spectrogram_mode;
    struct {
        bool enabled;
        float freq_hz;
        float slope;
    } low_cut;
    struct {
        bool enabled;
        float freq_hz;
        float slope;
    } high_cut;
    struct {
        bool enabled;
        float freq_hz;
        float gain_db;
        float q_width;
    } bands[4];
} SessionEffectsPanelState;

typedef struct {
    bool visible;
    int track_index;
    int clip_index;
    bool view_source;
    float zoom;
    float scroll;
} SessionClipInspectorState;

typedef struct {
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
} SessionLayoutState;

typedef struct {
    char directory[SESSION_PATH_MAX];
    int selected_index;
} SessionLibraryState;

typedef struct {
    char input_root[SESSION_PATH_MAX];
    char output_root[SESSION_PATH_MAX];
    char library_copy_root[SESSION_PATH_MAX];
} SessionDataPathState;

typedef struct {
    bool enabled;
    float freq_hz;
    float slope;
} SessionEqCut;

typedef struct {
    bool enabled;
    float freq_hz;
    float gain_db;
    float q_width;
} SessionEqBand;

typedef struct {
    SessionEqCut low_cut;
    SessionEqCut high_cut;
    SessionEqBand bands[4];
} SessionEqCurve;

// Captures serialized effect instance parameters for session persistence.
typedef struct SessionFxInstance {
    FxTypeId type;
    bool enabled;
    uint32_t param_count;
    float params[FX_MAX_PARAMS];
    FxParamMode param_mode[FX_MAX_PARAMS];
    float param_beats[FX_MAX_PARAMS];
    uint32_t param_id_count;
    char param_ids[FX_MAX_PARAMS][32];
    float param_values_by_id[FX_MAX_PARAMS];
    FxParamMode param_modes_by_id[FX_MAX_PARAMS];
    float param_beats_by_id[FX_MAX_PARAMS];
    char name[SESSION_FX_NAME_MAX];
} SessionFxInstance;

typedef struct {
    char name[SESSION_NAME_MAX];
    float gain;
    float pan;
    bool muted;
    bool solo;
    SessionEqCurve eq;
    int clip_count;
    SessionClip* clips;
    SessionFxInstance* fx;
    int fx_count;
} SessionTrack;

typedef struct {
    float bpm;
    int ts_num;
    int ts_den;
} SessionTempo;

// Captures serialized tempo change data for session persistence.
typedef struct {
    float beat;
    float bpm;
} SessionTempoEvent;

// Captures serialized time signature change data for session persistence.
typedef struct {
    float beat;
    int ts_num;
    int ts_den;
} SessionTimeSignatureEvent;

typedef struct {
    uint32_t version;
    EngineRuntimeConfig engine;
    SessionTempo tempo;
    SessionTempoEvent* tempo_events;
    int tempo_event_count;
    SessionTimeSignatureEvent* time_signature_events;
    int time_signature_event_count;
    SessionLoopState loop;
    SessionTimelineView timeline;
    int selected_track_index;
    int selected_clip_index;
    SessionEffectsPanelState effects_panel;
    SessionClipInspectorState clip_inspector;
    SessionLayoutState layout;
    SessionLibraryState library;
    SessionDataPathState data_paths;
    bool transport_playing;
    uint64_t transport_frame;
    SessionTrack* tracks;
    int track_count;
    SessionFxInstance* master_fx;
    int master_fx_count;
} SessionDocument;

struct AppState;

void session_document_init(SessionDocument* doc);
void session_document_reset(SessionDocument* doc);
void session_document_free(SessionDocument* doc);
bool session_document_capture(const struct AppState* state, SessionDocument* out_doc);
bool session_document_validate(const SessionDocument* doc, char* error_message, size_t error_message_len);
bool session_document_write_file(const SessionDocument* doc, const char* path);
bool session_save_to_file(const struct AppState* state, const char* path);
bool session_document_read_file(const char* path, SessionDocument* out_doc);
bool session_apply_document(struct AppState* state, const SessionDocument* doc);
bool session_load_from_file(struct AppState* state, const char* path);
void session_apply_pending_master_fx(struct AppState* state);
void session_apply_pending_track_fx(struct AppState* state);
