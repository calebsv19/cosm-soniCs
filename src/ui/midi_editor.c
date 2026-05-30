#include "ui/midi_editor.h"
#include "midi_editor_internal.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

enum {
    MIDI_EDITOR_MARGIN = 12,
    MIDI_EDITOR_HEADER_HEIGHT = 28,
    MIDI_EDITOR_FOOTER_HEIGHT = 18,
    MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH = 42,
    MIDI_EDITOR_PIANO_MIN_WIDTH = 48,
    MIDI_EDITOR_PIANO_MAX_WIDTH = 70,
    MIDI_EDITOR_GRID_MIN_WIDTH = 80,
    MIDI_EDITOR_GRID_MIN_HEIGHT = 48,
    MIDI_EDITOR_TIME_RULER_HEIGHT = 20
};

int midi_editor_max_int(int a, int b) {
    return a > b ? a : b;
}

static int midi_editor_min_int(int a, int b) {
    return a < b ? a : b;
}

bool midi_editor_rect_valid(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static bool midi_editor_viewport_matches_selection(const AppState* state,
                                                   const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.viewport_track_index == selection->track_index &&
           state->midi_editor_ui.viewport_clip_index == selection->clip_index &&
           state->midi_editor_ui.viewport_clip_creation_index == selection->clip->creation_index;
}

static void midi_editor_resolve_viewport(const AppState* state,
                                         const MidiEditorSelection* selection,
                                         uint64_t* out_start,
                                         uint64_t* out_end,
                                         uint64_t* out_span) {
    uint64_t clip_frames = selection && selection->clip && selection->clip->duration_frames > 0
        ? selection->clip->duration_frames
        : 1u;
    uint64_t start = 0;
    uint64_t span = clip_frames;
    if (midi_editor_viewport_matches_selection(state, selection) &&
        state->midi_editor_ui.viewport_span_frames > 0 &&
        state->midi_editor_ui.viewport_span_frames < clip_frames) {
        span = state->midi_editor_ui.viewport_span_frames;
        start = state->midi_editor_ui.viewport_start_frame;
        if (start > clip_frames) {
            start = clip_frames;
        }
        if (start + span > clip_frames || start + span < start) {
            start = clip_frames > span ? clip_frames - span : 0;
        }
    }
    uint64_t end = start + span;
    if (end > clip_frames || end < start) {
        end = clip_frames;
    }
    if (end <= start) {
        end = start + 1u;
    }
    if (out_start) {
        *out_start = start;
    }
    if (out_end) {
        *out_end = end;
    }
    if (out_span) {
        *out_span = end - start;
    }
}

static SDL_Rect midi_editor_inset_rect(SDL_Rect rect, int inset_x, int inset_y) {
    if (inset_x < 0) {
        inset_x = 0;
    }
    if (inset_y < 0) {
        inset_y = 0;
    }
    if (rect.w > inset_x * 2) {
        rect.x += inset_x;
        rect.w -= inset_x * 2;
    } else {
        rect.x += rect.w / 2;
        rect.w = 0;
    }
    if (rect.h > inset_y * 2) {
        rect.y += inset_y;
        rect.h -= inset_y * 2;
    } else {
        rect.y += rect.h / 2;
        rect.h = 0;
    }
    return rect;
}

int midi_editor_sample_rate(const AppState* state) {
    if (state && state->runtime_cfg.sample_rate > 0) {
        return state->runtime_cfg.sample_rate;
    }
    if (state && state->engine) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        if (cfg && cfg->sample_rate > 0) {
            return cfg->sample_rate;
        }
    }
    return 48000;
}

int midi_editor_quantize_division(const AppState* state) {
    static const int divisions[] = {4, 8, 16, 32, 64};
    int value = state ? state->midi_editor_ui.quantize_division : 0;
    for (int i = 0; i < (int)(sizeof(divisions) / sizeof(divisions[0])); ++i) {
        if (divisions[i] == value) {
            return value;
        }
    }
    return 16;
}

const char* midi_editor_quantize_label(const AppState* state) {
    switch (midi_editor_quantize_division(state)) {
    case 4: return "Q 1/4";
    case 8: return "Q 1/8";
    case 32: return "Q 1/32";
    case 64: return "Q 1/64";
    case 16:
    default:
        return "Q 1/16";
    }
}

bool midi_editor_get_selection(const AppState* state, MidiEditorSelection* out_selection) {
    if (out_selection) {
        memset(out_selection, 0, sizeof(*out_selection));
        out_selection->track_index = -1;
        out_selection->clip_index = -1;
    }
    if (!state || !state->engine) {
        return false;
    }

    int track_index = state->selected_track_index;
    int clip_index = state->selected_clip_index;
    if ((track_index < 0 || clip_index < 0) && state->selection_count == 1) {
        track_index = state->selection[0].track_index;
        clip_index = state->selection[0].clip_index;
    }

    int track_count = engine_get_track_count(state->engine);
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return false;
    }
    const EngineTrack* track = &tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }

    if (out_selection) {
        out_selection->track_index = track_index;
        out_selection->clip_index = clip_index;
        out_selection->track = track;
        out_selection->clip = clip;
    }
    return true;
}

bool midi_editor_should_render(const AppState* state) {
    return midi_editor_get_selection(state, NULL);
}

void midi_editor_compute_layout(const AppState* state, MidiEditorLayout* layout) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    layout->highest_note = 71;
    layout->lowest_note = 48;

    const Pane* pane = ui_layout_get_pane(state, 2);
    if (!pane) {
        return;
    }
    layout->panel_rect = pane->rect;
    if (!midi_editor_rect_valid(&layout->panel_rect)) {
        return;
    }

    SDL_Rect content = midi_editor_inset_rect(layout->panel_rect, MIDI_EDITOR_MARGIN, MIDI_EDITOR_MARGIN);
    if (!midi_editor_rect_valid(&content)) {
        return;
    }

    int header_h = midi_editor_max_int(MIDI_EDITOR_HEADER_HEIGHT, ui_font_line_height(0.9f) + 8);
    header_h = midi_editor_min_int(header_h, content.h);
    layout->header_rect = (SDL_Rect){content.x, content.y, content.w, header_h};

    int button_gap = 6;
    int button_y = layout->header_rect.y + 3;
    int button_h = layout->header_rect.h - 6;
    if (button_h < 18) {
        button_h = layout->header_rect.h;
        button_y = layout->header_rect.y;
    }
    int test_w = ui_measure_text_width("Test", 0.8f) + 14;
    if (test_w < MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH) {
        test_w = MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH;
    }
    if (test_w > content.w / 4) {
        test_w = content.w / 4;
    }
    layout->test_button_rect = (SDL_Rect){content.x + content.w - test_w,
                                          button_y,
                                          test_w,
                                          button_h};
    int small_button_w = ui_measure_text_width("Oct+", 0.75f) + 12;
    if (small_button_w < 38) {
        small_button_w = 38;
    }
    int quantize_w = ui_measure_text_width(midi_editor_quantize_label(state), 0.75f) + 12;
    if (quantize_w < MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH) {
        quantize_w = MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH;
    }
    int control_x = layout->test_button_rect.x - button_gap - small_button_w;
    layout->quantize_up_button_rect = (SDL_Rect){control_x,
                                                 button_y,
                                                 small_button_w,
                                                 button_h};
    control_x -= button_gap + small_button_w;
    layout->quantize_down_button_rect = (SDL_Rect){control_x,
                                                   button_y,
                                                   small_button_w,
                                                   button_h};
    control_x -= button_gap + quantize_w;
    layout->quantize_button_rect = (SDL_Rect){control_x,
                                              button_y,
                                              quantize_w,
                                              button_h};
    control_x -= button_gap + small_button_w;
    layout->octave_up_button_rect = (SDL_Rect){control_x,
                                               button_y,
                                               small_button_w,
                                               button_h};
    control_x -= button_gap + small_button_w;
    layout->octave_down_button_rect = (SDL_Rect){control_x,
                                                 button_y,
                                                 small_button_w,
                                                 button_h};
    control_x -= button_gap + small_button_w;
    layout->velocity_up_button_rect = (SDL_Rect){control_x,
                                                 button_y,
                                                 small_button_w,
                                                 button_h};
    control_x -= button_gap + small_button_w;
    layout->velocity_down_button_rect = (SDL_Rect){control_x,
                                                   button_y,
                                                   small_button_w,
                                                   button_h};
    int edit_w = ui_measure_text_width("Edit", 0.75f) + 12;
    if (edit_w < MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH) {
        edit_w = MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH;
    }

    MidiEditorSelection selection = {0};
    EngineInstrumentPresetId preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    bool has_selection = midi_editor_get_selection(state, &selection);
    if (has_selection) {
        preset = engine_clip_midi_effective_instrument_preset(state->engine,
                                                              selection.track_index,
                                                              selection.clip_index);
        midi_editor_resolve_viewport(state,
                                     &selection,
                                     &layout->view_start_frame,
                                     &layout->view_end_frame,
                                     &layout->view_span_frames);
    } else {
        layout->view_start_frame = 0;
        layout->view_end_frame = 1;
        layout->view_span_frames = 1;
    }
    char instrument_label[64];
    snprintf(instrument_label,
             sizeof(instrument_label),
             "Instrument: %s",
             engine_instrument_preset_display_name(preset));
    int instrument_w = ui_measure_text_width(instrument_label, 0.8f) + 14;
    if (instrument_w < MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH * 2) {
        instrument_w = MIDI_EDITOR_HEADER_BUTTON_MIN_WIDTH * 2;
    }
    int control_button_total_w = test_w + quantize_w + edit_w + (small_button_w * 6) + (button_gap * 9);
    int max_instrument_w = content.w - control_button_total_w;
    if (instrument_w > max_instrument_w) {
        instrument_w = max_instrument_w;
    }
    if (instrument_w < 0) {
        instrument_w = 0;
    }
    layout->instrument_panel_button_rect = (SDL_Rect){layout->velocity_down_button_rect.x - button_gap - edit_w,
                                                      button_y,
                                                      edit_w,
                                                      button_h};
    layout->instrument_button_rect = (SDL_Rect){layout->instrument_panel_button_rect.x - button_gap - instrument_w,
                                                button_y,
                                                instrument_w,
                                                button_h};
    int menu_bottom = content.y + content.h;
    midi_preset_browser_compute_layout(layout->instrument_button_rect,
                                       menu_bottom,
                                       state ? state->midi_editor_ui.instrument_menu_scroll_row : 0,
                                       state ? (EngineInstrumentPresetCategoryId)state->midi_editor_ui.instrument_menu_expanded_category
                                             : ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT,
                                       &layout->instrument_browser);
    layout->instrument_menu_rect = layout->instrument_browser.menu_rect;
    layout->instrument_menu_item_count = engine_instrument_preset_count();
    if (layout->instrument_menu_item_count > ENGINE_INSTRUMENT_PRESET_COUNT) {
        layout->instrument_menu_item_count = ENGINE_INSTRUMENT_PRESET_COUNT;
    }
    if (layout->instrument_menu_item_count < 0) {
        layout->instrument_menu_item_count = 0;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        layout->instrument_menu_item_rects[i] =
            midi_preset_browser_rect_for_preset(&layout->instrument_browser, (EngineInstrumentPresetId)i);
    }

    int title_w = ui_measure_text_width("MIDI Editor", 1.0f) + 12;
    int header_buttons_left = layout->instrument_button_rect.w > 0
        ? layout->instrument_button_rect.x
        : layout->test_button_rect.x;
    int max_title_w = header_buttons_left - content.x - button_gap;
    if (max_title_w < 0) {
        max_title_w = 0;
    }
    title_w = midi_editor_min_int(title_w, max_title_w);
    title_w = midi_editor_min_int(title_w, content.w);
    layout->title_rect = (SDL_Rect){layout->header_rect.x,
                                    layout->header_rect.y,
                                    title_w,
                                    layout->header_rect.h};
    int summary_x = layout->title_rect.x + layout->title_rect.w + 8;
    int summary_w = header_buttons_left - button_gap - summary_x;
    if (summary_w < 0) {
        summary_w = 0;
    }
    layout->summary_rect = (SDL_Rect){summary_x,
                                      layout->header_rect.y,
                                      summary_w,
                                      layout->header_rect.h};

    int param_h = 0;
    layout->instrument_param_count = 0;

    int footer_h = midi_editor_min_int(MIDI_EDITOR_FOOTER_HEIGHT, content.h - header_h - param_h);
    if (footer_h < 0) {
        footer_h = 0;
    }
    int body_y = content.y + header_h + 8;
    int footer_y = content.y + content.h - footer_h;
    int body_h = footer_y - body_y - 8;
    if (body_h < MIDI_EDITOR_GRID_MIN_HEIGHT) {
        body_h = footer_y - body_y;
    }
    if (body_h < 0) {
        body_h = 0;
    }
    layout->body_rect = (SDL_Rect){content.x, body_y, content.w, body_h};
    layout->footer_rect = (SDL_Rect){content.x, footer_y, content.w, footer_h};

    int piano_w = content.w / 7;
    if (piano_w < MIDI_EDITOR_PIANO_MIN_WIDTH) {
        piano_w = MIDI_EDITOR_PIANO_MIN_WIDTH;
    }
    if (piano_w > MIDI_EDITOR_PIANO_MAX_WIDTH) {
        piano_w = MIDI_EDITOR_PIANO_MAX_WIDTH;
    }
    if (piano_w + MIDI_EDITOR_GRID_MIN_WIDTH > layout->body_rect.w) {
        piano_w = midi_editor_max_int(0, layout->body_rect.w - MIDI_EDITOR_GRID_MIN_WIDTH);
    }
    int ruler_h = MIDI_EDITOR_TIME_RULER_HEIGHT;
    if (layout->body_rect.h < MIDI_EDITOR_GRID_MIN_HEIGHT + ruler_h) {
        ruler_h = layout->body_rect.h / 5;
        if (ruler_h < 8) {
            ruler_h = 0;
        }
    }
    layout->time_ruler_rect = (SDL_Rect){layout->body_rect.x + piano_w,
                                         layout->body_rect.y,
                                         layout->body_rect.w - piano_w,
                                         ruler_h};
    int lane_y = layout->body_rect.y + ruler_h;
    int lane_h = layout->body_rect.h - ruler_h;
    if (lane_h < 0) {
        lane_h = 0;
    }
    layout->piano_rect = (SDL_Rect){layout->body_rect.x,
                                    lane_y,
                                    piano_w,
                                    lane_h};
    layout->grid_rect = (SDL_Rect){layout->piano_rect.x + layout->piano_rect.w,
                                   lane_y,
                                   layout->body_rect.w - layout->piano_rect.w,
                                   lane_h};

    int rows = MIDI_EDITOR_VISIBLE_KEY_ROWS;
    if (layout->grid_rect.h > 0 && layout->grid_rect.h / rows < 5) {
        rows = layout->grid_rect.h / 5;
    }
    if (rows < 1) {
        rows = 1;
    }
    int top_note = layout->highest_note;
    if (has_selection) {
        midi_editor_resolve_pitch_viewport(state, &selection, rows, &top_note, &rows);
    } else {
        midi_editor_resolve_pitch_viewport(state, NULL, rows, &top_note, &rows);
    }
    layout->key_row_count = rows;
    layout->highest_note = top_note;
    layout->lowest_note = layout->highest_note - rows + 1;
    int row_h = rows > 0 ? layout->grid_rect.h / rows : 0;
    int remainder = rows > 0 ? layout->grid_rect.h % rows : 0;
    int y = layout->grid_rect.y;
    for (int i = 0; i < rows; ++i) {
        int h = row_h + (i < remainder ? 1 : 0);
        layout->key_label_rects[i] = (SDL_Rect){layout->piano_rect.x, y, layout->piano_rect.w, h};
        layout->key_lane_rects[i] = (SDL_Rect){layout->grid_rect.x, y, layout->grid_rect.w, h};
        y += h;
    }
}

bool midi_editor_note_rect(const MidiEditorLayout* layout,
                           const EngineMidiNote* note,
                           uint64_t clip_frames,
                           SDL_Rect* out_rect) {
    if (out_rect) {
        *out_rect = (SDL_Rect){0, 0, 0, 0};
    }
    if (!layout || !note || clip_frames == 0 || !midi_editor_rect_valid(&layout->grid_rect)) {
        return false;
    }
    int row = 0;
    if (!midi_editor_pitch_note_to_row(layout, (int)note->note, &row)) {
        return false;
    }
    SDL_Rect lane = layout->key_lane_rects[row];
    uint64_t visible_start = layout->view_start_frame;
    uint64_t visible_end = layout->view_end_frame;
    uint64_t visible_span = layout->view_span_frames;
    if (visible_span == 0 || visible_end <= visible_start || visible_end > clip_frames) {
        visible_start = 0;
        visible_end = clip_frames;
        visible_span = clip_frames;
    }
    uint64_t note_end = note->start_frame + note->duration_frames;
    if (note_end < note->start_frame) {
        note_end = UINT64_MAX;
    }
    if (note_end <= visible_start || note->start_frame >= visible_end) {
        return false;
    }
    double start_t = ((double)note->start_frame - (double)visible_start) / (double)visible_span;
    double end_t = ((double)note_end - (double)visible_start) / (double)visible_span;
    if (start_t < 0.0) start_t = 0.0;
    if (end_t > 1.0) end_t = 1.0;
    if (end_t <= start_t) {
        end_t = start_t + 0.01;
    }

    int x = layout->grid_rect.x + (int)(start_t * (double)layout->grid_rect.w);
    int w = (int)((end_t - start_t) * (double)layout->grid_rect.w);
    if (w < 3) {
        w = 3;
    }
    if (x + w > layout->grid_rect.x + layout->grid_rect.w) {
        w = layout->grid_rect.x + layout->grid_rect.w - x;
    }
    SDL_Rect note_rect = {x + 1, lane.y + 1, w - 2, lane.h - 2};
    if (note_rect.w < 1) note_rect.w = 1;
    if (note_rect.h < 1) note_rect.h = 1;
    if (out_rect) {
        *out_rect = note_rect;
    }
    return true;
}

bool midi_editor_hit_test_note(const MidiEditorLayout* layout,
                               const EngineClip* clip,
                               int x,
                               int y,
                               MidiEditorNoteHit* out_hit) {
    if (out_hit) {
        memset(out_hit, 0, sizeof(*out_hit));
        out_hit->note_index = -1;
        out_hit->part = MIDI_EDITOR_NOTE_HIT_NONE;
    }
    if (!layout || !clip || engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    int note_count = engine_clip_midi_note_count(clip);
    uint64_t clip_frames = clip->duration_frames > 0 ? clip->duration_frames : 1u;
    SDL_Point point = {x, y};
    for (int i = note_count - 1; notes && i >= 0; --i) {
        SDL_Rect rect = {0, 0, 0, 0};
        if (!midi_editor_note_rect(layout, &notes[i], clip_frames, &rect)) {
            continue;
        }
        SDL_Rect hit_rect = {
            rect.x - 3,
            rect.y - 2,
            rect.w + 6,
            rect.h + 4
        };
        if (!SDL_PointInRect(&point, &hit_rect)) {
            continue;
        }
        MidiEditorNoteHitPart part = MIDI_EDITOR_NOTE_HIT_BODY;
        int edge_w = rect.w < 16 ? 4 : 6;
        if (x <= rect.x + edge_w) {
            part = MIDI_EDITOR_NOTE_HIT_LEFT_EDGE;
        } else if (x >= rect.x + rect.w - edge_w) {
            part = MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE;
        }
        if (out_hit) {
            out_hit->note_index = i;
            out_hit->rect = rect;
            out_hit->part = part;
        }
        return true;
    }
    return false;
}

bool midi_editor_point_to_frame(const MidiEditorLayout* layout,
                                uint64_t clip_frames,
                                int x,
                                uint64_t* out_frame) {
    if (!layout || clip_frames == 0 || !midi_editor_rect_valid(&layout->grid_rect)) {
        return false;
    }
    int clamped_x = x;
    if (clamped_x < layout->grid_rect.x) {
        clamped_x = layout->grid_rect.x;
    }
    int grid_right = layout->grid_rect.x + layout->grid_rect.w;
    if (clamped_x > grid_right) {
        clamped_x = grid_right;
    }
    double ratio = layout->grid_rect.w > 0
        ? (double)(clamped_x - layout->grid_rect.x) / (double)layout->grid_rect.w
        : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    uint64_t visible_start = layout->view_start_frame;
    uint64_t visible_end = layout->view_end_frame;
    uint64_t visible_span = layout->view_span_frames;
    if (visible_span == 0 || visible_end <= visible_start || visible_end > clip_frames) {
        visible_start = 0;
        visible_span = clip_frames;
    }
    uint64_t frame = visible_start + (uint64_t)(ratio * (double)visible_span + 0.5);
    if (frame > clip_frames || frame < visible_start) {
        frame = clip_frames;
    }
    if (out_frame) {
        *out_frame = frame;
    }
    return true;
}

bool midi_editor_point_to_note_frame(const MidiEditorLayout* layout,
                                     uint64_t clip_frames,
                                     int x,
                                     int y,
                                     uint8_t* out_note,
                                     uint64_t* out_frame) {
    if (!layout || clip_frames == 0 || !midi_editor_rect_valid(&layout->grid_rect)) {
        return false;
    }
    SDL_Point point = {x, y};
    if (!SDL_PointInRect(&point, &layout->grid_rect)) {
        return false;
    }
    int row = -1;
    for (int i = 0; i < layout->key_row_count; ++i) {
        if (SDL_PointInRect(&point, &layout->key_lane_rects[i])) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return false;
    }
    uint8_t note = 0;
    if (!midi_editor_pitch_row_to_note(layout, row, &note)) {
        return false;
    }
    uint64_t frame = 0;
    if (!midi_editor_point_to_frame(layout, clip_frames, x, &frame)) {
        return false;
    }
    if (out_note) {
        *out_note = note;
    }
    if (out_frame) {
        *out_frame = frame;
    }
    return true;
}
