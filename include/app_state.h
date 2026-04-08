#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "daw/data_paths.h"
#include "session.h"
#include "engine/engine.h"
#include "effects/param_utils.h"
#include "input/input_manager.h"
#include "ui/panes.h"
#include "ui/resize.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"
#include "ui/library_browser.h"
#include "audio/media_registry.h"
#include "ui/effects_panel_slot.h"
#include "ui/timeline_waveform.h"
#include "session/project_manager.h"
#include "time/tempo.h"
#include "undo/undo_manager.h"

#define TIMELINE_MAX_SELECTION 256

typedef struct EngineSamplerSource EngineSamplerSource;

// Holds queued master FX params for session application.
typedef struct {
    int track_index;
    int clip_index;
} TimelineSelectionEntry;

typedef struct {
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
    UIResizeZone zones[6];
    int zone_count;
    UIResizeDrag drag;
} UILayoutRuntime;

// Describes the active manipulation mode for a timeline drag gesture.
typedef enum {
    TIMELINE_DRAG_MODE_SLIDE = 0,
    TIMELINE_DRAG_MODE_SLIP,
    TIMELINE_DRAG_MODE_RIPPLE,
    TIMELINE_DRAG_MODE_TRIM_LEFT,
    TIMELINE_DRAG_MODE_TRIM_RIGHT,
    TIMELINE_DRAG_MODE_FADE_IN,
    TIMELINE_DRAG_MODE_FADE_OUT,
} TimelineDragMode;

// Tracks the in-progress mouse drag for timeline clips and their edit targets.
typedef struct {
    bool active;
    bool trimming_left;
    bool trimming_right;
    bool adjusting_fade_in;
    bool adjusting_fade_out;
    TimelineDragMode mode;
    int track_index;
    int clip_index;
    int destination_track_index;
    int start_mouse_x;
    float start_mouse_seconds;
    float start_right_seconds;
    float current_start_seconds;
    float current_duration_seconds;
    bool started_moving;
    uint64_t initial_start_frames;
    uint64_t initial_offset_frames;
    uint64_t initial_duration_frames;
    uint64_t clip_total_frames;
    uint64_t initial_fade_in_frames;
    uint64_t initial_fade_out_frames;
    bool multi_move;
    int multi_clip_count;
    EngineSamplerSource* multi_samplers[TIMELINE_MAX_SELECTION];
    int multi_initial_track[TIMELINE_MAX_SELECTION];
    uint64_t multi_initial_start[TIMELINE_MAX_SELECTION];
    uint64_t multi_initial_offset[TIMELINE_MAX_SELECTION];
    EngineSamplerSource** ripple_targets;
    int ripple_target_count;
    int64_t ripple_last_delta_frames;
    bool pending_shift_select;
    bool pending_shift_remove;
    int pending_shift_track;
    int pending_shift_clip;
} TimelineDragState;

// Keeps editing state for numeric inspector fields that map to clip timing.
typedef struct {
    bool editing_timeline_start;
    bool editing_timeline_end;
    bool editing_timeline_length;
    bool editing_source_start;
    bool editing_source_end;
    bool editing_playback_rate;
    char timeline_start[32];
    char timeline_end[32];
    char timeline_length[32];
    char source_start[32];
    char source_end[32];
    char playback_rate[16];
    int cursor;
} ClipInspectorEditState;

// Tracks the right panel view/gesture state for clip waveform inspection.
typedef struct {
    float zoom;
    float scroll;
    bool dragging_window;
    bool trimming_left;
    bool trimming_right;
    bool view_source;
    bool use_kit_viz_waveform;
} ClipInspectorWaveformState;

// Stores the current clip inspector selections, edits, and interaction flags.
typedef struct {
    bool visible;
    int track_index;
    int clip_index;
    char name[ENGINE_CLIP_NAME_MAX];
    int name_scroll;
    float gain;
    float playback_rate;
    bool editing_name;
    int name_cursor;
    bool adjusting_gain;
    bool adjusting_fade_in;
    bool adjusting_fade_out;
    bool fade_drag_from_waveform;
    bool pending_fade_drag;
    bool pending_fade_in;
    bool pending_fade_from_waveform;
    int pending_fade_start_x;
    bool fade_in_selected;
    bool fade_out_selected;
    uint64_t fade_in_frames;
    uint64_t fade_out_frames;
    bool has_focus;
    bool phase_invert_l;
    bool phase_invert_r;
    bool normalize;
    bool reverse;
    ClipInspectorEditState edit;
    ClipInspectorWaveformState waveform;
} ClipInspectorState;

typedef enum {
    FX_PANEL_OVERLAY_CLOSED = 0,
    FX_PANEL_OVERLAY_CATEGORIES,
    FX_PANEL_OVERLAY_EFFECTS
} EffectsPanelOverlayLayer;

#define FX_PANEL_MAX_TYPES 64
#define FX_PANEL_MAX_CATEGORIES 12

typedef enum {
    FX_PANEL_TARGET_MASTER = 0,
    FX_PANEL_TARGET_TRACK
} EffectsPanelTarget;

typedef enum {
    FX_PANEL_VIEW_STACK = 0,
    FX_PANEL_VIEW_LIST
} EffectsPanelViewMode;

// Stores UI-facing metadata for a single effect type.
typedef struct {
    FxTypeId type_id;
    char name[32];
    uint32_t param_count;
    char param_names[FX_MAX_PARAMS][32];
    float param_defaults[FX_MAX_PARAMS];
    EffectParamSpec param_specs[FX_MAX_PARAMS];
} FxTypeUIInfo;

typedef struct {
    char name[32];
    int type_indices[FX_PANEL_MAX_TYPES];
    int type_count;
} FxCategoryUIInfo;

typedef struct {
    FxInstId id;
    FxTypeId type_id;
    uint32_t param_count;
    float param_values[FX_MAX_PARAMS];
    FxParamMode param_mode[FX_MAX_PARAMS];
    float param_beats[FX_MAX_PARAMS];
    bool enabled;
} FxSlotUIState;

typedef enum {
    FX_SNAPSHOT_CONTROL_NONE = 0,
    FX_SNAPSHOT_CONTROL_GAIN,
    FX_SNAPSHOT_CONTROL_PAN
} FxSnapshotControl;

typedef enum {
    FX_LIST_DETAIL_EFFECT = 0,
    FX_LIST_DETAIL_EQ,
    FX_LIST_DETAIL_METER
} EffectsPanelListDetailMode;

typedef enum {
    EQ_DETAIL_VIEW_MASTER = 0,
    EQ_DETAIL_VIEW_TRACK
} EffectsPanelEqDetailView;

#define FX_METER_CORR_HISTORY_POINTS 64
#define FX_METER_MID_SIDE_HISTORY_POINTS 64
#define FX_METER_VECTOR_HISTORY_POINTS 64
#define FX_METER_LEVEL_HISTORY_POINTS 64
#define FX_METER_LUFS_HISTORY_POINTS 64

// Selects how the vectorscope plots its phase view.
typedef enum {
    FX_METER_SCOPE_MID_SIDE = 0,
    FX_METER_SCOPE_LEFT_RIGHT
} EffectsMeterScopeMode;

// Selects which LUFS window to visualize in the meter detail view.
typedef enum {
    FX_METER_LUFS_MOMENTARY = 0,
    FX_METER_LUFS_SHORT_TERM,
    FX_METER_LUFS_INTEGRATED
} EffectsMeterLufsMode;

// Selects the color palette for the spectrogram meter.
typedef enum {
    FX_METER_SPECTROGRAM_WHITE_BLACK = 0,
    FX_METER_SPECTROGRAM_BLACK_WHITE,
    FX_METER_SPECTROGRAM_HEAT
} EffectsMeterSpectrogramMode;

// Holds recent meter samples for history visualizations.
typedef struct EffectsMeterHistory {
    FxInstId active_id;
    FxTypeId active_type;
    Uint32 last_sample_ticks;
    float corr_values[FX_METER_CORR_HISTORY_POINTS];
    int corr_head;
    int corr_count;
    float mid_values[FX_METER_MID_SIDE_HISTORY_POINTS];
    float side_values[FX_METER_MID_SIDE_HISTORY_POINTS];
    int mid_head;
    int mid_count;
    float vec_x[FX_METER_VECTOR_HISTORY_POINTS];
    float vec_y[FX_METER_VECTOR_HISTORY_POINTS];
    int vec_head;
    int vec_count;
    float peak_values[FX_METER_LEVEL_HISTORY_POINTS];
    float rms_values[FX_METER_LEVEL_HISTORY_POINTS];
    int level_head;
    int level_count;
    float lufs_m_values[FX_METER_LUFS_HISTORY_POINTS];
    float lufs_s_values[FX_METER_LUFS_HISTORY_POINTS];
    float lufs_i_values[FX_METER_LUFS_HISTORY_POINTS];
    int lufs_head;
    int lufs_count;
} EffectsMeterHistory;

typedef struct {
    bool eq_open;
    bool dragging;
    FxSnapshotControl active_control;
    float pan;
    float gain;
    bool muted;
    bool solo;
    Uint32 last_click_ticks;
    float list_scroll;
    float list_scroll_max;
    bool list_scroll_dragging;
    float list_scroll_drag_offset;
} EffectsPanelTrackSnapshotState;

typedef struct {
    bool hovered;
    bool dragging;
    bool pending_apply;
    Uint32 last_apply_ticks;
    SDL_Point last_mouse;
    bool spectrum_ready;
    float spectrum_smooth[ENGINE_SPECTRUM_BINS];
    float spectrum_norm_lo;
    float spectrum_norm_hi;
    bool spectrum_norm_ready;
    int spectrum_hold_frames;
    EffectsPanelEqDetailView view_mode;
    int last_track_index;
} EffectsPanelEqDetailState;

typedef enum {
    EQ_CURVE_HANDLE_NONE = 0,
    EQ_CURVE_HANDLE_POINT,
    EQ_CURVE_HANDLE_WIDTH,
    EQ_CURVE_HANDLE_CUT_LOW,
    EQ_CURVE_HANDLE_CUT_HIGH
} EqCurveHandle;

typedef struct {
    bool enabled;
    float freq_hz;
    float gain_db;
    float q_width;
} EqCurveBand;

typedef struct {
    bool enabled;
    float freq_hz;
    float slope;
} EqCurveCut;

typedef struct {
    EqCurveBand bands[4];
    EqCurveCut low_cut;
    EqCurveCut high_cut;
    int selected_band;
    EqCurveHandle selected_handle;
    int hover_band;
    EqCurveHandle hover_handle;
    int hover_toggle_band;
    bool hover_toggle_low;
    bool hover_toggle_high;
} EqCurveState;

typedef struct EffectsPanelState {
    bool initialized;
    EffectsPanelOverlayLayer overlay_layer;
    EffectsPanelTarget target;
    int target_track_index;
    char target_label[64];
    EffectsPanelViewMode view_mode;
    bool spec_panel_enabled; // Enables spec-driven FX panel layout for supported effects.
    int hovered_category_index;
    int hovered_effect_index;
    int active_category_index;
    int highlighted_slot_index;
    int hovered_toggle_slot_index;
    int selected_slot_index;
    bool focused;
    bool dragging_slider;
    int active_slot_index;
    int active_param_index;
    int list_open_slot_index;
    EffectsPanelListDetailMode list_detail_mode;
    Uint32 list_last_click_ticks;
    int list_last_click_index;
    bool restore_pending;
    int restore_selected_index;
    int restore_open_index;
    int overlay_scroll_index;
    FxTypeUIInfo types[FX_PANEL_MAX_TYPES];
    int type_count;
    FxCategoryUIInfo categories[FX_PANEL_MAX_CATEGORIES];
    int category_count;
    FxSlotUIState chain[FX_MASTER_MAX];
    int chain_count;
    EffectsSlotRuntime slot_runtime[FX_MASTER_MAX];
    EffectsPanelPreviewSlotState preview_slots[FX_MASTER_MAX]; // Tracks preview open state + history per slot.
    bool preview_all_open; // Tracks the last global preview toggle state.
    bool preview_toggle_hovered; // Tracks hover state for the global preview toggle.
    int param_scroll_drag_slot;
    Uint32 title_last_click_ticks;
    bool title_debug_last_click; // transient debug flag
    EffectsPanelTrackSnapshotState track_snapshot;
    EffectsPanelEqDetailState eq_detail;
    EffectsMeterHistory meter_history;
    EffectsMeterScopeMode meter_scope_mode;
    EffectsMeterLufsMode meter_lufs_mode;
    EffectsMeterSpectrogramMode meter_spectrogram_mode;
    FxInstId last_open_master_fx_id; // Tracks the last open master FX for quick switching.
    FxInstId* last_open_track_fx_ids; // Tracks last open FX per track.
    int last_open_track_fx_count;
    FxInstId pending_open_fx_id; // Tracks a queued open FX to apply after target switches.
    EqCurveState eq_curve;
    EqCurveState eq_curve_master;
    EqCurveState* eq_curve_tracks;
    int eq_curve_tracks_count;
} EffectsPanelState;

// Stores timeline header control hitboxes and hover/drag state.
typedef struct {
    SDL_Rect add_rect;
    SDL_Rect remove_rect;
    bool add_hovered;
    bool remove_hovered;
    SDL_Rect loop_toggle_rect;
    SDL_Rect snap_toggle_rect;
    SDL_Rect automation_toggle_rect;
    SDL_Rect automation_target_rect;
    SDL_Rect tempo_toggle_rect;
    SDL_Rect automation_label_toggle_rect;
    SDL_Rect loop_start_rect;
    SDL_Rect loop_end_rect;
    bool loop_toggle_hovered;
    bool snap_toggle_hovered;
    bool automation_toggle_hovered;
    bool automation_target_hovered;
    bool tempo_toggle_hovered;
    bool automation_label_toggle_hovered;
    bool loop_start_hovered;
    bool loop_end_hovered;
    bool adjusting_loop_start;
    bool adjusting_loop_end;
} TimelineControlsUI;

// Tracks the active automation target and selected point for editing.
typedef struct {
    EngineAutomationTarget target;
    int track_index;
    int clip_index;
    int point_index;
    bool dragging;
    bool dragging_from_inspector;
} AutomationUIState;

// Tracks selected tempo map event and drag state for the tempo overlay.
typedef struct {
    int event_index;
    bool dragging;
} TempoOverlayUIState;

typedef struct {
    bool editing;
    int track_index;
    char buffer[ENGINE_CLIP_NAME_MAX];
    char original[ENGINE_CLIP_NAME_MAX];
    int cursor;
} TrackNameEditor;

typedef struct {
    FxTypeId type_id;
    bool enabled;
    uint32_t param_count;
    float param_values[FX_MAX_PARAMS];
    FxParamMode param_mode[FX_MAX_PARAMS];
    float param_beats[FX_MAX_PARAMS];
    uint32_t param_id_count;
    char param_ids[FX_MAX_PARAMS][32];
    float param_values_by_id[FX_MAX_PARAMS];
    FxParamMode param_modes_by_id[FX_MAX_PARAMS];
    float param_beats_by_id[FX_MAX_PARAMS];
} PendingMasterFx;

typedef struct {
    SessionFxInstance fx[FX_MASTER_MAX];
    int fx_count;
} PendingTrackFxEntry;

typedef struct {
    bool has_name;
    char name[SESSION_NAME_MAX];
    char path[SESSION_PATH_MAX];
} ProjectState;

typedef struct {
    bool active;
    char buffer[SESSION_NAME_MAX];
    int cursor;
} ProjectSavePrompt;

typedef struct {
    bool active;
    ProjectInfo entries[64];
    int count;
    int selected_index;
    float scroll_offset;
    Uint32 last_click_ticks;
    int last_click_index;
} ProjectLoadModal;

typedef enum {
    TEMPO_FOCUS_NONE = 0,
    TEMPO_FOCUS_BPM,
    TEMPO_FOCUS_TS
} TempoFocus;

// Identifies which half of the time signature is active in the UI.
typedef enum {
    TEMPO_TS_PART_NONE = 0,
    TEMPO_TS_PART_NUM,
    TEMPO_TS_PART_DEN
} TempoTSPart;

// Captures tempo and time signature editing state for the transport UI.
typedef struct {
    TempoFocus focus;
    bool editing;
    char buffer[16];
    int cursor;
    TempoTSPart ts_part;
    char ts_buffer[8];
    int ts_cursor;
    Uint32 last_click_ticks;
    TempoFocus last_click_focus;
    TempoTSPart last_click_ts_part;
} TempoUIState;

typedef struct AppState AppState;

struct AppState {
    Pane panes[4];
    PaneManager pane_manager;
    int pane_count;
    int window_width;
    int window_height;
    int mouse_x;
    int mouse_y;
    EngineRuntimeConfig runtime_cfg;
    DawDataPaths data_paths;
    Engine* engine;
    TransportUI transport_ui;
    UILayoutRuntime layout_runtime;
    MediaRegistry media_registry;
    LibraryBrowser library;
    int drag_library_index;
    bool dragging_library;
    InputManager input_manager;
    int active_track_index;
    int selected_track_index;
    int selected_clip_index;
    int selection_count;
    TimelineSelectionEntry selection[TIMELINE_MAX_SELECTION];
    TimelineDragState timeline_drag;
    ClipInspectorState inspector;
    EffectsPanelState effects_panel;
    TimelineControlsUI timeline_controls;
    TrackNameEditor track_name_editor;
    float timeline_visible_seconds;
    float timeline_window_start_seconds;
    float timeline_vertical_scale;
    TimelineFollowMode timeline_follow_mode;
    bool timeline_follow_override;
    bool timeline_hovered;
    bool timeline_drop_active;
    float timeline_drop_seconds;
    float timeline_drop_seconds_snapped;
    float timeline_drop_preview_duration;
    char timeline_drop_label[LIBRARY_NAME_MAX];
    bool timeline_show_all_grid_lines;
    bool timeline_view_in_beats;
    bool timeline_snap_enabled;
    bool timeline_automation_mode;
    bool timeline_automation_labels_enabled;
    bool timeline_tempo_overlay_enabled;
    AutomationUIState automation_ui;
    TempoOverlayUIState tempo_overlay_ui;
    bool timeline_marquee_active;
    SDL_Rect timeline_marquee_rect;
    bool timeline_marquee_extend;
    int timeline_marquee_start_x;
    int timeline_marquee_start_y;
    int timeline_drop_track_index;
    bool loop_enabled;
    uint64_t loop_start_frame;
    uint64_t loop_end_frame;
    bool loop_restart_pending;
    bool reset_meter_history_on_seek; // Debug toggle to clear meter histories on transport jumps.
    bool bounce_requested;
    bool bounce_active;
    uint64_t bounce_progress_frames;
    uint64_t bounce_total_frames;
    uint64_t bounce_start_frame;
    uint64_t bounce_end_frame;
    bool engine_logging_enabled;
    bool cache_logging_enabled;
    bool timing_logging_enabled;
    PendingMasterFx pending_master_fx[FX_MASTER_MAX];
    int pending_master_fx_count;
    bool pending_master_fx_dirty;
    PendingTrackFxEntry* pending_track_fx;
    int pending_track_fx_count;
    bool pending_track_fx_dirty;
    TempoState tempo;
    TempoMap tempo_map;
    TimeSignatureMap time_signature_map;
    TempoUIState tempo_ui;
    ProjectState project;
    ProjectSavePrompt project_prompt;
    ProjectLoadModal project_load;
    WaveformCache waveform_cache;
    UndoManager undo;
};
