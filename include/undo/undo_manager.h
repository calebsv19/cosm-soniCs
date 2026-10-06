#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "effects/effects_manager.h"
#include "engine/engine.h"
#include "session.h"
#include "time/tempo.h"
#include "ui/library_browser.h"

struct EngineSamplerSource;
struct AppState;

typedef enum {
    UNDO_CMD_NONE = 0,
    UNDO_CMD_CLIP_TRANSFORM,
    UNDO_CMD_CLIP_ADD_REMOVE,
    UNDO_CMD_CLIP_RENAME,
    UNDO_CMD_MULTI_CLIP_TRANSFORM,
    UNDO_CMD_AUTOMATION_EDIT,
    UNDO_CMD_TEMPO_MAP_EDIT,
    UNDO_CMD_TRACK_EDIT,
    UNDO_CMD_TRACK_RENAME,
    UNDO_CMD_FX_EDIT,
    UNDO_CMD_EQ_CURVE,
    UNDO_CMD_TRACK_SNAPSHOT,
    UNDO_CMD_LIBRARY_RENAME,
    UNDO_CMD_MIDI_NOTE_EDIT,
    UNDO_CMD_CLIP_CONTENT
} UndoCommandType;

// Stores clip parameters for undo/redo transforms.
typedef struct {
    EngineClipKind kind;
    struct EngineSamplerSource* sampler;
    uint64_t creation_index;
    uint64_t track_runtime_id;
    int track_index;
    uint64_t start_frame;
    uint64_t offset_frames;
    uint64_t duration_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    float gain;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
    bool instrument_inherits_track;
    EngineMidiNote* midi_notes;
    int midi_note_count;
} UndoClipState;

typedef struct {
    UndoClipState before;
    UndoClipState after;
} UndoClipTransform;

typedef struct {
    bool added;
    int track_index;
    SessionClip clip;
    struct EngineSamplerSource* sampler;
} UndoClipAddRemove;

// Identifies an audio or MIDI rename target without borrowing sampler storage or mutable indices.
typedef struct {
    uint64_t creation_index;
    char before_name[ENGINE_CLIP_NAME_MAX];
    char after_name[ENGINE_CLIP_NAME_MAX];
} UndoClipRename;

// Captures authored settings of a gesture-created track without borrowing runtime-owned storage.
typedef struct {
    uint64_t runtime_id;
    EngineTrackSettings settings;
    EngineEqCurve eq;
    char name[ENGINE_CLIP_NAME_MAX];
} UndoCreatedTrack;

// Owns before/after selection identities for a complete clip-content action.
typedef struct {
    int created_start;
    int created_count;
    UndoCreatedTrack* created_tracks;
    int count;
    uint64_t* before;
    uint64_t* after;
} UndoClipContentSelection;

// Owns a compound transform and guards for any trailing tracks created by the gesture.
typedef struct {
    int created_start;
    int created_count;
    UndoCreatedTrack* created_tracks;
    int count;
    UndoClipState* before;
    UndoClipState* after;
} UndoMultiClipTransform;

typedef struct {
    int track_index;
    uint64_t left_track_id;
    uint64_t right_track_id;
    bool has_before;
    bool has_after;
    SessionTrack before;
    SessionTrack after;
} UndoTrackEdit;

// Stores automation lane snapshots for undoing automation edits.
typedef struct {
    int track_index;
    int clip_index;
    int before_lane_count;
    SessionAutomationLane* before_lanes;
    int after_lane_count;
    SessionAutomationLane* after_lanes;
} UndoAutomationEdit;

// Stores tempo map snapshots for undoing tempo edits.
typedef struct {
    int before_event_count;
    TempoEvent* before_events;
    int after_event_count;
    TempoEvent* after_events;
} UndoTempoMapEdit;

typedef struct {
    int track_index;
    char before_name[ENGINE_CLIP_NAME_MAX];
    char after_name[ENGINE_CLIP_NAME_MAX];
} UndoTrackRename;

typedef enum {
    UNDO_FX_TARGET_MASTER = 0,
    UNDO_FX_TARGET_TRACK
} UndoFxTarget;

typedef enum {
    UNDO_FX_EDIT_ADD = 0,
    UNDO_FX_EDIT_REMOVE,
    UNDO_FX_EDIT_REORDER,
    UNDO_FX_EDIT_PARAM,
    UNDO_FX_EDIT_ENABLE
} UndoFxEditKind;

typedef struct {
    UndoFxTarget target;
    int track_index;
    uint64_t track_runtime_id;
    UndoFxEditKind kind;
    FxInstId id;
    int before_index;
    int after_index;
    uint32_t param_index;
    SessionFxInstance before_state;
    SessionFxInstance after_state;
} UndoFxEdit;

typedef struct {
    bool is_master;
    int track_index;
    uint64_t track_runtime_id;
    SessionEqCurve before;
    SessionEqCurve after;
} UndoEqCurveEdit;

typedef struct {
    bool is_master;
    int track_index;
    uint64_t track_runtime_id;
    float gain_before;
    float gain_after;
    float pan_before;
    float pan_after;
    bool muted_before;
    bool muted_after;
    bool solo_before;
    bool solo_after;
    bool instrument_state_captured; // Distinguishes complete captures from older scalar-only commands.
    bool midi_instrument_enabled_before;
    bool midi_instrument_enabled_after;
    EngineInstrumentPresetId midi_instrument_preset_before;
    EngineInstrumentPresetId midi_instrument_preset_after;
    EngineInstrumentParams midi_instrument_params_before;
    EngineInstrumentParams midi_instrument_params_after;
} UndoTrackSnapshotEdit;

typedef struct {
    char directory[SESSION_PATH_MAX];
    char before_name[LIBRARY_NAME_MAX];
    char after_name[LIBRARY_NAME_MAX];
} UndoLibraryRename;

typedef struct {
    int track_index;
    uint64_t clip_creation_index;
    int before_note_count;
    EngineMidiNote* before_notes;
    int after_note_count;
    EngineMidiNote* after_notes;
} UndoMidiNoteEdit;

// Owns one edit command and optional complete clip-content states for destructive overlap history.
typedef struct {
    UndoCommandType type;
    EngineClipContentSnapshot* clip_content_before;
    EngineClipContentSnapshot* clip_content_after;
    union {
        UndoClipTransform clip_transform;
        UndoClipAddRemove clip_add_remove;
        UndoClipRename clip_rename;
        UndoMultiClipTransform multi_clip_transform;
        UndoClipContentSelection clip_content_selection;
        UndoAutomationEdit automation_edit;
        UndoTempoMapEdit tempo_map_edit;
        UndoTrackEdit track_edit;
        UndoTrackRename track_rename;
        UndoFxEdit fx_edit;
        UndoEqCurveEdit eq_curve_edit;
        UndoTrackSnapshotEdit track_snapshot_edit;
        UndoLibraryRename library_rename;
        UndoMidiNoteEdit midi_note_edit;
    } data;
} UndoCommand;

typedef struct {
    UndoCommand* undo_stack;
    int undo_count;
    int undo_capacity;
    UndoCommand* redo_stack;
    int redo_count;
    int redo_capacity;
    int max_commands;
    UndoCommand active_drag;
    bool active_drag_valid;
    uint64_t drag_serial; // Identifies each successful reservation across clear/cancel.
    const char* rejection_message; // Borrows a static message for the current rejection notice.
    uint32_t rejection_ticks;
} UndoManager;

void undo_manager_init(UndoManager* manager);
void undo_manager_free(UndoManager* manager);
void undo_manager_clear(UndoManager* manager);
void undo_manager_set_limit(UndoManager* manager, int max_commands);
bool undo_manager_push(UndoManager* manager, const UndoCommand* command);
bool undo_manager_begin_drag(UndoManager* manager, const UndoCommand* command);
bool undo_manager_commit_drag(UndoManager* manager, const UndoCommand* command);
void undo_manager_cancel_drag(UndoManager* manager);
bool undo_manager_can_undo(const UndoManager* manager);
bool undo_manager_can_redo(const UndoManager* manager);
bool undo_manager_undo(UndoManager* manager, struct AppState* state);
bool undo_manager_redo(UndoManager* manager, struct AppState* state);
bool undo_clip_state_from_engine_clip(const EngineClip* clip, int track_index, UndoClipState* out_state);
// Captures transform history with a stable destination track identity.
bool undo_clip_state_capture(const Engine* engine, const EngineClip* clip, int track_index, UndoClipState* out_state);
bool undo_clip_state_clone(UndoClipState* dst, const UndoClipState* src);
void undo_clip_state_clear(UndoClipState* state);

// Captures comparable authored track settings with initialized padding and no borrowed ownership.
void undo_created_track_capture(const EngineTrack* track, UndoCreatedTrack* out);

// Adds or removes one complete track only after reserving its retained history.
bool undo_manager_edit_track(struct AppState* state, int track_index, bool add);

// Binds track-addressed mixer, EQ and effect history to the current track lifetime.
bool undo_command_bind_track(struct AppState* state, UndoCommand* command);

// Reserves a complete discrete command before applying its engine edit.
bool undo_manager_apply_edit(struct AppState* state, UndoCommand* command);
// Adds an effect with pre-reserved history and an atomic captured result (-1 selects master).
FxInstId undo_manager_add_effect(struct AppState* state, int track_index, FxTypeId type);

// Records a short-lived static rejection message and returns false to the caller.
bool undo_manager_reject(UndoManager* manager, const char* message);
// Returns the current rejection notice while its five-second display interval is active.
const char* undo_manager_rejection_message(const UndoManager* manager);
