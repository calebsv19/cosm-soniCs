#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "effects/effects_manager.h"
#include "engine/engine.h"
#include "session.h"
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
    UNDO_CMD_TRACK_EDIT,
    UNDO_CMD_TRACK_RENAME,
    UNDO_CMD_FX_EDIT,
    UNDO_CMD_EQ_CURVE,
    UNDO_CMD_TRACK_SNAPSHOT,
    UNDO_CMD_LIBRARY_RENAME
} UndoCommandType;

// Stores clip parameters for undo/redo transforms.
typedef struct {
    struct EngineSamplerSource* sampler;
    int track_index;
    uint64_t start_frame;
    uint64_t offset_frames;
    uint64_t duration_frames;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    EngineFadeCurve fade_in_curve;
    EngineFadeCurve fade_out_curve;
    float gain;
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

typedef struct {
    struct EngineSamplerSource* sampler;
    int track_index;
    char before_name[ENGINE_CLIP_NAME_MAX];
    char after_name[ENGINE_CLIP_NAME_MAX];
} UndoClipRename;

typedef struct {
    int count;
    UndoClipState* before;
    UndoClipState* after;
} UndoMultiClipTransform;

typedef struct {
    int track_index;
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
    SessionEqCurve before;
    SessionEqCurve after;
} UndoEqCurveEdit;

typedef struct {
    bool is_master;
    int track_index;
    float gain_before;
    float gain_after;
    float pan_before;
    float pan_after;
    bool muted_before;
    bool muted_after;
    bool solo_before;
    bool solo_after;
} UndoTrackSnapshotEdit;

typedef struct {
    char directory[SESSION_PATH_MAX];
    char before_name[LIBRARY_NAME_MAX];
    char after_name[LIBRARY_NAME_MAX];
} UndoLibraryRename;

typedef struct {
    UndoCommandType type;
    union {
        UndoClipTransform clip_transform;
        UndoClipAddRemove clip_add_remove;
        UndoClipRename clip_rename;
        UndoMultiClipTransform multi_clip_transform;
        UndoAutomationEdit automation_edit;
        UndoTrackEdit track_edit;
        UndoTrackRename track_rename;
        UndoFxEdit fx_edit;
        UndoEqCurveEdit eq_curve_edit;
        UndoTrackSnapshotEdit track_snapshot_edit;
        UndoLibraryRename library_rename;
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
