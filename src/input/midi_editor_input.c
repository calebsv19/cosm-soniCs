#include "input/midi_editor_input.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "input/timeline_snap.h"
#include "time/tempo.h"
#include "ui/layout.h"
#include "ui/midi_editor.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MIDI_EDITOR_DEFAULT_VELOCITY 0.80f
#define MIDI_EDITOR_VELOCITY_DRAG_PIXELS 120.0f

static const int k_midi_editor_quantize_divisions[] = {4, 8, 16, 32, 64};

static void midi_editor_select_note(AppState* state,
                                    const MidiEditorSelection* selection,
                                    int note_index);
static void midi_editor_clear_hover_note(AppState* state);
static void midi_editor_clear_move_group_drag(AppState* state);
static void midi_editor_clear_shift_note_pending(AppState* state);
static void midi_editor_end_drag(AppState* state);
static bool midi_editor_configure_move_group_drag(AppState* state,
                                                  const MidiEditorSelection* selection,
                                                  int note_index);

static float midi_editor_clamp_velocity(float velocity) {
    if (velocity < 0.0f) return 0.0f;
    if (velocity > 1.0f) return 1.0f;
    return velocity;
}

static uint64_t midi_editor_clip_frames(const EngineClip* clip) {
    return clip && clip->duration_frames > 0 ? clip->duration_frames : 1u;
}

static uint64_t midi_editor_clamp_frame(uint64_t frame, uint64_t max_frame) {
    return frame > max_frame ? max_frame : frame;
}

static void midi_editor_store_viewport(AppState* state,
                                       const MidiEditorSelection* selection,
                                       uint64_t start,
                                       uint64_t span) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    uint64_t clip_frames = midi_editor_clip_frames(selection->clip);
    if (span == 0 || span >= clip_frames) {
        start = 0;
        span = clip_frames;
    } else if (start + span > clip_frames || start + span < start) {
        start = clip_frames > span ? clip_frames - span : 0;
    }
    state->midi_editor_ui.viewport_track_index = selection->track_index;
    state->midi_editor_ui.viewport_clip_index = selection->clip_index;
    state->midi_editor_ui.viewport_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.viewport_start_frame = start;
    state->midi_editor_ui.viewport_span_frames = span;
}

static void midi_editor_fit_viewport(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    midi_editor_store_viewport(state, selection, 0, midi_editor_clip_frames(selection->clip));
}

static bool midi_editor_seek_to_editor_x(AppState* state,
                                         const MidiEditorSelection* selection,
                                         const MidiEditorLayout* layout,
                                         int x) {
    if (!state || !state->engine || !selection || !selection->clip || !layout) {
        return false;
    }
    uint64_t frame = 0;
    uint64_t clip_frames = midi_editor_clip_frames(selection->clip);
    if (!midi_editor_point_to_frame(layout, clip_frames, x, &frame)) {
        return false;
    }
    frame = midi_editor_clamp_frame(frame, clip_frames);
    uint64_t absolute_frame = selection->clip->timeline_start_frames + frame;
    bool was_playing = engine_transport_is_playing(state->engine);
    input_manager_reset_meter_history_on_seek(state);
    engine_transport_seek(state->engine, absolute_frame);
    if (was_playing) {
        engine_transport_play(state->engine);
    }
    return true;
}

static float midi_editor_current_default_velocity(const AppState* state) {
    if (!state || state->midi_editor_ui.default_velocity <= 0.0f) {
        return MIDI_EDITOR_DEFAULT_VELOCITY;
    }
    return midi_editor_clamp_velocity(state->midi_editor_ui.default_velocity);
}

static void midi_editor_set_default_velocity(AppState* state, float velocity) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.default_velocity = midi_editor_clamp_velocity(velocity);
}

static int midi_editor_clamp_octave_offset(int offset) {
    if (offset < -2) return -2;
    if (offset > 2) return 2;
    return offset;
}

static uint8_t midi_editor_apply_octave_offset(int base_note, int octave_offset) {
    int note = base_note + midi_editor_clamp_octave_offset(octave_offset) * 12;
    if (note < ENGINE_MIDI_NOTE_MIN) note = ENGINE_MIDI_NOTE_MIN;
    if (note > ENGINE_MIDI_NOTE_MAX) note = ENGINE_MIDI_NOTE_MAX;
    return (uint8_t)note;
}

static bool midi_editor_point_in_panel(const AppState* state, int x, int y) {
    if (!midi_editor_should_render(state)) {
        return false;
    }
    const Pane* pane = ui_layout_get_pane(state, 2);
    if (!pane) {
        return false;
    }
    SDL_Point p = {x, y};
    return SDL_PointInRect(&p, &pane->rect);
}

static bool midi_editor_get_fresh_selection(const AppState* state,
                                            MidiEditorSelection* selection,
                                            MidiEditorLayout* layout) {
    if (!midi_editor_get_selection(state, selection)) {
        return false;
    }
    if (layout) {
        midi_editor_compute_layout(state, layout);
        if (layout->grid_rect.w <= 0 || layout->grid_rect.h <= 0) {
            return false;
        }
    }
    return true;
}

static bool midi_editor_pointer_over_editor_grid_or_ruler(const MidiEditorLayout* layout, int x, int y) {
    if (!layout) {
        return false;
    }
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &layout->grid_rect) ||
           SDL_PointInRect(&point, &layout->time_ruler_rect);
}

static bool midi_editor_pan_viewport(AppState* state,
                                     const MidiEditorSelection* selection,
                                     const MidiEditorLayout* layout,
                                     int direction_steps) {
    if (!state || !selection || !selection->clip || !layout || direction_steps == 0) {
        return false;
    }
    uint64_t clip_frames = midi_editor_clip_frames(selection->clip);
    uint64_t span = layout->view_span_frames > 0 ? layout->view_span_frames : clip_frames;
    if (span >= clip_frames) {
        midi_editor_fit_viewport(state, selection);
        return true;
    }
    uint64_t step = span / 8u;
    if (step < 1u) {
        step = 1u;
    }
    uint64_t start = layout->view_start_frame;
    if (direction_steps > 0) {
        uint64_t delta = step * (uint64_t)direction_steps;
        uint64_t max_start = clip_frames > span ? clip_frames - span : 0;
        start = delta > max_start || start > max_start - delta ? max_start : start + delta;
    } else {
        uint64_t delta = step * (uint64_t)(-direction_steps);
        start = delta > start ? 0 : start - delta;
    }
    midi_editor_store_viewport(state, selection, start, span);
    return true;
}

static bool midi_editor_zoom_viewport(AppState* state,
                                      const MidiEditorSelection* selection,
                                      const MidiEditorLayout* layout,
                                      int anchor_x,
                                      float zoom_factor) {
    if (!state || !selection || !selection->clip || !layout || zoom_factor <= 0.0f) {
        return false;
    }
    uint64_t clip_frames = midi_editor_clip_frames(selection->clip);
    uint64_t old_span = layout->view_span_frames > 0 ? layout->view_span_frames : clip_frames;
    if (old_span > clip_frames) {
        old_span = clip_frames;
    }
    uint64_t min_span = clip_frames / 64u;
    if (min_span < 64u) {
        min_span = clip_frames < 64u ? 1u : 64u;
    }
    uint64_t new_span = (uint64_t)((double)old_span * (double)zoom_factor + 0.5);
    if (new_span < min_span) {
        new_span = min_span;
    }
    if (new_span > clip_frames) {
        new_span = clip_frames;
    }
    uint64_t anchor_frame = 0;
    if (!midi_editor_point_to_frame(layout, clip_frames, anchor_x, &anchor_frame)) {
        anchor_frame = layout->view_start_frame + old_span / 2u;
    }
    double ratio = layout->grid_rect.w > 0
        ? (double)(anchor_x - layout->grid_rect.x) / (double)layout->grid_rect.w
        : 0.5;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    double new_start_d = (double)anchor_frame - ratio * (double)new_span;
    uint64_t new_start = new_start_d <= 0.0 ? 0u : (uint64_t)(new_start_d + 0.5);
    if (new_start + new_span > clip_frames || new_start + new_span < new_start) {
        new_start = clip_frames > new_span ? clip_frames - new_span : 0;
    }
    midi_editor_store_viewport(state, selection, new_start, new_span);
    return true;
}

static const EngineClip* midi_editor_clip_from_indices(const AppState* state,
                                                       int track_index,
                                                       int clip_index) {
    if (!state || !state->engine) {
        return NULL;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return NULL;
    }
    const EngineTrack* track = &tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip;
}

static bool midi_editor_snapshot_notes(const EngineClip* clip, EngineMidiNote** out_notes, int* out_count) {
    if (!out_notes || !out_count) {
        return false;
    }
    *out_notes = NULL;
    *out_count = 0;
    if (!clip || engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }
    int count = engine_clip_midi_note_count(clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    if (count <= 0) {
        return true;
    }
    if (!notes) {
        return false;
    }
    EngineMidiNote* copy = (EngineMidiNote*)calloc((size_t)count, sizeof(EngineMidiNote));
    if (!copy) {
        return false;
    }
    memcpy(copy, notes, sizeof(EngineMidiNote) * (size_t)count);
    *out_notes = copy;
    *out_count = count;
    return true;
}

static void midi_editor_free_notes(EngineMidiNote** notes, int* count) {
    if (!notes || !count) {
        return;
    }
    free(*notes);
    *notes = NULL;
    *count = 0;
}

static bool midi_editor_selection_matches_ui(const AppState* state,
                                             const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.selected_track_index == selection->track_index &&
           state->midi_editor_ui.selected_clip_index == selection->clip_index &&
           state->midi_editor_ui.selected_clip_creation_index == selection->clip->creation_index;
}

static void midi_editor_clear_note_selection(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.selected_note_index = -1;
    memset(state->midi_editor_ui.selected_note_indices,
           0,
           sizeof(state->midi_editor_ui.selected_note_indices));
}

static void midi_editor_clear_marquee_state(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.marquee_active = false;
    state->midi_editor_ui.marquee_additive = false;
    state->midi_editor_ui.marquee_start_x = 0;
    state->midi_editor_ui.marquee_start_y = 0;
    state->midi_editor_ui.marquee_current_x = 0;
    state->midi_editor_ui.marquee_current_y = 0;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
}

static void midi_editor_clear_note_press_pending(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.note_press_pending = false;
    state->midi_editor_ui.note_press_pending_track_index = -1;
    state->midi_editor_ui.note_press_pending_clip_index = -1;
    state->midi_editor_ui.note_press_pending_clip_creation_index = 0;
    state->midi_editor_ui.note_press_pending_note_index = -1;
    state->midi_editor_ui.note_press_pending_x = 0;
    state->midi_editor_ui.note_press_pending_y = 0;
    state->midi_editor_ui.note_press_pending_part = MIDI_EDITOR_NOTE_HIT_NONE;
    state->midi_editor_ui.note_press_group_candidate = false;
}

static int midi_editor_effective_selected_note_count(const AppState* state,
                                                     const MidiEditorSelection* selection,
                                                     int note_count) {
    if (!midi_editor_selection_matches_ui(state, selection) || note_count <= 0) {
        return 0;
    }
    if (note_count > ENGINE_MIDI_NOTE_CAP) {
        note_count = ENGINE_MIDI_NOTE_CAP;
    }
    int count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            ++count;
        }
    }
    if (count == 0 &&
        state->midi_editor_ui.selected_note_index >= 0 &&
        state->midi_editor_ui.selected_note_index < note_count) {
        count = 1;
    }
    return count;
}

static bool midi_editor_note_index_selected(const AppState* state,
                                            const MidiEditorSelection* selection,
                                            int note_index) {
    if (!midi_editor_selection_matches_ui(state, selection) ||
        note_index < 0 ||
        note_index >= ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (state->midi_editor_ui.selected_note_indices[note_index]) {
        return true;
    }
    return state->midi_editor_ui.selected_note_index == note_index;
}

static void midi_editor_build_effective_selection_mask(const AppState* state,
                                                       const MidiEditorSelection* selection,
                                                       int note_count,
                                                       bool* out_mask) {
    if (!out_mask || note_count <= 0) {
        return;
    }
    memset(out_mask, 0, sizeof(bool) * (size_t)note_count);
    if (!midi_editor_selection_matches_ui(state, selection)) {
        return;
    }
    int bounded = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    int count = 0;
    for (int i = 0; i < bounded; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            out_mask[i] = true;
            ++count;
        }
    }
    if (count == 0 &&
        state->midi_editor_ui.selected_note_index >= 0 &&
        state->midi_editor_ui.selected_note_index < bounded) {
        out_mask[state->midi_editor_ui.selected_note_index] = true;
    }
}

static void midi_editor_focus_first_selected_note(AppState* state, int note_count) {
    if (!state) {
        return;
    }
    int bounded = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    state->midi_editor_ui.selected_note_index = -1;
    for (int i = 0; i < bounded; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            state->midi_editor_ui.selected_note_index = i;
            return;
        }
    }
}

static bool midi_editor_begin_note_undo(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = selection->track_index;
    cmd.data.midi_note_edit.clip_creation_index = selection->clip->creation_index;
    if (!midi_editor_snapshot_notes(selection->clip,
                                    &cmd.data.midi_note_edit.before_notes,
                                    &cmd.data.midi_note_edit.before_note_count)) {
        return false;
    }
    bool ok = undo_manager_begin_drag(&state->undo, &cmd);
    midi_editor_free_notes(&cmd.data.midi_note_edit.before_notes,
                           &cmd.data.midi_note_edit.before_note_count);
    return ok;
}

static bool midi_editor_commit_note_undo(AppState* state) {
    if (!state || !state->undo.active_drag_valid ||
        state->undo.active_drag.type != UNDO_CMD_MIDI_NOTE_EDIT) {
        return false;
    }
    UndoCommand cmd = {0};
    const UndoMidiNoteEdit* active = &state->undo.active_drag.data.midi_note_edit;
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = active->track_index;
    cmd.data.midi_note_edit.clip_creation_index = active->clip_creation_index;
    cmd.data.midi_note_edit.before_notes = active->before_notes;
    cmd.data.midi_note_edit.before_note_count = active->before_note_count;

    const EngineClip* clip = midi_editor_clip_from_indices(state, active->track_index, state->midi_editor_ui.drag_clip_index);
    if (!clip || clip->creation_index != active->clip_creation_index ||
        !midi_editor_snapshot_notes(clip,
                                    &cmd.data.midi_note_edit.after_notes,
                                    &cmd.data.midi_note_edit.after_note_count)) {
        undo_manager_cancel_drag(&state->undo);
        return false;
    }
    bool ok = undo_manager_commit_drag(&state->undo, &cmd);
    midi_editor_free_notes(&cmd.data.midi_note_edit.after_notes,
                           &cmd.data.midi_note_edit.after_note_count);
    return ok;
}

static bool midi_editor_push_note_undo(AppState* state,
                                       const MidiEditorSelection* selection,
                                       EngineMidiNote* before_notes,
                                       int before_count) {
    if (!state || !selection || !selection->clip) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_MIDI_NOTE_EDIT;
    cmd.data.midi_note_edit.track_index = selection->track_index;
    cmd.data.midi_note_edit.clip_creation_index = selection->clip->creation_index;
    cmd.data.midi_note_edit.before_notes = before_notes;
    cmd.data.midi_note_edit.before_note_count = before_count;
    bool ok = midi_editor_snapshot_notes(selection->clip,
                                         &cmd.data.midi_note_edit.after_notes,
                                         &cmd.data.midi_note_edit.after_note_count);
    if (ok) {
        ok = undo_manager_push(&state->undo, &cmd);
    }
    midi_editor_free_notes(&cmd.data.midi_note_edit.before_notes,
                           &cmd.data.midi_note_edit.before_note_count);
    midi_editor_free_notes(&cmd.data.midi_note_edit.after_notes,
                           &cmd.data.midi_note_edit.after_note_count);
    return ok;
}

static uint64_t midi_editor_min_note_duration(uint64_t clip_frames) {
    uint64_t min_duration = clip_frames / 64u;
    if (min_duration == 0) {
        min_duration = 1;
    }
    return min_duration;
}

static int midi_editor_sample_rate_for_state(const AppState* state) {
    int sample_rate = state && state->runtime_cfg.sample_rate > 0 ? state->runtime_cfg.sample_rate : 48000;
    const EngineRuntimeConfig* cfg = state && state->engine ? engine_get_config(state->engine) : NULL;
    if (cfg && cfg->sample_rate > 0) {
        sample_rate = cfg->sample_rate;
    }
    return sample_rate > 0 ? sample_rate : 48000;
}

static int midi_editor_quantize_division_index_for_value(int division) {
    int count = (int)(sizeof(k_midi_editor_quantize_divisions) /
                      sizeof(k_midi_editor_quantize_divisions[0]));
    for (int i = 0; i < count; ++i) {
        if (k_midi_editor_quantize_divisions[i] == division) {
            return i;
        }
    }
    return 2;
}

static int midi_editor_current_quantize_division(const AppState* state) {
    if (!state) {
        return 16;
    }
    int division = state->midi_editor_ui.quantize_division;
    int count = (int)(sizeof(k_midi_editor_quantize_divisions) /
                      sizeof(k_midi_editor_quantize_divisions[0]));
    for (int i = 0; i < count; ++i) {
        if (k_midi_editor_quantize_divisions[i] == division) {
            return division;
        }
    }
    return 16;
}

static double midi_editor_quantize_step_beats(const AppState* state) {
    int division = midi_editor_current_quantize_division(state);
    return division > 0 ? 4.0 / (double)division : 0.25;
}

static bool midi_editor_adjust_quantize_division(AppState* state, int delta) {
    if (!state || delta == 0) {
        return false;
    }
    int count = (int)(sizeof(k_midi_editor_quantize_divisions) /
                      sizeof(k_midi_editor_quantize_divisions[0]));
    int index = midi_editor_quantize_division_index_for_value(midi_editor_current_quantize_division(state));
    index += delta;
    if (index < 0) {
        index = 0;
    }
    if (index >= count) {
        index = count - 1;
    }
    state->midi_editor_ui.quantize_division = k_midi_editor_quantize_divisions[index];
    state->midi_editor_ui.instrument_menu_open = false;
    return true;
}

static uint64_t midi_editor_quantize_step_frames_at(const AppState* state,
                                                    const EngineClip* clip,
                                                    uint64_t frame) {
    if (!state || !clip) {
        return 1;
    }
    int sample_rate = midi_editor_sample_rate_for_state(state);
    double absolute_seconds = (double)(clip->timeline_start_frames + frame) / (double)sample_rate;
    double start_beat = tempo_map_seconds_to_beats(&state->tempo_map, absolute_seconds);
    double end_seconds = tempo_map_beats_to_seconds(&state->tempo_map,
                                                    start_beat + midi_editor_quantize_step_beats(state));
    double frames = (end_seconds - absolute_seconds) * (double)sample_rate;
    if (frames < 1.0) {
        frames = 1.0;
    }
    return (uint64_t)llround(frames);
}

static uint64_t midi_editor_quantize_relative_frame(const AppState* state,
                                                    const EngineClip* clip,
                                                    uint64_t frame,
                                                    bool force) {
    if (!state || !clip || (!force && !state->timeline_snap_enabled)) {
        return frame;
    }
    int sample_rate = midi_editor_sample_rate_for_state(state);
    if (sample_rate <= 0) {
        return frame;
    }
    double step = midi_editor_quantize_step_beats(state);
    if (step <= 0.0) {
        return frame;
    }
    double absolute_seconds = (double)(clip->timeline_start_frames + frame) / (double)sample_rate;
    double beat = tempo_map_seconds_to_beats(&state->tempo_map, absolute_seconds);
    double snapped_beat = floor(beat / step + 0.5) * step;
    double snapped_seconds = tempo_map_beats_to_seconds(&state->tempo_map, snapped_beat);
    double snapped_frames = snapped_seconds * (double)sample_rate - (double)clip->timeline_start_frames;
    if (snapped_frames < 0.0) {
        snapped_frames = 0.0;
    }
    if (snapped_frames > (double)clip->duration_frames) {
        snapped_frames = (double)clip->duration_frames;
    }
    return (uint64_t)llround(snapped_frames);
}

static uint64_t midi_editor_default_note_duration(const AppState* state, const EngineClip* clip) {
    uint64_t clip_frames = (clip && clip->duration_frames > 0) ? clip->duration_frames : 1u;
    uint64_t fallback = clip_frames / 16u;
    if (fallback == 0) {
        fallback = 1;
    }
    if (state && state->timeline_snap_enabled) {
        uint64_t frames = midi_editor_quantize_step_frames_at(state, clip, 0);
        if (frames > 0) {
            fallback = frames;
        }
    }
    if (fallback > clip_frames) {
        fallback = clip_frames;
    }
    return fallback > 0 ? fallback : 1u;
}

static uint64_t midi_editor_snap_relative_frame(const AppState* state,
                                                const EngineClip* clip,
                                                uint64_t frame) {
    if (!state || !clip || !state->timeline_snap_enabled) {
        return frame;
    }
    return midi_editor_quantize_relative_frame(state, clip, frame, false);
}

static bool midi_editor_qwerty_note_for_key(SDL_Keycode key, uint8_t* out_note) {
    uint8_t note = 0;
    switch (key) {
    case SDLK_z: note = 48; break; /* C3 */
    case SDLK_x: note = 50; break;
    case SDLK_c: note = 52; break;
    case SDLK_v: note = 53; break;
    case SDLK_b: note = 55; break;
    case SDLK_n: note = 57; break;
    case SDLK_m: note = 59; break;
    case SDLK_a: note = 60; break; /* C4 */
    case SDLK_w: note = 61; break;
    case SDLK_s: note = 62; break;
    case SDLK_e: note = 63; break;
    case SDLK_d: note = 64; break;
    case SDLK_f: note = 65; break;
    case SDLK_t: note = 66; break;
    case SDLK_g: note = 67; break;
    case SDLK_y: note = 68; break;
    case SDLK_h: note = 69; break;
    case SDLK_u: note = 70; break;
    case SDLK_j: note = 71; break;
    case SDLK_k: note = 72; break;
    default:
        return false;
    }
    if (out_note) {
        *out_note = note;
    }
    return true;
}

static bool midi_editor_transport_relative_frame(const AppState* state,
                                                 const EngineClip* clip,
                                                 bool clamp_to_region,
                                                 uint64_t* out_frame) {
    if (!state || !state->engine || !clip || !out_frame) {
        return false;
    }
    uint64_t transport_frame = engine_get_transport_frame(state->engine);
    if (transport_frame < clip->timeline_start_frames) {
        return false;
    }
    uint64_t relative = transport_frame - clip->timeline_start_frames;
    uint64_t clip_frames = clip->duration_frames > 0 ? clip->duration_frames : 1u;
    if (relative > clip_frames) {
        if (!clamp_to_region) {
            return false;
        }
        relative = clip_frames;
    }
    *out_frame = relative;
    return true;
}

static int midi_editor_find_active_qwerty_note(const AppState* state, SDL_Keycode key) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (state->midi_editor_ui.qwerty_active_notes[i].active &&
            state->midi_editor_ui.qwerty_active_notes[i].key == key) {
            return i;
        }
    }
    return -1;
}

static int midi_editor_find_free_qwerty_note_slot(const AppState* state) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (!state->midi_editor_ui.qwerty_active_notes[i].active) {
            return i;
        }
    }
    return -1;
}

static void midi_editor_clear_qwerty_active_notes(AppState* state) {
    if (!state) {
        return;
    }
    if (state->engine) {
        engine_midi_audition_all_notes_off(state->engine);
    }
    memset(state->midi_editor_ui.qwerty_active_notes,
           0,
           sizeof(state->midi_editor_ui.qwerty_active_notes));
}

static bool midi_editor_complete_qwerty_note(AppState* state, int active_index) {
    if (!state || active_index < 0 || active_index >= MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY) {
        return false;
    }
    MidiEditorQwertyActiveNote active = state->midi_editor_ui.qwerty_active_notes[active_index];
    state->midi_editor_ui.qwerty_active_notes[active_index].active = false;
    if (state->engine) {
        (void)engine_midi_audition_note_off(state->engine, active.note);
    }
    if (!active.active) {
        return false;
    }
    if (!active.record_on_release) {
        return true;
    }

    const EngineClip* clip = midi_editor_clip_from_indices(state, active.track_index, active.clip_index);
    if (!clip || clip->creation_index != active.clip_creation_index) {
        return false;
    }

    uint64_t end_frame = active.start_frame;
    if (!midi_editor_transport_relative_frame(state, clip, true, &end_frame)) {
        end_frame = active.start_frame;
    }
    end_frame = midi_editor_snap_relative_frame(state, clip, end_frame);
    uint64_t clip_frames = clip->duration_frames > 0 ? clip->duration_frames : 1u;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    if (end_frame <= active.start_frame) {
        end_frame = active.start_frame + min_duration;
    }
    if (end_frame > clip_frames) {
        end_frame = clip_frames;
    }
    uint64_t duration = end_frame > active.start_frame ? end_frame - active.start_frame : min_duration;
    if (active.start_frame + duration > clip_frames) {
        duration = clip_frames > active.start_frame ? clip_frames - active.start_frame : min_duration;
    }
    if (duration < min_duration) {
        duration = min_duration;
    }
    if (active.start_frame + duration > clip_frames) {
        return false;
    }

    MidiEditorSelection selection = {0};
    selection.track_index = active.track_index;
    selection.clip_index = active.clip_index;
    selection.clip = clip;
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(clip, &before_notes, &before_count)) {
        return false;
    }

    EngineMidiNote note = {active.start_frame, duration, active.note, midi_editor_clamp_velocity(active.velocity)};
    int new_index = -1;
    if (!engine_clip_midi_add_note(state->engine, active.track_index, active.clip_index, note, &new_index)) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    midi_editor_select_note(state, &selection, new_index);
    return midi_editor_push_note_undo(state, &selection, before_notes, before_count);
}

static void midi_editor_complete_all_qwerty_notes(AppState* state) {
    if (!state) {
        return;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (state->midi_editor_ui.qwerty_active_notes[i].active) {
            (void)midi_editor_complete_qwerty_note(state, i);
        }
    }
}

static bool midi_editor_begin_qwerty_note(AppState* state,
                                          const MidiEditorSelection* selection,
                                          SDL_Keycode key,
                                          uint8_t note,
                                          bool record_on_release) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    if (midi_editor_find_active_qwerty_note(state, key) >= 0) {
        return true;
    }
    uint64_t start_frame = 0;
    if (record_on_release &&
        !midi_editor_transport_relative_frame(state, selection->clip, false, &start_frame)) {
        return false;
    }
    if (record_on_release) {
        start_frame = midi_editor_snap_relative_frame(state, selection->clip, start_frame);
    }
    if (record_on_release && start_frame >= selection->clip->duration_frames) {
        return false;
    }

    int slot = midi_editor_find_free_qwerty_note_slot(state);
    if (slot < 0) {
        return false;
    }
    EngineInstrumentPresetId preset = engine_clip_midi_instrument_preset(selection->clip);
    EngineInstrumentParams params = engine_clip_midi_instrument_params(selection->clip);
    float velocity = midi_editor_current_default_velocity(state);
    (void)engine_midi_audition_note_on(state->engine, selection->track_index, preset, params, note, velocity);
    state->midi_editor_ui.qwerty_active_notes[slot] = (MidiEditorQwertyActiveNote){
        .active = true,
        .key = key,
        .note = note,
        .velocity = velocity,
        .record_on_release = record_on_release,
        .track_index = selection->track_index,
        .clip_index = selection->clip_index,
        .clip_creation_index = selection->clip->creation_index,
        .start_frame = start_frame
    };
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.selected_note_index = -1;
    return true;
}

static bool midi_editor_toggle_qwerty_record(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_record_armed) {
        midi_editor_complete_all_qwerty_notes(state);
        state->midi_editor_ui.qwerty_record_armed = false;
        midi_editor_clear_qwerty_active_notes(state);
    } else {
        state->midi_editor_ui.qwerty_record_armed = true;
        midi_editor_clear_qwerty_active_notes(state);
    }
    return true;
}

static bool midi_editor_toggle_qwerty_test(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_test_enabled) {
        state->midi_editor_ui.qwerty_test_enabled = false;
        midi_editor_clear_qwerty_active_notes(state);
    } else {
        state->midi_editor_ui.qwerty_test_enabled = true;
        midi_editor_clear_qwerty_active_notes(state);
    }
    state->midi_editor_ui.instrument_menu_open = false;
    return true;
}

static bool midi_editor_handle_instrument_button(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_EDITOR;
    state->midi_editor_ui.instrument_menu_open = !state->midi_editor_ui.instrument_menu_open;
    return true;
}

static bool midi_editor_open_instrument_panel(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_record_armed) {
        midi_editor_complete_all_qwerty_notes(state);
    }
    state->midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;
    state->midi_editor_ui.qwerty_record_armed = false;
    state->midi_editor_ui.qwerty_test_enabled = false;
    state->midi_editor_ui.instrument_menu_open = false;
    state->midi_editor_ui.instrument_param_drag_active = false;
    state->midi_editor_ui.instrument_param_drag_index = -1;
    midi_editor_clear_qwerty_active_notes(state);
    midi_editor_clear_shift_note_pending(state);
    midi_editor_clear_note_press_pending(state);
    midi_editor_clear_marquee_state(state);
    midi_editor_end_drag(state);
    return true;
}

static bool midi_editor_handle_instrument_menu_click(AppState* state,
                                                     const MidiEditorSelection* selection,
                                                     const MidiEditorLayout* layout,
                                                     int x,
                                                     int y) {
    if (!state || !selection || !selection->clip || !layout ||
        !state->midi_editor_ui.instrument_menu_open) {
        return false;
    }
    SDL_Point point = {x, y};
    for (int i = 0; i < layout->instrument_menu_item_count; ++i) {
        if (SDL_PointInRect(&point, &layout->instrument_menu_item_rects[i])) {
            EngineInstrumentPresetId preset = engine_instrument_preset_clamp((EngineInstrumentPresetId)i);
            (void)engine_clip_midi_set_instrument_preset(state->engine,
                                                         selection->track_index,
                                                         selection->clip_index,
                                                         preset);
            state->midi_editor_ui.instrument_menu_open = false;
            return true;
        }
    }
    if (!SDL_PointInRect(&point, &layout->instrument_button_rect)) {
        state->midi_editor_ui.instrument_menu_open = false;
        if (SDL_PointInRect(&point, &layout->test_button_rect)) {
            return false;
        }
        return midi_editor_point_in_panel(state, x, y);
    }
    return false;
}

static bool midi_editor_handle_scroll(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_MOUSEWHEEL) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y)) {
        return midi_editor_point_in_panel(state, state->mouse_x, state->mouse_y);
    }
    int delta = event->wheel.y != 0 ? event->wheel.y : event->wheel.x;
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    if (delta == 0) {
        return true;
    }
    SDL_Keymod mods = SDL_GetModState();
    if (mods & KMOD_ALT) {
        float zoom_factor = 1.0f - (float)delta * 0.1f;
        if (zoom_factor < 0.1f) {
            zoom_factor = 0.1f;
        }
        return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, zoom_factor);
    }
    if (mods & KMOD_SHIFT) {
        uint64_t frame = 0;
        uint64_t clip_frames = midi_editor_clip_frames(selection.clip);
        if (midi_editor_point_to_frame(&layout, clip_frames, state->mouse_x, &frame)) {
            uint64_t span = layout.view_span_frames > 0 ? layout.view_span_frames : clip_frames;
            uint64_t step = span / 16u;
            if (step < 1u) {
                step = 1u;
            }
            if (delta > 0) {
                frame = step > frame ? 0 : frame - step;
            } else {
                frame = frame + step > clip_frames ? clip_frames : frame + step;
            }
            uint64_t absolute_frame = selection.clip->timeline_start_frames + frame;
            bool was_playing = engine_transport_is_playing(state->engine);
            input_manager_reset_meter_history_on_seek(state);
            engine_transport_seek(state->engine, absolute_frame);
            if (was_playing) {
                engine_transport_play(state->engine);
            }
        }
        return true;
    }
    return midi_editor_pan_viewport(state, &selection, &layout, -delta);
}

static bool midi_editor_handle_viewport_keydown(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    if (!midi_editor_pointer_over_editor_grid_or_ruler(&layout, state->mouse_x, state->mouse_y)) {
        return false;
    }
    SDL_Keymod mods = SDL_GetModState();
    SDL_Keycode key = event->key.keysym.sym;
    if (mods & KMOD_ALT) {
        if (key == SDLK_LEFT) {
            return midi_editor_pan_viewport(state, &selection, &layout, -1);
        }
        if (key == SDLK_RIGHT) {
            return midi_editor_pan_viewport(state, &selection, &layout, 1);
        }
        if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
            return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, 0.8f);
        }
        if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
            return midi_editor_zoom_viewport(state, &selection, &layout, state->mouse_x, 1.25f);
        }
        if (key == SDLK_0 || key == SDLK_KP_0) {
            midi_editor_fit_viewport(state, &selection);
            return true;
        }
    }
    return false;
}

static bool midi_editor_adjust_octave(AppState* state, int delta) {
    if (!state || delta == 0) {
        return false;
    }
    state->midi_editor_ui.qwerty_octave_offset =
        midi_editor_clamp_octave_offset(state->midi_editor_ui.qwerty_octave_offset + delta);
    midi_editor_clear_qwerty_active_notes(state);
    return true;
}

static bool midi_editor_adjust_default_velocity(AppState* state, float delta) {
    if (!state || delta == 0.0f) {
        return false;
    }
    midi_editor_set_default_velocity(state, midi_editor_current_default_velocity(state) + delta);
    return true;
}

static bool midi_editor_note_values_match(EngineMidiNote a, EngineMidiNote b) {
    return a.start_frame == b.start_frame &&
           a.duration_frames == b.duration_frames &&
           a.note == b.note &&
           a.velocity == b.velocity;
}

static void midi_editor_select_matching_notes_after_set(AppState* state,
                                                        const MidiEditorSelection* selection,
                                                        const EngineMidiNote* selected_notes,
                                                        int selected_count) {
    if (!state || !selection || !selection->clip || !selected_notes || selected_count <= 0) {
        return;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    int note_count = engine_clip_midi_note_count(selection->clip);
    if (!notes || note_count <= 0) {
        midi_editor_select_note(state, selection, -1);
        return;
    }
    bool consumed[ENGINE_MIDI_NOTE_CAP] = {false};
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    midi_editor_clear_note_selection(state);
    int bounded_note_count = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    for (int i = 0; i < selected_count; ++i) {
        for (int j = 0; j < bounded_note_count; ++j) {
            if (!consumed[j] && midi_editor_note_values_match(notes[j], selected_notes[i])) {
                consumed[j] = true;
                state->midi_editor_ui.selected_note_indices[j] = true;
                if (state->midi_editor_ui.selected_note_index < 0) {
                    state->midi_editor_ui.selected_note_index = j;
                }
                break;
            }
        }
    }
}

static bool midi_editor_collect_selected_pattern(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 EngineMidiNote* out_notes,
                                                 int out_capacity,
                                                 int* out_count,
                                                 uint64_t* out_span_frames,
                                                 uint64_t* out_min_start,
                                                 uint64_t* out_max_end) {
    if (!state || !selection || !selection->clip || !out_notes || !out_count ||
        !out_span_frames || out_capacity <= 0) {
        return false;
    }
    *out_count = 0;
    *out_span_frames = 0;
    if (out_min_start) {
        *out_min_start = 0;
    }
    if (out_max_end) {
        *out_max_end = 0;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    uint64_t min_start = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    uint64_t max_end = 0;
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        uint64_t end = notes[i].start_frame + notes[i].duration_frames;
        if (notes[i].start_frame < min_start) {
            min_start = notes[i].start_frame;
        }
        if (end > max_end) {
            max_end = end;
        }
        ++selected_count;
    }
    if (selected_count <= 0 || selected_count > out_capacity || max_end <= min_start) {
        return false;
    }
    int out = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = notes[i];
        note.start_frame -= min_start;
        out_notes[out++] = note;
    }
    *out_count = out;
    *out_span_frames = max_end - min_start;
    if (out_min_start) {
        *out_min_start = min_start;
    }
    if (out_max_end) {
        *out_max_end = max_end;
    }
    return out > 0;
}

static bool midi_editor_copy_selected_notes(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int count = 0;
    uint64_t span = 0;
    if (!midi_editor_collect_selected_pattern(state,
                                              &selection,
                                              state->midi_editor_ui.clipboard_notes,
                                              ENGINE_MIDI_NOTE_CAP,
                                              &count,
                                              &span,
                                              NULL,
                                              NULL)) {
        state->midi_editor_ui.clipboard_note_count = 0;
        state->midi_editor_ui.clipboard_span_frames = 0;
        return false;
    }
    state->midi_editor_ui.clipboard_note_count = count;
    state->midi_editor_ui.clipboard_span_frames = span;
    state->midi_editor_ui.instrument_menu_open = false;
    return true;
}

static bool midi_editor_selected_insert_frame(AppState* state,
                                              const MidiEditorSelection* selection,
                                              uint64_t pattern_span,
                                              uint64_t* out_frame) {
    if (!state || !selection || !selection->clip || !out_frame || pattern_span == 0) {
        return false;
    }
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    if (pattern_span > clip_frames) {
        return false;
    }
    uint64_t relative = 0;
    if (midi_editor_transport_relative_frame(state, selection->clip, false, &relative) &&
        relative + pattern_span <= clip_frames) {
        *out_frame = relative;
        return true;
    }

    EngineMidiNote* scratch = (EngineMidiNote*)calloc((size_t)ENGINE_MIDI_NOTE_CAP, sizeof(EngineMidiNote));
    if (!scratch) {
        return false;
    }
    int count = 0;
    uint64_t span = 0;
    uint64_t max_end = 0;
    bool has_selection = midi_editor_collect_selected_pattern(state,
                                                              selection,
                                                              scratch,
                                                              ENGINE_MIDI_NOTE_CAP,
                                                              &count,
                                                              &span,
                                                              NULL,
                                                              &max_end);
    free(scratch);
    if (has_selection && max_end + pattern_span <= clip_frames) {
        *out_frame = max_end;
        return true;
    }
    *out_frame = 0;
    return pattern_span <= clip_frames;
}

static bool midi_editor_insert_pattern(AppState* state,
                                       const MidiEditorSelection* selection,
                                       const EngineMidiNote* pattern,
                                       int pattern_count,
                                       uint64_t pattern_span,
                                       uint64_t insert_frame) {
    if (!state || !selection || !selection->clip || !pattern || pattern_count <= 0 ||
        pattern_count > ENGINE_MIDI_NOTE_CAP || pattern_span == 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (note_count < 0 || note_count > ENGINE_MIDI_NOTE_CAP ||
        note_count + pattern_count > ENGINE_MIDI_NOTE_CAP ||
        (note_count > 0 && !notes)) {
        return false;
    }
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    if (pattern_span > clip_frames || insert_frame + pattern_span > clip_frames) {
        return false;
    }
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection->clip, &before_notes, &before_count)) {
        return false;
    }
    int replacement_count = note_count + pattern_count;
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)replacement_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)pattern_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    if (note_count > 0) {
        memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    }
    for (int i = 0; i < pattern_count; ++i) {
        EngineMidiNote note = pattern[i];
        note.start_frame += insert_frame;
        if (note.duration_frames == 0 || note.start_frame + note.duration_frames > clip_frames) {
            free(replacement);
            free(selected_after);
            midi_editor_free_notes(&before_notes, &before_count);
            return false;
        }
        replacement[note_count + i] = note;
        selected_after[i] = note;
    }

    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection->track_index,
                                         selection->clip_index,
                                         replacement,
                                         replacement_count);
    if (ok) {
        MidiEditorSelection fresh = *selection;
        (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
        midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, pattern_count);
        midi_editor_clear_hover_note(state);
        state->midi_editor_ui.instrument_menu_open = false;
        ok = midi_editor_push_note_undo(state, selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    return ok;
}

static bool midi_editor_paste_notes(AppState* state) {
    if (!state || state->midi_editor_ui.clipboard_note_count <= 0 ||
        state->midi_editor_ui.clipboard_note_count > ENGINE_MIDI_NOTE_CAP ||
        state->midi_editor_ui.clipboard_span_frames == 0) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    uint64_t insert_frame = 0;
    if (!midi_editor_selected_insert_frame(state,
                                           &selection,
                                           state->midi_editor_ui.clipboard_span_frames,
                                           &insert_frame)) {
        return false;
    }
    return midi_editor_insert_pattern(state,
                                      &selection,
                                      state->midi_editor_ui.clipboard_notes,
                                      state->midi_editor_ui.clipboard_note_count,
                                      state->midi_editor_ui.clipboard_span_frames,
                                      insert_frame);
}

static bool midi_editor_duplicate_selected_notes(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    EngineMidiNote* pattern = (EngineMidiNote*)calloc((size_t)ENGINE_MIDI_NOTE_CAP, sizeof(EngineMidiNote));
    if (!pattern) {
        return false;
    }
    int pattern_count = 0;
    uint64_t pattern_span = 0;
    uint64_t max_end = 0;
    bool collected = midi_editor_collect_selected_pattern(state,
                                                          &selection,
                                                          pattern,
                                                          ENGINE_MIDI_NOTE_CAP,
                                                          &pattern_count,
                                                          &pattern_span,
                                                          NULL,
                                                          &max_end);
    bool ok = false;
    if (collected) {
        ok = midi_editor_insert_pattern(state,
                                        &selection,
                                        pattern,
                                        pattern_count,
                                        pattern_span,
                                        max_end);
    }
    free(pattern);
    return ok;
}

static bool midi_editor_quantize_selected_note(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (selected_mask[i]) {
            ++selected_count;
        }
    }
    if (selected_count <= 0) {
        return false;
    }

    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }

    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)selected_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    int selected_after_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        EngineMidiNote note = replacement[i];
        uint64_t start = midi_editor_quantize_relative_frame(state, selection.clip, note.start_frame, true);
        uint64_t end = midi_editor_quantize_relative_frame(state,
                                                           selection.clip,
                                                           note.start_frame + note.duration_frames,
                                                           true);
        if (end <= start) {
            end = start + midi_editor_quantize_step_frames_at(state, selection.clip, start);
        }
        if (end > clip_frames) {
            end = clip_frames;
        }
        if (end <= start) {
            start = clip_frames > min_duration ? clip_frames - min_duration : 0;
            end = clip_frames;
        }
        note.start_frame = start;
        note.duration_frames = end > start ? end - start : min_duration;
        if (note.duration_frames < min_duration) {
            note.duration_frames = min_duration;
        }
        if (note.start_frame + note.duration_frames > clip_frames) {
            note.duration_frames = clip_frames > note.start_frame ? clip_frames - note.start_frame : min_duration;
        }
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }

    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection.track_index,
                                         selection.clip_index,
                                         replacement,
                                         note_count);
    if (ok) {
        MidiEditorSelection fresh = selection;
        (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
        midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, selected_after_count);
        ok = midi_editor_push_note_undo(state, &selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    free(selected_after);
    state->midi_editor_ui.instrument_menu_open = false;
    return ok;
}

static bool midi_editor_handle_qwerty_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)) {
        return false;
    }
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl_or_cmd = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
    bool alt_held = (mods & KMOD_ALT) != 0;
    if (ctrl_or_cmd || alt_held) {
        return false;
    }

    SDL_Keycode key = event->key.keysym.sym;
    if (event->type == SDL_KEYDOWN && key == SDLK_r && event->key.repeat == 0) {
        state->midi_editor_ui.instrument_menu_open = false;
        return midi_editor_toggle_qwerty_record(state);
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        if (key == SDLK_LEFTBRACKET) {
            return midi_editor_adjust_octave(state, -1);
        }
        if (key == SDLK_RIGHTBRACKET) {
            return midi_editor_adjust_octave(state, 1);
        }
        if (key == SDLK_MINUS) {
            return midi_editor_adjust_default_velocity(state, -0.05f);
        }
        if (key == SDLK_EQUALS) {
            return midi_editor_adjust_default_velocity(state, 0.05f);
        }
        if (key == SDLK_q) {
            return midi_editor_quantize_selected_note(state);
        }
    }

    uint8_t base_note = 0;
    if (!midi_editor_qwerty_note_for_key(key, &base_note)) {
        return false;
    }
    uint8_t note = midi_editor_apply_octave_offset((int)base_note, state->midi_editor_ui.qwerty_octave_offset);
    bool record_on_release = state->midi_editor_ui.qwerty_record_armed;
    bool test_only = state->midi_editor_ui.qwerty_test_enabled && !record_on_release;
    if (!record_on_release && !test_only) {
        return false;
    }
    if (event->type == SDL_KEYDOWN) {
        if (event->key.repeat != 0) {
            return true;
        }
        MidiEditorSelection selection = {0};
        if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
            return true;
        }
        (void)midi_editor_begin_qwerty_note(state, &selection, key, note, record_on_release);
        return true;
    }

    int active_index = midi_editor_find_active_qwerty_note(state, key);
    if (active_index >= 0) {
        (void)midi_editor_complete_qwerty_note(state, active_index);
    }
    return true;
}

static void midi_editor_select_note(AppState* state,
                                    const MidiEditorSelection* selection,
                                    int note_index) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.selected_note_index = note_index;
    memset(state->midi_editor_ui.selected_note_indices,
           0,
           sizeof(state->midi_editor_ui.selected_note_indices));
    if (note_index >= 0 && note_index < ENGINE_MIDI_NOTE_CAP) {
        state->midi_editor_ui.selected_note_indices[note_index] = true;
    }
    state->midi_editor_ui.hover_note_valid = note_index >= 0;
    state->midi_editor_ui.hover_track_index = selection->track_index;
    state->midi_editor_ui.hover_clip_index = selection->clip_index;
    state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.hover_note_index = note_index;
}

static void midi_editor_toggle_note_selection(AppState* state,
                                              const MidiEditorSelection* selection,
                                              int note_index) {
    if (!state || !selection || !selection->clip ||
        note_index < 0 || note_index >= ENGINE_MIDI_NOTE_CAP) {
        return;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    if (note_index >= note_count) {
        return;
    }
    if (!midi_editor_selection_matches_ui(state, selection)) {
        state->midi_editor_ui.selected_track_index = selection->track_index;
        state->midi_editor_ui.selected_clip_index = selection->clip_index;
        state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
        midi_editor_clear_note_selection(state);
    } else if (midi_editor_effective_selected_note_count(state, selection, note_count) == 1 &&
               state->midi_editor_ui.selected_note_index >= 0 &&
               state->midi_editor_ui.selected_note_index < ENGINE_MIDI_NOTE_CAP &&
               !state->midi_editor_ui.selected_note_indices[state->midi_editor_ui.selected_note_index]) {
        state->midi_editor_ui.selected_note_indices[state->midi_editor_ui.selected_note_index] = true;
    }

    state->midi_editor_ui.selected_note_indices[note_index] =
        !state->midi_editor_ui.selected_note_indices[note_index];
    if (state->midi_editor_ui.selected_note_indices[note_index]) {
        state->midi_editor_ui.selected_note_index = note_index;
    } else if (state->midi_editor_ui.selected_note_index == note_index) {
        midi_editor_focus_first_selected_note(state, note_count);
    }
    if (midi_editor_effective_selected_note_count(state, selection, note_count) == 0) {
        state->midi_editor_ui.selected_note_index = -1;
    }
    state->midi_editor_ui.hover_note_valid = note_index >= 0;
    state->midi_editor_ui.hover_track_index = selection->track_index;
    state->midi_editor_ui.hover_clip_index = selection->clip_index;
    state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.hover_note_index = note_index;
}

static bool midi_editor_note_is_selected(const AppState* state,
                                         const MidiEditorSelection* selection,
                                         int note_index) {
    return midi_editor_note_index_selected(state, selection, note_index);
}

static void midi_editor_clear_hover_note(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.hover_note_valid = false;
    state->midi_editor_ui.hover_note_index = -1;
}

static void midi_editor_update_hover_note(AppState* state,
                                          const MidiEditorSelection* selection,
                                          const MidiEditorLayout* layout,
                                          int x,
                                          int y) {
    if (!state || !selection || !layout) {
        return;
    }
    MidiEditorNoteHit hit = {0};
    if (midi_editor_hit_test_note(layout, selection->clip, x, y, &hit)) {
        state->midi_editor_ui.hover_note_valid = true;
        state->midi_editor_ui.hover_track_index = selection->track_index;
        state->midi_editor_ui.hover_clip_index = selection->clip_index;
        state->midi_editor_ui.hover_clip_creation_index = selection->clip->creation_index;
        state->midi_editor_ui.hover_note_index = hit.note_index;
        return;
    }
    midi_editor_clear_hover_note(state);
}

static SDL_Rect midi_editor_rect_from_points(int x0, int y0, int x1, int y1) {
    SDL_Rect rect = {x0, y0, x1 - x0, y1 - y0};
    if (rect.w < 0) {
        rect.x += rect.w;
        rect.w = -rect.w;
    }
    if (rect.h < 0) {
        rect.y += rect.h;
        rect.h = -rect.h;
    }
    return rect;
}

static void midi_editor_update_marquee_preview(AppState* state,
                                               const MidiEditorSelection* selection,
                                               const MidiEditorLayout* layout,
                                               int x,
                                               int y) {
    if (!state || !selection || !selection->clip || !layout ||
        !state->midi_editor_ui.marquee_active) {
        return;
    }
    state->midi_editor_ui.marquee_current_x = x;
    state->midi_editor_ui.marquee_current_y = y;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
    SDL_Rect marquee = midi_editor_rect_from_points(state->midi_editor_ui.marquee_start_x,
                                                    state->midi_editor_ui.marquee_start_y,
                                                    x,
                                                    y);
    if (marquee.w < 2 && marquee.h < 2) {
        return;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    int note_count = engine_clip_midi_note_count(selection->clip);
    uint64_t clip_frames = selection->clip->duration_frames > 0 ? selection->clip->duration_frames : 1u;
    for (int i = 0; notes && i < note_count && i < ENGINE_MIDI_NOTE_CAP; ++i) {
        SDL_Rect note_rect = {0, 0, 0, 0};
        if (midi_editor_note_rect(layout, &notes[i], clip_frames, &note_rect) &&
            SDL_HasIntersection(&marquee, &note_rect)) {
            state->midi_editor_ui.marquee_preview_note_indices[i] = true;
        }
    }
}

static bool midi_editor_begin_marquee(AppState* state,
                                      const MidiEditorSelection* selection,
                                      int x,
                                      int y,
                                      bool additive) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.marquee_active = true;
    state->midi_editor_ui.marquee_additive = additive;
    state->midi_editor_ui.marquee_start_x = x;
    state->midi_editor_ui.marquee_start_y = y;
    state->midi_editor_ui.marquee_current_x = x;
    state->midi_editor_ui.marquee_current_y = y;
    memset(state->midi_editor_ui.marquee_preview_note_indices,
           0,
           sizeof(state->midi_editor_ui.marquee_preview_note_indices));
    return true;
}

static bool midi_editor_commit_marquee(AppState* state) {
    if (!state || !state->midi_editor_ui.marquee_active) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout) ||
        !midi_editor_selection_matches_ui(state, &selection)) {
        midi_editor_clear_marquee_state(state);
        return false;
    }
    midi_editor_update_marquee_preview(state,
                                       &selection,
                                       &layout,
                                       state->midi_editor_ui.marquee_current_x,
                                       state->midi_editor_ui.marquee_current_y);
    int note_count = engine_clip_midi_note_count(selection.clip);
    if (note_count > ENGINE_MIDI_NOTE_CAP) {
        note_count = ENGINE_MIDI_NOTE_CAP;
    }
    if (!state->midi_editor_ui.marquee_additive) {
        midi_editor_clear_note_selection(state);
    }
    for (int i = 0; i < note_count; ++i) {
        if (state->midi_editor_ui.marquee_preview_note_indices[i]) {
            state->midi_editor_ui.selected_note_indices[i] = true;
        }
    }
    midi_editor_focus_first_selected_note(state, note_count);
    midi_editor_clear_marquee_state(state);
    return true;
}

static bool midi_editor_delete_selected_note(AppState* state) {
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, &selection, note_count, selected_mask);
    int selected_count = 0;
    for (int i = 0; i < note_count; ++i) {
        if (selected_mask[i]) {
            ++selected_count;
        }
    }
    if (selected_count <= 0) {
        return false;
    }
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(selection.clip, &before_notes, &before_count)) {
        return false;
    }
    int replacement_count = note_count - selected_count;
    EngineMidiNote* replacement = replacement_count > 0
        ? (EngineMidiNote*)calloc((size_t)replacement_count, sizeof(EngineMidiNote))
        : NULL;
    if (replacement_count > 0 && !replacement) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    int out = 0;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            replacement[out++] = notes[i];
        }
    }
    bool ok = engine_clip_midi_set_notes(state->engine,
                                         selection.track_index,
                                         selection.clip_index,
                                         replacement,
                                         replacement_count);
    if (ok) {
        midi_editor_clear_note_selection(state);
        midi_editor_clear_hover_note(state);
        ok = midi_editor_push_note_undo(state, &selection, before_notes, before_count);
    } else {
        midi_editor_free_notes(&before_notes, &before_count);
    }
    free(replacement);
    return ok;
}

static bool midi_editor_command_modifier_active(const SDL_Event* event) {
    SDL_Keymod mods = SDL_GetModState();
    if (event) {
        mods = (SDL_Keymod)(mods | event->key.keysym.mod);
    }
    return (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
}

static bool midi_editor_handle_clipboard_keydown(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN || !midi_editor_command_modifier_active(event)) {
        return false;
    }
    switch (event->key.keysym.sym) {
    case SDLK_c:
        (void)midi_editor_copy_selected_notes(state);
        return true;
    case SDLK_v:
        (void)midi_editor_paste_notes(state);
        return true;
    case SDLK_d:
        (void)midi_editor_duplicate_selected_notes(state);
        return true;
    default:
        break;
    }
    return false;
}

static bool midi_editor_set_drag_state(AppState* state,
                                       const MidiEditorSelection* selection,
                                       MidiEditorDragMode mode,
                                       int note_index,
                                       int x,
                                       int y,
                                       EngineMidiNote note) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.drag_active = true;
    state->midi_editor_ui.drag_mode = mode;
    state->midi_editor_ui.drag_track_index = selection->track_index;
    state->midi_editor_ui.drag_clip_index = selection->clip_index;
    state->midi_editor_ui.drag_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.drag_note_index = note_index;
    state->midi_editor_ui.drag_original_note = note;
    state->midi_editor_ui.drag_start_x = x;
    state->midi_editor_ui.drag_start_y = y;
    state->midi_editor_ui.drag_anchor_frame = note.start_frame;
    state->midi_editor_ui.drag_anchor_note = note.note;
    state->midi_editor_ui.drag_anchor_velocity = note.velocity;
    state->midi_editor_ui.drag_velocity_group = false;
    midi_editor_clear_move_group_drag(state);
    memset(state->midi_editor_ui.drag_velocity_original_values,
           0,
           sizeof(state->midi_editor_ui.drag_velocity_original_values));
    if (mode == MIDI_EDITOR_DRAG_VELOCITY) {
        int note_count = engine_clip_midi_note_count(selection->clip);
        const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
        if (notes && note_count > 0 && note_count <= ENGINE_MIDI_NOTE_CAP &&
            midi_editor_note_index_selected(state, selection, note_index)) {
            bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
            midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
            for (int i = 0; i < note_count; ++i) {
                state->midi_editor_ui.drag_velocity_original_values[i] = notes[i].velocity;
                if (selected_mask[i]) {
                    state->midi_editor_ui.drag_velocity_group = true;
                }
            }
        } else if (notes && note_index >= 0 && note_index < note_count && note_index < ENGINE_MIDI_NOTE_CAP) {
            state->midi_editor_ui.drag_velocity_original_values[note_index] = notes[note_index].velocity;
        }
    } else if (mode == MIDI_EDITOR_DRAG_MOVE) {
        (void)midi_editor_configure_move_group_drag(state, selection, note_index);
    }
    state->midi_editor_ui.drag_mutated = (mode == MIDI_EDITOR_DRAG_CREATE);
    return true;
}

static bool midi_editor_begin_drag(AppState* state,
                                   const MidiEditorSelection* selection,
                                   MidiEditorDragMode mode,
                                   int note_index,
                                   int x,
                                   int y,
                                   EngineMidiNote note) {
    if (!midi_editor_begin_note_undo(state, selection)) {
        return false;
    }
    return midi_editor_set_drag_state(state, selection, mode, note_index, x, y, note);
}

static bool midi_editor_note_equal(EngineMidiNote a, EngineMidiNote b) {
    return a.start_frame == b.start_frame &&
           a.duration_frames == b.duration_frames &&
           a.note == b.note &&
           a.velocity == b.velocity;
}

static void midi_editor_clear_move_group_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.drag_move_group = false;
    state->midi_editor_ui.drag_move_original_note_count = 0;
    memset(state->midi_editor_ui.drag_move_selected_indices,
           0,
           sizeof(state->midi_editor_ui.drag_move_selected_indices));
    memset(state->midi_editor_ui.drag_move_original_notes,
           0,
           sizeof(state->midi_editor_ui.drag_move_original_notes));
}

static bool midi_editor_configure_move_group_drag(AppState* state,
                                                  const MidiEditorSelection* selection,
                                                  int note_index) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP ||
        !midi_editor_note_index_selected(state, selection, note_index) ||
        midi_editor_effective_selected_note_count(state, selection, note_count) <= 1) {
        return false;
    }
    midi_editor_clear_move_group_drag(state);
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    memcpy(state->midi_editor_ui.drag_move_original_notes,
           notes,
           sizeof(EngineMidiNote) * (size_t)note_count);
    state->midi_editor_ui.drag_move_original_note_count = note_count;
    bool any_selected = false;
    for (int i = 0; i < note_count; ++i) {
        state->midi_editor_ui.drag_move_selected_indices[i] = selected_mask[i];
        if (selected_mask[i]) {
            any_selected = true;
        }
    }
    state->midi_editor_ui.drag_move_group = any_selected;
    return any_selected;
}

static bool midi_editor_update_velocity_group_drag(AppState* state,
                                                   const MidiEditorSelection* selection,
                                                   int mouse_y) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    if (!notes || note_count <= 0 || note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    float delta = (float)(state->midi_editor_ui.drag_start_y - mouse_y) / MIDI_EDITOR_VELOCITY_DRAG_PIXELS;
    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)note_count, sizeof(EngineMidiNote));
    if (!replacement) {
        return false;
    }
    memcpy(replacement, notes, sizeof(EngineMidiNote) * (size_t)note_count);
    bool changed = false;
    float anchor_velocity = state->midi_editor_ui.drag_anchor_velocity;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        float original = state->midi_editor_ui.drag_velocity_original_values[i];
        float velocity = midi_editor_clamp_velocity(original + delta);
        if (replacement[i].velocity != velocity) {
            changed = true;
        }
        replacement[i].velocity = velocity;
        if (i == state->midi_editor_ui.drag_note_index) {
            anchor_velocity = velocity;
        }
    }
    bool ok = true;
    if (changed) {
        ok = engine_clip_midi_set_notes(state->engine,
                                        selection->track_index,
                                        selection->clip_index,
                                        replacement,
                                        note_count);
        if (ok) {
            state->midi_editor_ui.drag_mutated = true;
        }
    }
    midi_editor_set_default_velocity(state, anchor_velocity);
    free(replacement);
    return ok;
}

static bool midi_editor_update_move_group_drag(AppState* state,
                                               const MidiEditorSelection* selection,
                                               uint64_t pointer_frame,
                                               uint8_t pointer_note,
                                               uint64_t clip_frames) {
    if (!state || !selection || !selection->clip ||
        !state->midi_editor_ui.drag_move_group ||
        state->midi_editor_ui.drag_move_original_note_count <= 0 ||
        state->midi_editor_ui.drag_move_original_note_count > ENGINE_MIDI_NOTE_CAP ||
        clip_frames == 0) {
        return false;
    }
    int note_count = engine_clip_midi_note_count(selection->clip);
    const EngineMidiNote* current_notes = engine_clip_midi_notes(selection->clip);
    int original_count = state->midi_editor_ui.drag_move_original_note_count;
    if (!current_notes || note_count != original_count) {
        return false;
    }

    const EngineMidiNote* original_notes = state->midi_editor_ui.drag_move_original_notes;
    int64_t raw_delta = 0;
    if (pointer_frame >= state->midi_editor_ui.drag_anchor_frame) {
        raw_delta = (int64_t)(pointer_frame - state->midi_editor_ui.drag_anchor_frame);
    } else {
        raw_delta = -(int64_t)(state->midi_editor_ui.drag_anchor_frame - pointer_frame);
    }
    uint64_t anchor_start = state->midi_editor_ui.drag_original_note.start_frame;
    uint64_t raw_anchor_start = anchor_start;
    if (raw_delta >= 0) {
        uint64_t delta = (uint64_t)raw_delta;
        raw_anchor_start = anchor_start + delta;
        if (raw_anchor_start > clip_frames) {
            raw_anchor_start = clip_frames;
        }
    } else {
        uint64_t delta = (uint64_t)(-raw_delta);
        raw_anchor_start = delta > anchor_start ? 0 : anchor_start - delta;
    }
    uint64_t snapped_anchor_start = midi_editor_snap_relative_frame(state, selection->clip, raw_anchor_start);
    int64_t frame_delta = 0;
    if (snapped_anchor_start >= anchor_start) {
        frame_delta = (int64_t)(snapped_anchor_start - anchor_start);
    } else {
        frame_delta = -(int64_t)(anchor_start - snapped_anchor_start);
    }

    uint64_t min_start = clip_frames;
    uint64_t max_end = 0;
    int min_note = ENGINE_MIDI_NOTE_MAX;
    int max_note = ENGINE_MIDI_NOTE_MIN;
    for (int i = 0; i < original_count; ++i) {
        if (!state->midi_editor_ui.drag_move_selected_indices[i]) {
            continue;
        }
        if (original_notes[i].start_frame < min_start) {
            min_start = original_notes[i].start_frame;
        }
        uint64_t end = original_notes[i].start_frame + original_notes[i].duration_frames;
        if (end > max_end) {
            max_end = end;
        }
        if ((int)original_notes[i].note < min_note) {
            min_note = (int)original_notes[i].note;
        }
        if ((int)original_notes[i].note > max_note) {
            max_note = (int)original_notes[i].note;
        }
    }
    if (min_start == clip_frames && max_end == 0) {
        return false;
    }
    if (frame_delta < 0 && (uint64_t)(-frame_delta) > min_start) {
        frame_delta = -(int64_t)min_start;
    }
    if (frame_delta > 0 && max_end + (uint64_t)frame_delta > clip_frames) {
        frame_delta = clip_frames > max_end ? (int64_t)(clip_frames - max_end) : 0;
    }

    int pitch_delta = (int)pointer_note - (int)state->midi_editor_ui.drag_anchor_note;
    if (min_note + pitch_delta < ENGINE_MIDI_NOTE_MIN) {
        pitch_delta = ENGINE_MIDI_NOTE_MIN - min_note;
    }
    if (max_note + pitch_delta > ENGINE_MIDI_NOTE_MAX) {
        pitch_delta = ENGINE_MIDI_NOTE_MAX - max_note;
    }

    EngineMidiNote* replacement = (EngineMidiNote*)calloc((size_t)original_count, sizeof(EngineMidiNote));
    EngineMidiNote* selected_after = (EngineMidiNote*)calloc((size_t)original_count, sizeof(EngineMidiNote));
    if (!replacement || !selected_after) {
        free(replacement);
        free(selected_after);
        return false;
    }
    memcpy(replacement, original_notes, sizeof(EngineMidiNote) * (size_t)original_count);
    int selected_after_count = 0;
    bool changed = false;
    for (int i = 0; i < original_count; ++i) {
        if (!state->midi_editor_ui.drag_move_selected_indices[i]) {
            continue;
        }
        EngineMidiNote note = original_notes[i];
        if (frame_delta >= 0) {
            note.start_frame += (uint64_t)frame_delta;
        } else {
            note.start_frame -= (uint64_t)(-frame_delta);
        }
        int moved_note = (int)note.note + pitch_delta;
        if (moved_note < ENGINE_MIDI_NOTE_MIN) moved_note = ENGINE_MIDI_NOTE_MIN;
        if (moved_note > ENGINE_MIDI_NOTE_MAX) moved_note = ENGINE_MIDI_NOTE_MAX;
        note.note = (uint8_t)moved_note;
        if (!midi_editor_note_equal(note, original_notes[i])) {
            changed = true;
        }
        replacement[i] = note;
        selected_after[selected_after_count++] = note;
    }

    bool ok = true;
    if (changed) {
        ok = engine_clip_midi_set_notes(state->engine,
                                        selection->track_index,
                                        selection->clip_index,
                                        replacement,
                                        original_count);
        if (ok) {
            MidiEditorSelection fresh = *selection;
            (void)midi_editor_get_fresh_selection(state, &fresh, NULL);
            midi_editor_select_matching_notes_after_set(state, &fresh, selected_after, selected_after_count);
            state->midi_editor_ui.drag_mutated = true;
        }
    }
    free(replacement);
    free(selected_after);
    return ok;
}

static bool midi_editor_update_drag(AppState* state, int mouse_x, int mouse_y) {
    if (!state || !state->midi_editor_ui.drag_active) {
        return false;
    }
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout) ||
        selection.track_index != state->midi_editor_ui.drag_track_index ||
        selection.clip_index != state->midi_editor_ui.drag_clip_index ||
        selection.clip->creation_index != state->midi_editor_ui.drag_clip_creation_index) {
        return false;
    }

    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    uint8_t pointer_note = state->midi_editor_ui.drag_anchor_note;
    uint64_t pointer_frame = state->midi_editor_ui.drag_anchor_frame;
    if (!midi_editor_point_to_note_frame(&layout, clip_frames, mouse_x, mouse_y, &pointer_note, &pointer_frame)) {
        int clamped_x = mouse_x;
        if (clamped_x < layout.grid_rect.x) {
            clamped_x = layout.grid_rect.x;
        }
        if (clamped_x > layout.grid_rect.x + layout.grid_rect.w) {
            clamped_x = layout.grid_rect.x + layout.grid_rect.w;
        }
        int clamped_y = mouse_y;
        if (clamped_y < layout.grid_rect.y) {
            clamped_y = layout.grid_rect.y;
        }
        if (clamped_y >= layout.grid_rect.y + layout.grid_rect.h) {
            clamped_y = layout.grid_rect.y + layout.grid_rect.h - 1;
        }
        if (!midi_editor_point_to_note_frame(&layout, clip_frames, clamped_x, clamped_y, &pointer_note, &pointer_frame)) {
            return false;
        }
    }
    pointer_frame = midi_editor_snap_relative_frame(state, selection.clip, pointer_frame);

    EngineMidiNote note = state->midi_editor_ui.drag_original_note;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    switch (state->midi_editor_ui.drag_mode) {
    case MIDI_EDITOR_DRAG_VELOCITY: {
        if (state->midi_editor_ui.drag_velocity_group) {
            return midi_editor_update_velocity_group_drag(state, &selection, mouse_y);
        }
        float delta = (float)(state->midi_editor_ui.drag_start_y - mouse_y) / MIDI_EDITOR_VELOCITY_DRAG_PIXELS;
        note.velocity = midi_editor_clamp_velocity(state->midi_editor_ui.drag_anchor_velocity + delta);
        midi_editor_set_default_velocity(state, note.velocity);
        break;
    }
    case MIDI_EDITOR_DRAG_CREATE:
    case MIDI_EDITOR_DRAG_RESIZE_RIGHT: {
        uint64_t end_frame = pointer_frame;
        if (end_frame < note.start_frame + min_duration) {
            end_frame = note.start_frame + min_duration;
        }
        if (end_frame > clip_frames) {
            end_frame = clip_frames;
        }
        note.duration_frames = end_frame > note.start_frame ? end_frame - note.start_frame : min_duration;
        break;
    }
    case MIDI_EDITOR_DRAG_RESIZE_LEFT: {
        uint64_t end_frame = note.start_frame + note.duration_frames;
        uint64_t new_start = pointer_frame;
        if (new_start + min_duration > end_frame) {
            new_start = end_frame > min_duration ? end_frame - min_duration : 0;
        }
        note.start_frame = new_start;
        note.duration_frames = end_frame > note.start_frame ? end_frame - note.start_frame : min_duration;
        break;
    }
    case MIDI_EDITOR_DRAG_MOVE: {
        if (state->midi_editor_ui.drag_move_group) {
            return midi_editor_update_move_group_drag(state,
                                                      &selection,
                                                      pointer_frame,
                                                      pointer_note,
                                                      clip_frames);
        }
        uint64_t start_pointer = state->midi_editor_ui.drag_anchor_frame;
        uint64_t raw_start = note.start_frame;
        if (pointer_frame >= start_pointer) {
            uint64_t delta = pointer_frame - start_pointer;
            raw_start = note.start_frame + delta;
        } else {
            uint64_t delta = start_pointer - pointer_frame;
            raw_start = delta > note.start_frame ? 0 : note.start_frame - delta;
        }
        if (raw_start + note.duration_frames > clip_frames) {
            raw_start = clip_frames > note.duration_frames ? clip_frames - note.duration_frames : 0;
        }
        note.start_frame = midi_editor_snap_relative_frame(state, selection.clip, raw_start);
        if (note.start_frame + note.duration_frames > clip_frames) {
            note.start_frame = clip_frames > note.duration_frames ? clip_frames - note.duration_frames : 0;
        }
        int pitch_delta = (int)pointer_note - (int)state->midi_editor_ui.drag_anchor_note;
        int moved_note = (int)note.note + pitch_delta;
        if (moved_note < ENGINE_MIDI_NOTE_MIN) moved_note = ENGINE_MIDI_NOTE_MIN;
        if (moved_note > ENGINE_MIDI_NOTE_MAX) moved_note = ENGINE_MIDI_NOTE_MAX;
        note.note = (uint8_t)moved_note;
        break;
    }
    case MIDI_EDITOR_DRAG_NONE:
    default:
        return false;
    }

    if (note.start_frame + note.duration_frames > clip_frames) {
        note.duration_frames = clip_frames > note.start_frame ? clip_frames - note.start_frame : min_duration;
    }
    if (note.duration_frames < min_duration) {
        note.duration_frames = min_duration;
    }
    if (midi_editor_note_equal(note, state->midi_editor_ui.drag_original_note) &&
        !state->midi_editor_ui.drag_mutated) {
        return true;
    }
    int new_index = -1;
    if (!engine_clip_midi_update_note(state->engine,
                                      selection.track_index,
                                      selection.clip_index,
                                      state->midi_editor_ui.drag_note_index,
                                      note,
                                      &new_index)) {
        return false;
    }
    if (new_index >= 0) {
        state->midi_editor_ui.drag_note_index = new_index;
        midi_editor_select_note(state, &selection, new_index);
    }
    state->midi_editor_ui.drag_mutated = true;
    return true;
}

static void midi_editor_end_drag(AppState* state) {
    if (!state || !state->midi_editor_ui.drag_active) {
        return;
    }
    if (state->midi_editor_ui.drag_mutated) {
        midi_editor_commit_note_undo(state);
    } else if (state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_MIDI_NOTE_EDIT) {
        undo_manager_cancel_drag(&state->undo);
    }
    state->midi_editor_ui.drag_active = false;
    state->midi_editor_ui.drag_mode = MIDI_EDITOR_DRAG_NONE;
    state->midi_editor_ui.drag_note_index = -1;
    state->midi_editor_ui.drag_mutated = false;
    midi_editor_clear_move_group_drag(state);
}

static void midi_editor_clear_shift_note_pending(AppState* state) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.shift_note_pending = false;
    state->midi_editor_ui.shift_note_pending_track_index = -1;
    state->midi_editor_ui.shift_note_pending_clip_index = -1;
    state->midi_editor_ui.shift_note_pending_clip_creation_index = 0;
    state->midi_editor_ui.shift_note_pending_note_index = -1;
    state->midi_editor_ui.shift_note_pending_x = 0;
    state->midi_editor_ui.shift_note_pending_y = 0;
}

static bool midi_editor_begin_shift_note_pending(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 int note_index,
                                                 int x,
                                                 int y) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.shift_note_pending = true;
    state->midi_editor_ui.shift_note_pending_track_index = selection->track_index;
    state->midi_editor_ui.shift_note_pending_clip_index = selection->clip_index;
    state->midi_editor_ui.shift_note_pending_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.shift_note_pending_note_index = note_index;
    state->midi_editor_ui.shift_note_pending_x = x;
    state->midi_editor_ui.shift_note_pending_y = y;
    return true;
}

static bool midi_editor_shift_note_pending_matches(const AppState* state,
                                                   const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.shift_note_pending &&
           state->midi_editor_ui.shift_note_pending_track_index == selection->track_index &&
           state->midi_editor_ui.shift_note_pending_clip_index == selection->clip_index &&
           state->midi_editor_ui.shift_note_pending_clip_creation_index == selection->clip->creation_index;
}

static bool midi_editor_update_shift_note_pending(AppState* state, int x, int y) {
    if (!state || !state->midi_editor_ui.shift_note_pending) {
        return false;
    }
    int dx = abs(x - state->midi_editor_ui.shift_note_pending_x);
    int dy = abs(y - state->midi_editor_ui.shift_note_pending_y);
    if (dx < 3 && dy < 3) {
        return true;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL) ||
        !midi_editor_shift_note_pending_matches(state, &selection)) {
        midi_editor_clear_shift_note_pending(state);
        return false;
    }
    int note_index = state->midi_editor_ui.shift_note_pending_note_index;
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_index < 0 || note_index >= note_count) {
        midi_editor_clear_shift_note_pending(state);
        return false;
    }
    if (!midi_editor_note_index_selected(state, &selection, note_index)) {
        midi_editor_select_note(state, &selection, note_index);
    }
    int start_x = state->midi_editor_ui.shift_note_pending_x;
    int start_y = state->midi_editor_ui.shift_note_pending_y;
    midi_editor_clear_shift_note_pending(state);
    return midi_editor_begin_drag(state,
                                  &selection,
                                  MIDI_EDITOR_DRAG_VELOCITY,
                                  note_index,
                                  start_x,
                                  start_y,
                                  notes[note_index]) &&
           midi_editor_update_drag(state, x, y);
}

static bool midi_editor_commit_shift_note_pending(AppState* state) {
    if (!state || !state->midi_editor_ui.shift_note_pending) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (midi_editor_get_fresh_selection(state, &selection, NULL) &&
        midi_editor_shift_note_pending_matches(state, &selection)) {
        midi_editor_toggle_note_selection(state,
                                          &selection,
                                          state->midi_editor_ui.shift_note_pending_note_index);
    }
    midi_editor_clear_shift_note_pending(state);
    return true;
}

static bool midi_editor_begin_note_press_pending(AppState* state,
                                                 const MidiEditorSelection* selection,
                                                 int note_index,
                                                 MidiEditorNoteHitPart part,
                                                 int x,
                                                 int y,
                                                 bool group_candidate) {
    if (!state || !selection || !selection->clip || note_index < 0) {
        return false;
    }
    state->midi_editor_ui.note_press_pending = true;
    state->midi_editor_ui.note_press_pending_track_index = selection->track_index;
    state->midi_editor_ui.note_press_pending_clip_index = selection->clip_index;
    state->midi_editor_ui.note_press_pending_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.note_press_pending_note_index = note_index;
    state->midi_editor_ui.note_press_pending_x = x;
    state->midi_editor_ui.note_press_pending_y = y;
    state->midi_editor_ui.note_press_pending_part = (int)part;
    state->midi_editor_ui.note_press_group_candidate = group_candidate;
    return true;
}

static bool midi_editor_note_press_pending_matches(const AppState* state,
                                                   const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.note_press_pending &&
           state->midi_editor_ui.note_press_pending_track_index == selection->track_index &&
           state->midi_editor_ui.note_press_pending_clip_index == selection->clip_index &&
           state->midi_editor_ui.note_press_pending_clip_creation_index == selection->clip->creation_index;
}

static MidiEditorDragMode midi_editor_drag_mode_for_hit_part(MidiEditorNoteHitPart part) {
    if (part == MIDI_EDITOR_NOTE_HIT_LEFT_EDGE) {
        return MIDI_EDITOR_DRAG_RESIZE_LEFT;
    }
    if (part == MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE) {
        return MIDI_EDITOR_DRAG_RESIZE_RIGHT;
    }
    return MIDI_EDITOR_DRAG_MOVE;
}

static bool midi_editor_update_note_press_pending(AppState* state, int x, int y) {
    if (!state || !state->midi_editor_ui.note_press_pending) {
        return false;
    }
    int dx = abs(x - state->midi_editor_ui.note_press_pending_x);
    int dy = abs(y - state->midi_editor_ui.note_press_pending_y);
    if (dx < 3 && dy < 3) {
        return true;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, NULL) ||
        !midi_editor_note_press_pending_matches(state, &selection)) {
        midi_editor_clear_note_press_pending(state);
        return false;
    }
    int note_index = state->midi_editor_ui.note_press_pending_note_index;
    int note_count = engine_clip_midi_note_count(selection.clip);
    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    if (!notes || note_index < 0 || note_index >= note_count) {
        midi_editor_clear_note_press_pending(state);
        return false;
    }
    MidiEditorNoteHitPart part = (MidiEditorNoteHitPart)state->midi_editor_ui.note_press_pending_part;
    bool group_move = state->midi_editor_ui.note_press_group_candidate &&
                      part == MIDI_EDITOR_NOTE_HIT_BODY &&
                      midi_editor_note_index_selected(state, &selection, note_index) &&
                      midi_editor_effective_selected_note_count(state, &selection, note_count) > 1;
    MidiEditorDragMode mode = group_move
        ? MIDI_EDITOR_DRAG_MOVE
        : midi_editor_drag_mode_for_hit_part(part);
    int start_x = state->midi_editor_ui.note_press_pending_x;
    int start_y = state->midi_editor_ui.note_press_pending_y;
    midi_editor_clear_note_press_pending(state);
    if (!group_move) {
        midi_editor_select_note(state, &selection, note_index);
    }
    return midi_editor_begin_drag(state, &selection, mode, note_index, start_x, start_y, notes[note_index]) &&
           midi_editor_update_drag(state, x, y);
}

static bool midi_editor_commit_note_press_pending(AppState* state) {
    if (!state || !state->midi_editor_ui.note_press_pending) {
        return false;
    }
    MidiEditorSelection selection = {0};
    if (midi_editor_get_fresh_selection(state, &selection, NULL) &&
        midi_editor_note_press_pending_matches(state, &selection)) {
        midi_editor_select_note(state,
                                &selection,
                                state->midi_editor_ui.note_press_pending_note_index);
    }
    midi_editor_clear_note_press_pending(state);
    return true;
}

static bool midi_editor_handle_left_down(AppState* state, int x, int y) {
    MidiEditorSelection selection = {0};
    MidiEditorLayout layout = {0};
    if (!midi_editor_get_fresh_selection(state, &selection, &layout)) {
        return false;
    }
    SDL_Point point = {x, y};
    if (midi_editor_handle_instrument_menu_click(state, &selection, &layout, x, y)) {
        return true;
    }
    if (SDL_PointInRect(&point, &layout.instrument_button_rect)) {
        return midi_editor_handle_instrument_button(state, &selection);
    }
    if (SDL_PointInRect(&point, &layout.instrument_panel_button_rect)) {
        return midi_editor_open_instrument_panel(state);
    }
    if (SDL_PointInRect(&point, &layout.test_button_rect)) {
        return midi_editor_toggle_qwerty_test(state);
    }
    if (SDL_PointInRect(&point, &layout.quantize_button_rect)) {
        return midi_editor_quantize_selected_note(state) || midi_editor_point_in_panel(state, x, y);
    }
    if (SDL_PointInRect(&point, &layout.quantize_down_button_rect)) {
        return midi_editor_adjust_quantize_division(state, -1);
    }
    if (SDL_PointInRect(&point, &layout.quantize_up_button_rect)) {
        return midi_editor_adjust_quantize_division(state, 1);
    }
    if (SDL_PointInRect(&point, &layout.octave_down_button_rect)) {
        return midi_editor_adjust_octave(state, -1);
    }
    if (SDL_PointInRect(&point, &layout.octave_up_button_rect)) {
        return midi_editor_adjust_octave(state, 1);
    }
    if (SDL_PointInRect(&point, &layout.velocity_down_button_rect)) {
        return midi_editor_adjust_default_velocity(state, -0.05f);
    }
    if (SDL_PointInRect(&point, &layout.velocity_up_button_rect)) {
        return midi_editor_adjust_default_velocity(state, 0.05f);
    }
    state->midi_editor_ui.instrument_menu_open = false;
    if (SDL_PointInRect(&point, &layout.time_ruler_rect)) {
        midi_editor_clear_hover_note(state);
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        return midi_editor_seek_to_editor_x(state, &selection, &layout, x);
    }
    if (!SDL_PointInRect(&point, &layout.grid_rect)) {
        return midi_editor_point_in_panel(state, x, y);
    }

    MidiEditorNoteHit hit = {0};
    if (midi_editor_hit_test_note(&layout, selection.clip, x, y, &hit)) {
        const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
        if (!notes || hit.note_index < 0 || hit.note_index >= engine_clip_midi_note_count(selection.clip)) {
            return true;
        }
        SDL_Keymod mods = SDL_GetModState();
        bool shift_held = (mods & KMOD_SHIFT) != 0;
        if (shift_held) {
            midi_editor_clear_marquee_state(state);
            midi_editor_clear_note_press_pending(state);
            return midi_editor_begin_shift_note_pending(state, &selection, hit.note_index, x, y);
        }
        bool note_was_selected = midi_editor_note_is_selected(state, &selection, hit.note_index);
        int selected_count = midi_editor_effective_selected_note_count(
            state,
            &selection,
            engine_clip_midi_note_count(selection.clip));
        if (note_was_selected) {
            midi_editor_clear_shift_note_pending(state);
            midi_editor_clear_marquee_state(state);
            return midi_editor_begin_note_press_pending(state,
                                                        &selection,
                                                        hit.note_index,
                                                        hit.part,
                                                        x,
                                                        y,
                                                        selected_count > 1);
        }
        midi_editor_select_note(state, &selection, hit.note_index);
        return true;
    }
    midi_editor_clear_hover_note(state);

    SDL_Keymod mods = SDL_GetModState();
    if ((mods & KMOD_SHIFT) != 0) {
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        return midi_editor_begin_marquee(state, &selection, x, y, true);
    }

    uint8_t note = 60;
    uint64_t frame = 0;
    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    if (!midi_editor_point_to_note_frame(&layout, clip_frames, x, y, &note, &frame)) {
        return true;
    }
    frame = midi_editor_snap_relative_frame(state, selection.clip, frame);
    uint64_t duration = midi_editor_default_note_duration(state, selection.clip);
    if (frame + duration > clip_frames) {
        frame = clip_frames > duration ? clip_frames - duration : 0;
    }
    EngineMidiNote new_note = {frame, duration, note, midi_editor_current_default_velocity(state)};
    int new_index = -1;
    if (!midi_editor_begin_note_undo(state, &selection)) {
        return true;
    }
    if (!engine_clip_midi_add_note(state->engine, selection.track_index, selection.clip_index, new_note, &new_index)) {
        undo_manager_cancel_drag(&state->undo);
        return true;
    }
    midi_editor_select_note(state, &selection, new_index);
    midi_editor_set_drag_state(state, &selection, MIDI_EDITOR_DRAG_CREATE, new_index, x, y, new_note);
    if (state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_MIDI_NOTE_EDIT) {
        state->midi_editor_ui.drag_mutated = true;
    }
    return true;
}

bool midi_editor_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event || !midi_editor_should_render(state) ||
        state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT) {
        return false;
    }

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            return midi_editor_handle_left_down(state, event->button.x, event->button.y);
        }
        return midi_editor_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            state->midi_editor_ui.instrument_param_drag_active = false;
            state->midi_editor_ui.instrument_param_drag_index = -1;
            midi_editor_commit_shift_note_pending(state);
            midi_editor_commit_note_press_pending(state);
            midi_editor_commit_marquee(state);
            midi_editor_end_drag(state);
        }
        return midi_editor_point_in_panel(state, event->button.x, event->button.y);
    case SDL_MOUSEMOTION:
        if (state->midi_editor_ui.shift_note_pending) {
            midi_editor_update_shift_note_pending(state, event->motion.x, event->motion.y);
        } else if (state->midi_editor_ui.note_press_pending) {
            midi_editor_update_note_press_pending(state, event->motion.x, event->motion.y);
        } else if (state->midi_editor_ui.marquee_active) {
            MidiEditorSelection selection = {0};
            MidiEditorLayout layout = {0};
            if (midi_editor_get_fresh_selection(state, &selection, &layout)) {
                midi_editor_update_marquee_preview(state, &selection, &layout, event->motion.x, event->motion.y);
            } else {
                midi_editor_clear_marquee_state(state);
            }
        } else if (state->midi_editor_ui.drag_active) {
            midi_editor_update_drag(state, event->motion.x, event->motion.y);
        } else {
            MidiEditorSelection selection = {0};
            MidiEditorLayout layout = {0};
            if (midi_editor_get_fresh_selection(state, &selection, &layout) &&
                SDL_PointInRect(&(SDL_Point){event->motion.x, event->motion.y}, &layout.grid_rect)) {
                midi_editor_update_hover_note(state, &selection, &layout, event->motion.x, event->motion.y);
            } else {
                midi_editor_clear_hover_note(state);
            }
        }
        return midi_editor_point_in_panel(state, event->motion.x, event->motion.y);
    case SDL_MOUSEWHEEL:
        return midi_editor_handle_scroll(state, event);
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event->type == SDL_KEYDOWN && midi_editor_handle_clipboard_keydown(state, event)) {
            return true;
        }
        if (event->type == SDL_KEYDOWN && midi_editor_handle_viewport_keydown(state, event)) {
            return true;
        }
        if (midi_editor_handle_qwerty_event(state, event)) {
            return true;
        }
        if (event->type == SDL_KEYUP) {
            break;
        }
        if (event->key.keysym.sym == SDLK_ESCAPE && state->midi_editor_ui.instrument_menu_open) {
            state->midi_editor_ui.instrument_menu_open = false;
            return true;
        }
        if (event->key.keysym.sym == SDLK_DELETE || event->key.keysym.sym == SDLK_BACKSPACE) {
            return midi_editor_delete_selected_note(state);
        }
        break;
    default:
        break;
    }
    return false;
}

void midi_editor_input_update(InputManager* manager, AppState* state, bool left_was_down, bool left_is_down) {
    (void)manager;
    if (!state) {
        return;
    }
    if (!midi_editor_should_render(state)) {
        if (state->midi_editor_ui.qwerty_record_armed) {
            midi_editor_complete_all_qwerty_notes(state);
            state->midi_editor_ui.qwerty_record_armed = false;
        }
        state->midi_editor_ui.qwerty_test_enabled = false;
        state->midi_editor_ui.instrument_menu_open = false;
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        midi_editor_clear_qwerty_active_notes(state);
        return;
    }
    if (state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT) {
        if (state->midi_editor_ui.qwerty_record_armed) {
            midi_editor_complete_all_qwerty_notes(state);
        }
        state->midi_editor_ui.qwerty_record_armed = false;
        state->midi_editor_ui.qwerty_test_enabled = false;
        midi_editor_clear_qwerty_active_notes(state);
        midi_editor_clear_shift_note_pending(state);
        midi_editor_clear_note_press_pending(state);
        midi_editor_clear_marquee_state(state);
        midi_editor_end_drag(state);
        return;
    }
    if (state->midi_editor_ui.shift_note_pending && left_is_down) {
        midi_editor_update_shift_note_pending(state, state->mouse_x, state->mouse_y);
    } else if (state->midi_editor_ui.note_press_pending && left_is_down) {
        midi_editor_update_note_press_pending(state, state->mouse_x, state->mouse_y);
    } else if (state->midi_editor_ui.marquee_active && left_is_down) {
        MidiEditorSelection selection = {0};
        MidiEditorLayout layout = {0};
        if (midi_editor_get_fresh_selection(state, &selection, &layout)) {
            midi_editor_update_marquee_preview(state, &selection, &layout, state->mouse_x, state->mouse_y);
        } else {
            midi_editor_clear_marquee_state(state);
        }
    } else if (state->midi_editor_ui.drag_active && left_is_down) {
        midi_editor_update_drag(state, state->mouse_x, state->mouse_y);
    }
    if (left_was_down && !left_is_down) {
        state->midi_editor_ui.instrument_param_drag_active = false;
        state->midi_editor_ui.instrument_param_drag_index = -1;
        midi_editor_commit_shift_note_pending(state);
        midi_editor_commit_note_press_pending(state);
        midi_editor_commit_marquee(state);
        midi_editor_end_drag(state);
    }
}

bool midi_editor_input_qwerty_capturing(const AppState* state) {
    return state &&
           state->midi_editor_ui.panel_mode != MIDI_REGION_PANEL_INSTRUMENT &&
           (state->midi_editor_ui.qwerty_record_armed ||
                     state->midi_editor_ui.qwerty_test_enabled);
}
