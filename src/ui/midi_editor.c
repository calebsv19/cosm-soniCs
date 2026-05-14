#include "ui/midi_editor.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>
#include <math.h>
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

static int midi_editor_max_int(int a, int b) {
    return a > b ? a : b;
}

static int midi_editor_min_int(int a, int b) {
    return a < b ? a : b;
}

static SDL_Color midi_editor_color_mix(SDL_Color a, SDL_Color b, int a_weight, int b_weight) {
    int total = a_weight + b_weight;
    if (total <= 0) {
        return a;
    }
    return (SDL_Color){
        (Uint8)(((int)a.r * a_weight + (int)b.r * b_weight) / total),
        (Uint8)(((int)a.g * a_weight + (int)b.g * b_weight) / total),
        (Uint8)(((int)a.b * a_weight + (int)b.b * b_weight) / total),
        (Uint8)(((int)a.a * a_weight + (int)b.a * b_weight) / total)
    };
}

static bool midi_editor_rect_valid(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static bool midi_editor_note_active(const AppState* state, int note) {
    if (!state) {
        return false;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (state->midi_editor_ui.qwerty_active_notes[i].active &&
            state->midi_editor_ui.qwerty_active_notes[i].note == (uint8_t)note) {
            return true;
        }
    }
    return false;
}

static bool midi_editor_selection_matches_ui(const AppState* state,
                                             const MidiEditorSelection* selection) {
    return state && selection && selection->clip &&
           state->midi_editor_ui.selected_track_index == selection->track_index &&
           state->midi_editor_ui.selected_clip_index == selection->clip_index &&
           state->midi_editor_ui.selected_clip_creation_index == selection->clip->creation_index;
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

static int midi_editor_selected_note_count(const AppState* state,
                                           const MidiEditorSelection* selection,
                                           int note_count) {
    if (!midi_editor_selection_matches_ui(state, selection) || note_count <= 0) {
        return 0;
    }
    int bounded = note_count < ENGINE_MIDI_NOTE_CAP ? note_count : ENGINE_MIDI_NOTE_CAP;
    int count = 0;
    for (int i = 0; i < bounded; ++i) {
        if (state->midi_editor_ui.selected_note_indices[i]) {
            ++count;
        }
    }
    if (count == 0 &&
        state->midi_editor_ui.selected_note_index >= 0 &&
        state->midi_editor_ui.selected_note_index < bounded) {
        count = 1;
    }
    return count;
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

static const char* midi_editor_pitch_label(int note) {
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (note < 0) {
        note = 0;
    }
    return names[note % 12];
}

static bool midi_editor_note_is_sharp(int note) {
    int pitch_class = note % 12;
    return pitch_class == 1 || pitch_class == 3 || pitch_class == 6 ||
           pitch_class == 8 || pitch_class == 10;
}

static bool midi_editor_note_is_c(int note) {
    return note % 12 == 0;
}

static int midi_editor_sample_rate(const AppState* state) {
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

static int midi_editor_quantize_division(const AppState* state) {
    static const int divisions[] = {4, 8, 16, 32, 64};
    int value = state ? state->midi_editor_ui.quantize_division : 0;
    for (int i = 0; i < (int)(sizeof(divisions) / sizeof(divisions[0])); ++i) {
        if (divisions[i] == value) {
            return value;
        }
    }
    return 16;
}

static const char* midi_editor_quantize_label(const AppState* state) {
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
        preset = engine_clip_midi_instrument_preset(selection.clip);
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
    int menu_items = engine_instrument_preset_count();
    if (menu_items > ENGINE_INSTRUMENT_PRESET_COUNT) {
        menu_items = ENGINE_INSTRUMENT_PRESET_COUNT;
    }
    if (menu_items < 0) {
        menu_items = 0;
    }
    layout->instrument_menu_item_count = menu_items;
    int item_h = midi_editor_max_int(24, ui_font_line_height(0.9f) + 10);
    layout->instrument_menu_rect = (SDL_Rect){layout->instrument_button_rect.x,
                                              layout->instrument_button_rect.y + layout->instrument_button_rect.h + 4,
                                              layout->instrument_button_rect.w,
                                              item_h * menu_items};
    int menu_bottom = content.y + content.h;
    if (layout->instrument_menu_rect.y + layout->instrument_menu_rect.h > menu_bottom) {
        layout->instrument_menu_rect.h = midi_editor_max_int(0, menu_bottom - layout->instrument_menu_rect.y);
    }
    for (int i = 0; i < menu_items; ++i) {
        SDL_Rect item = {layout->instrument_menu_rect.x,
                         layout->instrument_menu_rect.y + item_h * i,
                         layout->instrument_menu_rect.w,
                         item_h};
        if (item.y + item.h > layout->instrument_menu_rect.y + layout->instrument_menu_rect.h) {
            item.h = midi_editor_max_int(0, layout->instrument_menu_rect.y + layout->instrument_menu_rect.h - item.y);
        }
        layout->instrument_menu_item_rects[i] = item;
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
    layout->key_row_count = rows;
    if (rows < 12) {
        layout->highest_note = 60 + rows - 1;
    }
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

static void midi_editor_draw_rect(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color, bool fill) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (fill) {
        SDL_RenderFillRect(renderer, &rect);
    } else {
        SDL_RenderDrawRect(renderer, &rect);
    }
}

static void midi_editor_draw_button(SDL_Renderer* renderer,
                                    SDL_Rect rect,
                                    const char* label,
                                    bool active,
                                    const DawThemePalette* theme) {
    if (!renderer || !theme || !label || !midi_editor_rect_valid(&rect)) {
        return;
    }
    SDL_Color fill = active
        ? midi_editor_color_mix(theme->accent_primary, theme->control_fill, 2, 1)
        : theme->control_fill;
    SDL_Color border = active ? theme->text_primary : theme->control_border;
    SDL_Color text = active ? theme->text_primary : theme->text_muted;
    midi_editor_draw_rect(renderer, rect, fill, true);
    midi_editor_draw_rect(renderer, rect, border, false);
    int line_h = ui_font_line_height(0.8f);
    int y = rect.y + midi_editor_max_int(0, (rect.h - line_h) / 2);
    ui_draw_text_clipped(renderer, rect.x + 6, y, label, text, 0.8f, rect.w - 10);
}

static void midi_editor_draw_instrument_menu(SDL_Renderer* renderer,
                                             const AppState* state,
                                             const MidiEditorLayout* layout,
                                             const MidiEditorSelection* selection,
                                             const DawThemePalette* theme) {
    if (!renderer || !state || !layout || !selection || !selection->clip || !theme ||
        !state->midi_editor_ui.instrument_menu_open ||
        !midi_editor_rect_valid(&layout->instrument_menu_rect)) {
        return;
    }
    SDL_Color fill = midi_editor_color_mix(theme->control_fill, theme->inspector_fill, 3, 1);
    SDL_Color active_fill = midi_editor_color_mix(theme->accent_primary, theme->control_fill, 1, 2);
    EngineInstrumentPresetId selected = engine_clip_midi_instrument_preset(selection->clip);
    midi_editor_draw_rect(renderer, layout->instrument_menu_rect, fill, true);
    for (int i = 0; i < layout->instrument_menu_item_count; ++i) {
        SDL_Rect item = layout->instrument_menu_item_rects[i];
        if (!midi_editor_rect_valid(&item)) {
            continue;
        }
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        bool active = engine_instrument_preset_clamp(preset) == selected;
        if (active) {
            midi_editor_draw_rect(renderer, item, active_fill, true);
        }
        midi_editor_draw_rect(renderer, item, active ? theme->text_primary : theme->control_border, false);
        int text_h = ui_font_line_height(0.9f);
        ui_draw_text_clipped(renderer,
                             item.x + 8,
                             item.y + midi_editor_max_int(0, (item.h - text_h) / 2),
                             engine_instrument_preset_display_name(preset),
                             active ? theme->text_primary : theme->text_muted,
                             0.9f,
                             item.w - 12);
    }
    midi_editor_draw_rect(renderer, layout->instrument_menu_rect, theme->pane_border, false);
}

static SDL_Color midi_editor_velocity_color(float velocity) {
    if (velocity < 0.0f) velocity = 0.0f;
    if (velocity > 1.0f) velocity = 1.0f;
    static const SDL_Color stops[] = {
        {124, 72, 210, 255},  /* purple, low velocity */
        {47, 111, 230, 255},  /* blue */
        {24, 166, 126, 255},  /* green */
        {236, 205, 76, 255},  /* yellow */
        {238, 134, 50, 255},  /* orange */
        {226, 54, 62, 255},   /* red, high velocity */
    };
    const int stop_count = (int)(sizeof(stops) / sizeof(stops[0]));
    float scaled = velocity * (float)(stop_count - 1);
    int index = (int)scaled;
    if (index >= stop_count - 1) {
        return stops[stop_count - 1];
    }
    float t = scaled - (float)index;
    SDL_Color a = stops[index];
    SDL_Color b = stops[index + 1];
    SDL_Color out = {
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        255
    };
    return out;
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
    if (note->note < layout->lowest_note || note->note > layout->highest_note) {
        return false;
    }

    int row = layout->highest_note - (int)note->note;
    if (row < 0 || row >= layout->key_row_count) {
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
    int note = layout->highest_note - row;
    if (note < ENGINE_MIDI_NOTE_MIN) {
        note = ENGINE_MIDI_NOTE_MIN;
    }
    if (note > ENGINE_MIDI_NOTE_MAX) {
        note = ENGINE_MIDI_NOTE_MAX;
    }
    uint64_t frame = 0;
    if (!midi_editor_point_to_frame(layout, clip_frames, x, &frame)) {
        return false;
    }
    if (out_note) {
        *out_note = (uint8_t)note;
    }
    if (out_frame) {
        *out_frame = frame;
    }
    return true;
}

static void midi_editor_draw_note(SDL_Renderer* renderer,
                                  const MidiEditorLayout* layout,
                                  const EngineMidiNote* note,
                                  uint64_t clip_frames,
                                  SDL_Color fill,
                                  SDL_Color border,
                                  bool selected,
                                  bool hovered) {
    if (!renderer) {
        return;
    }
    SDL_Rect note_rect = {0, 0, 0, 0};
    if (!midi_editor_note_rect(layout, note, clip_frames, &note_rect)) {
        return;
    }
    midi_editor_draw_rect(renderer, note_rect, fill, true);
    midi_editor_draw_rect(renderer, note_rect, border, false);
    if (hovered && !selected) {
        SDL_Rect hover_rect = {note_rect.x - 1, note_rect.y - 1, note_rect.w + 2, note_rect.h + 2};
        midi_editor_draw_rect(renderer, hover_rect, border, false);
    }
    if (selected) {
        SDL_Rect outer = {note_rect.x - 2, note_rect.y - 2, note_rect.w + 4, note_rect.h + 4};
        SDL_Rect inner = {note_rect.x + 2, note_rect.y + 2, note_rect.w - 4, note_rect.h - 4};
        midi_editor_draw_rect(renderer, outer, border, false);
        if (inner.w > 2 && inner.h > 2) {
            midi_editor_draw_rect(renderer, inner, border, false);
        }
    }
}

static SDL_Rect midi_editor_marquee_rect(const AppState* state) {
    SDL_Rect rect = {0, 0, 0, 0};
    if (!state) {
        return rect;
    }
    rect.x = state->midi_editor_ui.marquee_start_x;
    rect.y = state->midi_editor_ui.marquee_start_y;
    rect.w = state->midi_editor_ui.marquee_current_x - state->midi_editor_ui.marquee_start_x;
    rect.h = state->midi_editor_ui.marquee_current_y - state->midi_editor_ui.marquee_start_y;
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

static void midi_editor_draw_marquee(SDL_Renderer* renderer,
                                     const AppState* state,
                                     const DawThemePalette* theme) {
    if (!renderer || !state || !theme || !state->midi_editor_ui.marquee_active) {
        return;
    }
    SDL_Rect rect = midi_editor_marquee_rect(state);
    if (rect.w < 2 || rect.h < 2) {
        return;
    }
    SDL_Color border = theme->accent_primary;
    SDL_Color inner = midi_editor_color_mix(theme->accent_primary, theme->text_primary, 2, 1);
    midi_editor_draw_rect(renderer, rect, border, false);
    SDL_Rect inset = {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2};
    midi_editor_draw_rect(renderer, inset, inner, false);
}

static void midi_editor_draw_timing_grid(SDL_Renderer* renderer,
                                         const AppState* state,
                                         const MidiEditorLayout* layout,
                                         const EngineClip* clip,
                                         const DawThemePalette* theme) {
    if (!renderer || !state || !layout || !clip || !theme ||
        !midi_editor_rect_valid(&layout->grid_rect)) {
        return;
    }
    int sample_rate = midi_editor_sample_rate(state);
    uint64_t visible_start = layout->view_start_frame;
    uint64_t visible_span = layout->view_span_frames;
    if (visible_span == 0 || layout->view_end_frame <= visible_start ||
        layout->view_end_frame > clip->duration_frames) {
        visible_start = 0;
        visible_span = clip->duration_frames;
    }
    double duration_seconds = sample_rate > 0
        ? (double)visible_span / (double)sample_rate
        : 0.0;
    if (duration_seconds <= 0.0) {
        return;
    }
    double start_seconds = (double)(clip->timeline_start_frames + visible_start) / (double)sample_rate;
    double end_seconds = start_seconds + duration_seconds;
    double start_beat = tempo_map_seconds_to_beats(&state->tempo_map, start_seconds);
    double end_beat = tempo_map_seconds_to_beats(&state->tempo_map, end_seconds);
    double step = 4.0 / (double)midi_editor_quantize_division(state);
    if (step <= 0.0) {
        step = 0.25;
    }
    double first = floor(start_beat / step) * step;
    int guard = 0;
    for (double beat = first; beat <= end_beat + step * 0.5 && guard < 1024; beat += step, ++guard) {
        double sec = tempo_map_beats_to_seconds(&state->tempo_map, beat);
        if (sec < start_seconds - 0.000001 || sec > end_seconds + 0.000001) {
            continue;
        }
        double ratio = (sec - start_seconds) / duration_seconds;
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        int x = layout->grid_rect.x + (int)(ratio * (double)layout->grid_rect.w + 0.5);
        double whole_beat = floor(beat + 0.5);
        bool major = fabs(beat - whole_beat) < step * 0.25;
        SDL_Color line = major ? theme->grid_major : theme->grid_minor;
        SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
        int y0 = layout->time_ruler_rect.h > 0 ? layout->time_ruler_rect.y : layout->grid_rect.y;
        SDL_RenderDrawLine(renderer, x, y0, x, layout->grid_rect.y + layout->grid_rect.h);
    }
}

static void midi_editor_draw_playhead(SDL_Renderer* renderer,
                                      const AppState* state,
                                      const MidiEditorLayout* layout,
                                      const EngineClip* clip,
                                      const DawThemePalette* theme) {
    if (!renderer || !state || !layout || !clip || !theme || !state->engine ||
        !midi_editor_rect_valid(&layout->grid_rect)) {
        return;
    }
    uint64_t clip_start = clip->timeline_start_frames;
    uint64_t visible_start = layout->view_start_frame;
    uint64_t visible_end = layout->view_end_frame;
    if (visible_end <= visible_start || visible_end > clip->duration_frames) {
        visible_start = 0;
        visible_end = clip->duration_frames;
    }
    uint64_t absolute_start = clip_start + visible_start;
    uint64_t absolute_end = clip_start + visible_end;
    uint64_t playhead = engine_get_transport_frame(state->engine);
    if (playhead < absolute_start || playhead > absolute_end || absolute_end <= absolute_start) {
        return;
    }
    double ratio = (double)(playhead - absolute_start) / (double)(absolute_end - absolute_start);
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    int x = layout->grid_rect.x + (int)(ratio * (double)layout->grid_rect.w + 0.5);
    SDL_Color line = theme->accent_primary;
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    int y0 = layout->time_ruler_rect.h > 0 ? layout->time_ruler_rect.y : layout->grid_rect.y;
    SDL_RenderDrawLine(renderer, x, y0, x, layout->grid_rect.y + layout->grid_rect.h);
    if (layout->time_ruler_rect.h > 0) {
        SDL_Rect cap = {x - 3, layout->time_ruler_rect.y + 2, 6, layout->time_ruler_rect.h - 4};
        midi_editor_draw_rect(renderer, cap, line, true);
    }
}

void midi_editor_render(SDL_Renderer* renderer, const AppState* state, const MidiEditorLayout* layout) {
    if (!renderer || !state || !layout || !midi_editor_rect_valid(&layout->panel_rect)) {
        return;
    }

    MidiEditorSelection selection = {0};
    DawThemePalette theme = {0};
    if (!daw_shared_theme_resolve_palette(&theme)) {
        theme.inspector_fill = (SDL_Color){28, 28, 36, 255};
        theme.pane_border = (SDL_Color){200, 200, 210, 255};
        theme.text_primary = (SDL_Color){220, 220, 230, 255};
        theme.text_muted = (SDL_Color){180, 184, 198, 255};
        theme.control_fill = (SDL_Color){34, 34, 44, 255};
        theme.control_border = (SDL_Color){92, 96, 110, 255};
        theme.slider_track = (SDL_Color){38, 38, 48, 255};
        theme.grid_minor = (SDL_Color){48, 50, 62, 255};
        theme.grid_major = (SDL_Color){74, 78, 96, 255};
        theme.accent_primary = (SDL_Color){120, 160, 220, 255};
    }

    SDL_Color panel_bg = theme.inspector_fill;
    SDL_Color header_bg = midi_editor_color_mix(theme.control_fill, theme.inspector_fill, 2, 1);
    SDL_Color piano_bg = midi_editor_color_mix(theme.control_fill, theme.inspector_fill, 1, 1);
    SDL_Color lane_a = midi_editor_color_mix(theme.inspector_fill, theme.slider_track, 3, 1);
    SDL_Color lane_b = midi_editor_color_mix(theme.inspector_fill, theme.slider_track, 5, 2);
    SDL_Color ruler_bg = midi_editor_color_mix(theme.control_fill, theme.inspector_fill, 2, 1);
    SDL_Color selected_note_border = theme.text_primary;

    midi_editor_draw_rect(renderer, layout->panel_rect, panel_bg, true);
    midi_editor_draw_rect(renderer, layout->panel_rect, theme.pane_border, false);
    midi_editor_draw_rect(renderer, layout->header_rect, header_bg, true);
    midi_editor_draw_rect(renderer, layout->header_rect, theme.pane_border, false);
    midi_editor_draw_rect(renderer, layout->piano_rect, piano_bg, true);
    midi_editor_draw_rect(renderer, layout->time_ruler_rect, ruler_bg, true);
    midi_editor_draw_rect(renderer, layout->grid_rect, lane_a, true);

    int title_h = ui_font_line_height(1.0f);
    int title_y = layout->title_rect.y + (layout->title_rect.h - title_h) / 2;
    if (title_h <= 0) {
        title_y = layout->title_rect.y + 7;
    }
    ui_draw_text_clipped(renderer,
                         layout->title_rect.x + 6,
                         title_y,
                         "MIDI Editor",
                         theme.text_primary,
                         1.0f,
                         layout->title_rect.w - 8);

    if (!midi_editor_get_selection(state, &selection)) {
        midi_editor_draw_button(renderer,
                                layout->instrument_button_rect,
                                "Instrument: Pure Sine",
                                false,
                                &theme);
        midi_editor_draw_button(renderer, layout->instrument_panel_button_rect, "Edit", false, &theme);
        midi_editor_draw_button(renderer,
                                layout->test_button_rect,
                                state->midi_editor_ui.qwerty_test_enabled ? "Test On" : "Test",
                                state->midi_editor_ui.qwerty_test_enabled,
                                &theme);
        midi_editor_draw_button(renderer, layout->quantize_button_rect, midi_editor_quantize_label(state), false, &theme);
        midi_editor_draw_button(renderer, layout->quantize_down_button_rect, "Q-", false, &theme);
        midi_editor_draw_button(renderer, layout->quantize_up_button_rect, "Q+", false, &theme);
        midi_editor_draw_button(renderer, layout->octave_down_button_rect, "Oct-", false, &theme);
        midi_editor_draw_button(renderer, layout->octave_up_button_rect, "Oct+", false, &theme);
        midi_editor_draw_button(renderer, layout->velocity_down_button_rect, "Vel-", false, &theme);
        midi_editor_draw_button(renderer, layout->velocity_up_button_rect, "Vel+", false, &theme);
        ui_draw_text_clipped(renderer,
                             layout->summary_rect.x,
                             title_y,
                             "No MIDI region",
                             theme.text_muted,
                             0.9f,
                             layout->summary_rect.w);
        return;
    }

    char instrument_label[64];
    snprintf(instrument_label,
             sizeof(instrument_label),
             "Instrument: %s",
             engine_instrument_preset_display_name(engine_clip_midi_instrument_preset(selection.clip)));
    midi_editor_draw_button(renderer,
                            layout->instrument_button_rect,
                            instrument_label,
                            state->midi_editor_ui.instrument_menu_open,
                            &theme);
    midi_editor_draw_button(renderer, layout->instrument_panel_button_rect, "Edit", false, &theme);
    midi_editor_draw_button(renderer,
                            layout->test_button_rect,
                            state->midi_editor_ui.qwerty_test_enabled ? "Test On" : "Test",
                            state->midi_editor_ui.qwerty_test_enabled,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->quantize_button_rect,
                            midi_editor_quantize_label(state),
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->quantize_down_button_rect,
                            "Q-",
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->quantize_up_button_rect,
                            "Q+",
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->octave_down_button_rect,
                            "Oct-",
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->octave_up_button_rect,
                            "Oct+",
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->velocity_down_button_rect,
                            "Vel-",
                            false,
                            &theme);
    midi_editor_draw_button(renderer,
                            layout->velocity_up_button_rect,
                            "Vel+",
                            false,
                            &theme);
    int sample_rate = midi_editor_sample_rate(state);
    double start_seconds = (double)selection.clip->timeline_start_frames / (double)sample_rate;
    double duration_seconds = (double)selection.clip->duration_frames / (double)sample_rate;
    int note_count = engine_clip_midi_note_count(selection.clip);
    int selected_count = midi_editor_selected_note_count(state, &selection, note_count);
    char summary[224];
    snprintf(summary,
             sizeof(summary),
             "%s  Track %d  Start %.2fs  Length %.2fs  Notes %d  Sel %d  %s  Oct %+d  Vel %.0f%%  %s",
             selection.clip->name[0] ? selection.clip->name : "MIDI Region",
             selection.track_index + 1,
             start_seconds,
             duration_seconds,
             note_count,
             selected_count,
             midi_editor_quantize_label(state),
             state->midi_editor_ui.qwerty_octave_offset,
             (state->midi_editor_ui.default_velocity > 0.0f
                  ? state->midi_editor_ui.default_velocity
                  : 0.80f) * 100.0f,
             state->midi_editor_ui.qwerty_record_armed ? "QWERTY REC" :
             (state->midi_editor_ui.qwerty_test_enabled ? "QWERTY TEST" : "R to arm"));
    ui_draw_text_clipped(renderer,
                         layout->summary_rect.x,
                         title_y,
                         summary,
                         theme.text_muted,
                         0.9f,
                         layout->summary_rect.w);

    for (int i = 0; i < layout->key_row_count; ++i) {
        SDL_Rect key = layout->key_label_rects[i];
        SDL_Rect lane = layout->key_lane_rects[i];
        int note = layout->highest_note - i;
        bool black_key = midi_editor_note_is_sharp(note);
        bool c_row = midi_editor_note_is_c(note);
        bool active_key = midi_editor_note_active(state, note);
        SDL_Color natural_lane = (i % 2 == 0) ? lane_a : lane_b;
        SDL_Color sharp_lane = midi_editor_color_mix(theme.control_fill, theme.inspector_fill, 2, 5);
        SDL_Color c_lane = midi_editor_color_mix(theme.accent_primary, natural_lane, 1, 9);
        SDL_Color lane_fill = black_key ? sharp_lane : (c_row ? c_lane : natural_lane);
        midi_editor_draw_rect(renderer, lane, lane_fill, true);
        midi_editor_draw_rect(renderer,
                              key,
                              active_key ? midi_editor_color_mix(theme.accent_primary, piano_bg, 3, 1) :
                                           (black_key ? theme.control_border :
                                            (c_row ? midi_editor_color_mix(theme.accent_primary, piano_bg, 1, 8) : piano_bg)),
                              true);
        midi_editor_draw_rect(renderer, key, active_key ? theme.text_primary : theme.pane_border, false);
        if (active_key) {
            midi_editor_draw_rect(renderer, lane, midi_editor_color_mix(theme.accent_primary, lane_fill, 1, 4), true);
            midi_editor_draw_rect(renderer, lane, theme.accent_primary, false);
        }
        if (key.h >= 8 && key.w > 12) {
            char pitch[8];
            snprintf(pitch, sizeof(pitch), "%s%d", midi_editor_pitch_label(note), (note / 12) - 1);
            ui_draw_text_clipped(renderer,
                                 key.x + 5,
                                 key.y + midi_editor_max_int(0, (key.h - ui_font_line_height(0.8f)) / 2),
                                 pitch,
                                 active_key || black_key ? theme.text_primary : theme.text_muted,
                                 0.8f,
                                 key.w - 8);
        }
    }

    if (midi_editor_rect_valid(&layout->grid_rect)) {
        midi_editor_draw_timing_grid(renderer, state, layout, selection.clip, &theme);
        if (midi_editor_rect_valid(&layout->time_ruler_rect)) {
            midi_editor_draw_rect(renderer, layout->time_ruler_rect, theme.pane_border, false);
        }
        for (int i = 0; i < layout->key_row_count; ++i) {
            SDL_Rect lane = layout->key_lane_rects[i];
            int note = layout->highest_note - i;
            SDL_Color line = midi_editor_note_is_c(note) ? theme.grid_major : theme.grid_minor;
            SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
            SDL_RenderDrawLine(renderer, layout->grid_rect.x, lane.y, layout->grid_rect.x + layout->grid_rect.w, lane.y);
        }
        midi_editor_draw_rect(renderer, layout->grid_rect, theme.pane_border, false);
        midi_editor_draw_playhead(renderer, state, layout, selection.clip, &theme);
    }

    const EngineMidiNote* notes = engine_clip_midi_notes(selection.clip);
    uint64_t clip_frames = selection.clip->duration_frames > 0 ? selection.clip->duration_frames : 1u;
    bool selection_identity_matches = midi_editor_selection_matches_ui(state, &selection);
    for (int i = 0; notes && i < note_count; ++i) {
        bool selected = selection_identity_matches &&
                        i < ENGINE_MIDI_NOTE_CAP &&
                        (state->midi_editor_ui.selected_note_indices[i] ||
                         state->midi_editor_ui.selected_note_index == i);
        bool marquee_preview = selection_identity_matches &&
                               state->midi_editor_ui.marquee_active &&
                               i < ENGINE_MIDI_NOTE_CAP &&
                               state->midi_editor_ui.marquee_preview_note_indices[i];
        bool hovered = state->midi_editor_ui.hover_note_valid &&
                       state->midi_editor_ui.hover_track_index == selection.track_index &&
                       state->midi_editor_ui.hover_clip_index == selection.clip_index &&
                       state->midi_editor_ui.hover_clip_creation_index == selection.clip->creation_index &&
                       state->midi_editor_ui.hover_note_index == i;
        SDL_Color velocity_fill = midi_editor_velocity_color(notes[i].velocity);
        SDL_Color velocity_border = hovered ? theme.text_primary :
            midi_editor_color_mix(velocity_fill, theme.text_primary, 3, 1);
        if (selected) {
            velocity_border = selected_note_border;
        } else if (marquee_preview) {
            velocity_border = theme.accent_primary;
        }
        SDL_Color fill = velocity_fill;
        if (selected) {
            fill = midi_editor_color_mix(velocity_fill, theme.text_primary, 3, 1);
        } else if (marquee_preview) {
            fill = midi_editor_color_mix(velocity_fill, theme.accent_primary, 2, 1);
        } else if (hovered) {
            fill = midi_editor_color_mix(velocity_fill, theme.text_primary, 5, 2);
        }
        midi_editor_draw_note(renderer,
                              layout,
                              &notes[i],
                              clip_frames,
                              fill,
                              velocity_border,
                              selected || marquee_preview,
                              hovered);
    }
    midi_editor_draw_marquee(renderer, state, &theme);

    if (midi_editor_rect_valid(&layout->footer_rect)) {
        char footer[128];
        snprintf(footer,
                 sizeof(footer),
                 "Visible C3-B4  Q quantize, Q-/Q+ grid, [ ] octave, -/+ velocity  View %llu-%llu / %llu frames",
                 (unsigned long long)layout->view_start_frame,
                 (unsigned long long)layout->view_end_frame,
                 (unsigned long long)clip_frames);
        ui_draw_text_clipped(renderer,
                             layout->footer_rect.x,
                             layout->footer_rect.y,
                             footer,
                             theme.text_muted,
                             0.9f,
                             layout->footer_rect.w);
    }
    midi_editor_draw_instrument_menu(renderer, state, layout, &selection, &theme);
}
