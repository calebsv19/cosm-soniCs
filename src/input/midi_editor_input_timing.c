#include "input/midi_editor_input_internal.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>
#include <math.h>

static const int k_midi_editor_quantize_divisions[] = {4, 8, 16, 32, 64};

uint64_t midi_editor_clip_frames(const EngineClip* clip) {
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

void midi_editor_fit_viewport(AppState* state, const MidiEditorSelection* selection) {
    if (!state || !selection || !selection->clip) {
        return;
    }
    midi_editor_store_viewport(state, selection, 0, midi_editor_clip_frames(selection->clip));
}

bool midi_editor_seek_to_editor_x(AppState* state,
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

bool midi_editor_pointer_over_editor_grid_or_ruler(const MidiEditorLayout* layout, int x, int y) {
    if (!layout) {
        return false;
    }
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &layout->grid_rect) ||
           SDL_PointInRect(&point, &layout->time_ruler_rect);
}

bool midi_editor_pointer_over_pitch_area(const MidiEditorLayout* layout, int x, int y) {
    if (!layout) {
        return false;
    }
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &layout->grid_rect) ||
           SDL_PointInRect(&point, &layout->piano_rect);
}

bool midi_editor_pan_viewport(AppState* state,
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

bool midi_editor_zoom_viewport(AppState* state,
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

static int midi_editor_pitch_row_capacity_for_layout(const MidiEditorLayout* layout) {
    int rows = MIDI_EDITOR_VISIBLE_KEY_ROWS;
    if (layout && layout->grid_rect.h > 0 && layout->grid_rect.h / rows < 5) {
        rows = layout->grid_rect.h / 5;
    }
    if (rows < 1) {
        rows = 1;
    }
    return rows;
}

static int midi_editor_pitch_row_at_y(const MidiEditorLayout* layout, int y) {
    if (!layout || layout->key_row_count <= 0) {
        return -1;
    }
    for (int row = 0; row < layout->key_row_count; ++row) {
        const SDL_Rect* lane = &layout->key_lane_rects[row];
        if (y >= lane->y && y < lane->y + lane->h) {
            return row;
        }
    }
    if (y < layout->grid_rect.y) {
        return 0;
    }
    if (y >= layout->grid_rect.y + layout->grid_rect.h) {
        return layout->key_row_count - 1;
    }
    return -1;
}

bool midi_editor_pan_pitch_viewport(AppState* state,
                                    const MidiEditorSelection* selection,
                                    const MidiEditorLayout* layout,
                                    int direction_steps) {
    if (!state || !selection || !selection->clip || !layout || direction_steps == 0 ||
        layout->key_row_count <= 0) {
        return false;
    }
    int top = layout->highest_note + direction_steps;
    midi_editor_store_pitch_viewport(state, selection, top, layout->key_row_count);
    return true;
}

bool midi_editor_zoom_pitch_viewport(AppState* state,
                                     const MidiEditorSelection* selection,
                                     const MidiEditorLayout* layout,
                                     int anchor_y,
                                     int row_delta) {
    if (!state || !selection || !selection->clip || !layout || row_delta == 0 ||
        layout->key_row_count <= 0) {
        return false;
    }
    int capacity = midi_editor_pitch_row_capacity_for_layout(layout);
    int old_rows = layout->key_row_count;
    int new_rows = old_rows + row_delta;
    if (new_rows < 1) {
        new_rows = 1;
    }
    if (new_rows > capacity) {
        new_rows = capacity;
    }
    if (new_rows == old_rows) {
        midi_editor_store_pitch_viewport(state, selection, layout->highest_note, old_rows);
        return true;
    }

    int old_anchor_row = midi_editor_pitch_row_at_y(layout, anchor_y);
    if (old_anchor_row < 0) {
        old_anchor_row = old_rows / 2;
    }
    int anchor_note = layout->highest_note - old_anchor_row;
    double row_ratio = old_rows > 1 ? (double)old_anchor_row / (double)(old_rows - 1) : 0.0;
    int new_anchor_row = (int)lround(row_ratio * (double)(new_rows - 1));
    int top = anchor_note + new_anchor_row;
    midi_editor_store_pitch_viewport(state, selection, top, new_rows);
    return true;
}

bool midi_editor_fit_pitch_viewport_to_selected_notes(AppState* state,
                                                      const MidiEditorSelection* selection,
                                                      const MidiEditorLayout* layout) {
    if (!state || !selection || !selection->clip || !layout || layout->key_row_count <= 0) {
        return false;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(selection->clip);
    int note_count = engine_clip_midi_note_count(selection->clip);
    if (!notes || note_count <= 0) {
        return true;
    }
    if (note_count > ENGINE_MIDI_NOTE_CAP) {
        note_count = ENGINE_MIDI_NOTE_CAP;
    }
    bool selected_mask[ENGINE_MIDI_NOTE_CAP] = {false};
    midi_editor_build_effective_selection_mask(state, selection, note_count, selected_mask);
    int min_note = ENGINE_MIDI_NOTE_MAX;
    int max_note = ENGINE_MIDI_NOTE_MIN;
    bool any_selected = false;
    for (int i = 0; i < note_count; ++i) {
        if (!selected_mask[i]) {
            continue;
        }
        int note = (int)notes[i].note;
        if (note < min_note) {
            min_note = note;
        }
        if (note > max_note) {
            max_note = note;
        }
        any_selected = true;
    }
    if (!any_selected) {
        return true;
    }

    int capacity = midi_editor_pitch_row_capacity_for_layout(layout);
    int selected_span = max_note - min_note + 1;
    if (selected_span < 1) {
        selected_span = 1;
    }
    int rows = layout->key_row_count;
    if (rows < selected_span) {
        rows = selected_span;
    }
    if (rows > capacity) {
        rows = capacity;
    }
    if (rows < 1) {
        rows = 1;
    }

    int top = 0;
    if (selected_span <= rows) {
        int extra_rows = rows - selected_span;
        top = max_note + extra_rows / 2;
    } else {
        int center = (min_note + max_note) / 2;
        top = center + rows / 2;
    }
    midi_editor_store_pitch_viewport(state, selection, top, rows);
    return true;
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

int midi_editor_current_quantize_division(const AppState* state) {
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

bool midi_editor_adjust_quantize_division(AppState* state, int delta) {
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

uint64_t midi_editor_quantize_step_frames_at(const AppState* state,
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

uint64_t midi_editor_quantize_relative_frame(const AppState* state,
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

uint64_t midi_editor_default_note_duration(const AppState* state, const EngineClip* clip) {
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

uint64_t midi_editor_snap_relative_frame(const AppState* state,
                                         const EngineClip* clip,
                                         uint64_t frame) {
    if (!state || !clip || !state->timeline_snap_enabled) {
        return frame;
    }
    return midi_editor_quantize_relative_frame(state, clip, frame, false);
}

bool midi_editor_transport_relative_frame(const AppState* state,
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
