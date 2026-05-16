#pragma once

#include "app_state.h"
#include "ui/midi_editor.h"

#include <stdbool.h>
#include <stdint.h>

struct AppState;
struct EngineClip;

bool midi_editor_point_in_panel(const struct AppState* state, int x, int y);
bool midi_editor_get_fresh_selection(const struct AppState* state,
                                     MidiEditorSelection* selection,
                                     MidiEditorLayout* layout);
const struct EngineClip* midi_editor_clip_from_indices(const struct AppState* state,
                                                       int track_index,
                                                       int clip_index);
bool midi_editor_snapshot_notes(const struct EngineClip* clip, EngineMidiNote** out_notes, int* out_count);
void midi_editor_free_notes(EngineMidiNote** notes, int* count);
bool midi_editor_selection_matches_ui(const struct AppState* state,
                                      const MidiEditorSelection* selection);
void midi_editor_clear_note_selection(struct AppState* state);
void midi_editor_clear_marquee_state(struct AppState* state);
void midi_editor_clear_note_press_pending(struct AppState* state);
int midi_editor_effective_selected_note_count(const struct AppState* state,
                                              const MidiEditorSelection* selection,
                                              int note_count);
bool midi_editor_note_index_selected(const struct AppState* state,
                                     const MidiEditorSelection* selection,
                                     int note_index);
void midi_editor_build_effective_selection_mask(const struct AppState* state,
                                                const MidiEditorSelection* selection,
                                                int note_count,
                                                bool* out_mask);
void midi_editor_focus_first_selected_note(struct AppState* state, int note_count);
bool midi_editor_begin_note_undo(struct AppState* state, const MidiEditorSelection* selection);
bool midi_editor_commit_note_undo(struct AppState* state);
bool midi_editor_push_note_undo(struct AppState* state,
                                const MidiEditorSelection* selection,
                                EngineMidiNote* before_notes,
                                int before_count);
bool midi_editor_apply_instrument_preset(struct AppState* state,
                                         const MidiEditorSelection* selection,
                                         EngineInstrumentPresetId preset);
bool midi_editor_begin_instrument_undo(struct AppState* state,
                                       const MidiEditorSelection* selection);
bool midi_editor_commit_instrument_undo(struct AppState* state);
uint64_t midi_editor_min_note_duration(uint64_t clip_frames);

uint64_t midi_editor_clip_frames(const struct EngineClip* clip);
void midi_editor_fit_viewport(struct AppState* state, const MidiEditorSelection* selection);
bool midi_editor_seek_to_editor_x(struct AppState* state,
                                  const MidiEditorSelection* selection,
                                  const MidiEditorLayout* layout,
                                  int x);
bool midi_editor_pointer_over_editor_grid_or_ruler(const MidiEditorLayout* layout, int x, int y);
bool midi_editor_pointer_over_pitch_area(const MidiEditorLayout* layout, int x, int y);
bool midi_editor_pan_viewport(struct AppState* state,
                              const MidiEditorSelection* selection,
                              const MidiEditorLayout* layout,
                              int direction_steps);
bool midi_editor_zoom_viewport(struct AppState* state,
                               const MidiEditorSelection* selection,
                               const MidiEditorLayout* layout,
                               int anchor_x,
                               float zoom_factor);
bool midi_editor_pan_pitch_viewport(struct AppState* state,
                                    const MidiEditorSelection* selection,
                                    const MidiEditorLayout* layout,
                                    int direction_steps);
bool midi_editor_zoom_pitch_viewport(struct AppState* state,
                                     const MidiEditorSelection* selection,
                                     const MidiEditorLayout* layout,
                                     int anchor_y,
                                     int row_delta);
bool midi_editor_fit_pitch_viewport_to_selected_notes(struct AppState* state,
                                                      const MidiEditorSelection* selection,
                                                      const MidiEditorLayout* layout);
int midi_editor_current_quantize_division(const struct AppState* state);
bool midi_editor_adjust_quantize_division(struct AppState* state, int delta);
uint64_t midi_editor_quantize_step_frames_at(const struct AppState* state,
                                             const struct EngineClip* clip,
                                             uint64_t frame);
uint64_t midi_editor_quantize_relative_frame(const struct AppState* state,
                                             const struct EngineClip* clip,
                                             uint64_t frame,
                                             bool force);
uint64_t midi_editor_default_note_duration(const struct AppState* state, const struct EngineClip* clip);
uint64_t midi_editor_snap_relative_frame(const struct AppState* state,
                                         const struct EngineClip* clip,
                                         uint64_t frame);
bool midi_editor_transport_relative_frame(const struct AppState* state,
                                          const struct EngineClip* clip,
                                          bool clamp_to_region,
                                          uint64_t* out_frame);

float midi_editor_clamp_velocity(float velocity);
float midi_editor_current_default_velocity(const struct AppState* state);
void midi_editor_set_default_velocity(struct AppState* state, float velocity);
void midi_editor_clear_qwerty_active_notes(struct AppState* state);
void midi_editor_complete_all_qwerty_notes(struct AppState* state);
bool midi_editor_toggle_qwerty_test(struct AppState* state);
bool midi_editor_adjust_octave(struct AppState* state, int delta);
bool midi_editor_adjust_default_velocity(struct AppState* state, float delta);
bool midi_editor_handle_qwerty_event(struct AppState* state, const SDL_Event* event);

void midi_editor_select_matching_notes_after_set(struct AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 const EngineMidiNote* selected_notes,
                                                 int selected_count);
bool midi_editor_quantize_selected_note(struct AppState* state);
bool midi_editor_delete_selected_note(struct AppState* state);
bool midi_editor_handle_clipboard_keydown(struct AppState* state, const SDL_Event* event);
bool midi_editor_handle_note_command_keydown(struct AppState* state, const SDL_Event* event);

void midi_editor_select_note(struct AppState* state,
                             const MidiEditorSelection* selection,
                             int note_index);
bool midi_editor_note_is_selected(const struct AppState* state,
                                  const MidiEditorSelection* selection,
                                  int note_index);
void midi_editor_clear_hover_note(struct AppState* state);
void midi_editor_update_hover_note(struct AppState* state,
                                   const MidiEditorSelection* selection,
                                   const MidiEditorLayout* layout,
                                   int x,
                                   int y);
void midi_editor_update_marquee_preview(struct AppState* state,
                                        const MidiEditorSelection* selection,
                                        const MidiEditorLayout* layout,
                                        int x,
                                        int y);
bool midi_editor_begin_marquee(struct AppState* state,
                               const MidiEditorSelection* selection,
                               int x,
                               int y,
                               bool additive);
bool midi_editor_commit_marquee(struct AppState* state);
bool midi_editor_set_drag_state(struct AppState* state,
                                const MidiEditorSelection* selection,
                                MidiEditorDragMode mode,
                                int note_index,
                                int x,
                                int y,
                                EngineMidiNote note);
void midi_editor_clear_move_group_drag(struct AppState* state);
bool midi_editor_configure_move_group_drag(struct AppState* state,
                                           const MidiEditorSelection* selection,
                                           int note_index);
void midi_editor_end_drag(struct AppState* state);
void midi_editor_clear_shift_note_pending(struct AppState* state);
bool midi_editor_begin_shift_note_pending(struct AppState* state,
                                          const MidiEditorSelection* selection,
                                          int note_index,
                                          int x,
                                          int y);
bool midi_editor_update_shift_note_pending(struct AppState* state, int x, int y);
bool midi_editor_commit_shift_note_pending(struct AppState* state);
bool midi_editor_begin_note_press_pending(struct AppState* state,
                                          const MidiEditorSelection* selection,
                                          int note_index,
                                          MidiEditorNoteHitPart part,
                                          int x,
                                          int y,
                                          bool group_candidate);
bool midi_editor_update_note_press_pending(struct AppState* state, int x, int y);
bool midi_editor_commit_note_press_pending(struct AppState* state);
bool midi_editor_update_drag(struct AppState* state, int mouse_x, int mouse_y);
