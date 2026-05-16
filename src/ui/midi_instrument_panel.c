#include "ui/midi_instrument_panel.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/midi_editor.h"
#include "ui/shared_theme_font_adapter.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    MIDI_INSTRUMENT_PANEL_MARGIN = 10,
    MIDI_INSTRUMENT_PANEL_HEADER_HEIGHT = 30,
    MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT = 58,
    MIDI_INSTRUMENT_PANEL_PARAM_HEIGHT = 76
};

static int instrument_panel_max_int(int a, int b) {
    return a > b ? a : b;
}

static int instrument_panel_min_int(int a, int b) {
    return a < b ? a : b;
}

static bool instrument_panel_rect_valid(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static float instrument_panel_clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static SDL_Rect instrument_panel_inset_rect(SDL_Rect rect, int dx, int dy) {
    rect.x += dx;
    rect.y += dy;
    rect.w -= dx * 2;
    rect.h -= dy * 2;
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

static SDL_Color instrument_panel_color_mix(SDL_Color a, SDL_Color b, int a_weight, int b_weight) {
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

static void instrument_panel_draw_rect(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color, bool fill) {
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

static void instrument_panel_draw_disc(SDL_Renderer* renderer,
                                       int cx,
                                       int cy,
                                       int radius,
                                       SDL_Color color,
                                       bool fill) {
    if (!renderer || radius <= 0) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int r2 = radius * radius;
    if (fill) {
        for (int y = -radius; y <= radius; ++y) {
            int x = 0;
            while (x * x + y * y <= r2) {
                ++x;
            }
            --x;
            SDL_RenderDrawLine(renderer, cx - x, cy + y, cx + x, cy + y);
        }
        return;
    }
    int last_x = cx + radius;
    int last_y = cy;
    for (int i = 1; i <= 72; ++i) {
        float angle = ((float)i / 72.0f) * 6.28318530718f;
        int x = cx + (int)(cosf(angle) * (float)radius + 0.5f);
        int y = cy + (int)(sinf(angle) * (float)radius + 0.5f);
        SDL_RenderDrawLine(renderer, last_x, last_y, x, y);
        last_x = x;
        last_y = y;
    }
}

static void instrument_panel_draw_arc(SDL_Renderer* renderer,
                                      int cx,
                                      int cy,
                                      int radius,
                                      float start_angle,
                                      float end_angle,
                                      SDL_Color color) {
    if (!renderer || radius <= 0) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int steps = 36;
    int last_x = cx + (int)(cosf(start_angle) * (float)radius + 0.5f);
    int last_y = cy + (int)(sinf(start_angle) * (float)radius + 0.5f);
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float angle = start_angle + (end_angle - start_angle) * t;
        int x = cx + (int)(cosf(angle) * (float)radius + 0.5f);
        int y = cy + (int)(sinf(angle) * (float)radius + 0.5f);
        SDL_RenderDrawLine(renderer, last_x, last_y, x, y);
        last_x = x;
        last_y = y;
    }
}

static void instrument_panel_draw_button(SDL_Renderer* renderer,
                                         SDL_Rect rect,
                                         const char* label,
                                         bool active,
                                         const DawThemePalette* theme) {
    if (!renderer || !theme || !instrument_panel_rect_valid(&rect)) {
        return;
    }
    SDL_Color fill = active
        ? instrument_panel_color_mix(theme->accent_primary, theme->control_fill, 1, 2)
        : theme->control_fill;
    SDL_Color border = active ? theme->text_primary : theme->control_border;
    instrument_panel_draw_rect(renderer, rect, fill, true);
    instrument_panel_draw_rect(renderer, rect, border, false);
    int text_h = ui_font_line_height(0.8f);
    int text_y = rect.y + instrument_panel_max_int(0, (rect.h - text_h) / 2);
    ui_draw_text_clipped(renderer,
                         rect.x + 7,
                         text_y,
                         label ? label : "",
                         active ? theme->text_primary : theme->text_muted,
                         0.8f,
                         rect.w - 12);
}

static void instrument_panel_draw_preset_menu(SDL_Renderer* renderer,
                                              const MidiInstrumentPanelLayout* layout,
                                              const AppState* state,
                                              const MidiEditorSelection* selection,
                                              const DawThemePalette* theme) {
    if (!renderer || !layout || !state || !selection || !selection->clip || !theme ||
        !instrument_panel_rect_valid(&layout->preset_menu_rect)) {
        return;
    }
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(state->engine,
                                                                                   selection->track_index,
                                                                                   selection->clip_index);
    midi_preset_browser_draw(renderer,
                             &layout->preset_browser,
                             preset,
                             theme);
}

bool midi_instrument_panel_should_render(const AppState* state) {
    return state &&
           state->midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT &&
           midi_editor_get_selection(state, NULL);
}

void midi_instrument_panel_compute_layout(const AppState* state, MidiInstrumentPanelLayout* layout) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    const Pane* pane = ui_layout_get_pane(state, 2);
    if (!pane) {
        return;
    }
    layout->panel_rect = pane->rect;
    if (!instrument_panel_rect_valid(&layout->panel_rect)) {
        return;
    }
    SDL_Rect content = instrument_panel_inset_rect(layout->panel_rect,
                                                  MIDI_INSTRUMENT_PANEL_MARGIN,
                                                  MIDI_INSTRUMENT_PANEL_MARGIN);
    if (!instrument_panel_rect_valid(&content)) {
        return;
    }

    int header_h = instrument_panel_max_int(MIDI_INSTRUMENT_PANEL_HEADER_HEIGHT, ui_font_line_height(0.95f) + 8);
    header_h = instrument_panel_min_int(header_h, content.h);
    layout->header_rect = (SDL_Rect){content.x, content.y, content.w, header_h};

    int gap = 8;
    int button_y = layout->header_rect.y + 3;
    int button_h = layout->header_rect.h - 6;
    if (button_h < 18) {
        button_y = layout->header_rect.y;
        button_h = layout->header_rect.h;
    }
    int notes_w = ui_measure_text_width("Notes", 0.8f) + 16;
    if (notes_w < 54) {
        notes_w = 54;
    }
    layout->notes_button_rect = (SDL_Rect){content.x + content.w - notes_w, button_y, notes_w, button_h};

    MidiEditorSelection selection = {0};
    EngineInstrumentPresetId preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    if (midi_editor_get_selection(state, &selection)) {
        preset = engine_clip_midi_effective_instrument_preset(state->engine,
                                                              selection.track_index,
                                                              selection.clip_index);
    }
    char preset_label[80];
    snprintf(preset_label,
             sizeof(preset_label),
             "Preset: %s",
             engine_instrument_preset_display_name(preset));
    int preset_w = ui_measure_text_width(preset_label, 0.8f) + 18;
    if (preset_w < 128) {
        preset_w = 128;
    }
    int max_preset_w = instrument_panel_min_int(240, content.w / 4);
    if (preset_w > max_preset_w) {
        preset_w = max_preset_w;
    }
    if (preset_w < 0) {
        preset_w = 0;
    }
    layout->preset_button_rect = (SDL_Rect){layout->notes_button_rect.x - gap - preset_w,
                                            button_y,
                                            preset_w,
                                            button_h};

    int title_w = ui_measure_text_width("Instrument", 1.0f) + 14;
    int max_title_w = layout->preset_button_rect.x - content.x - gap;
    if (title_w > max_title_w) {
        title_w = max_title_w;
    }
    if (title_w < 0) {
        title_w = 0;
    }
    layout->title_rect = (SDL_Rect){content.x, content.y, title_w, header_h};
    int summary_x = layout->title_rect.x + layout->title_rect.w + gap;
    int summary_w = layout->preset_button_rect.x - gap - summary_x;
    if (summary_w < 0) {
        summary_w = 0;
    }
    layout->summary_rect = (SDL_Rect){summary_x, content.y, summary_w, header_h};

    int menu_bottom = content.y + content.h;
    midi_preset_browser_compute_layout(layout->preset_button_rect,
                                       menu_bottom,
                                       state ? state->midi_editor_ui.instrument_menu_scroll_row : 0,
                                       state ? (EngineInstrumentPresetCategoryId)state->midi_editor_ui.instrument_menu_expanded_category
                                             : ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT,
                                       &layout->preset_browser);
    layout->preset_menu_rect = layout->preset_browser.menu_rect;
    layout->preset_menu_item_count = engine_instrument_preset_count();
    if (layout->preset_menu_item_count > ENGINE_INSTRUMENT_PRESET_COUNT) {
        layout->preset_menu_item_count = ENGINE_INSTRUMENT_PRESET_COUNT;
    }
    if (layout->preset_menu_item_count < 0) {
        layout->preset_menu_item_count = 0;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        layout->preset_menu_item_rects[i] =
            midi_preset_browser_rect_for_preset(&layout->preset_browser, (EngineInstrumentPresetId)i);
    }

    int group_count = engine_instrument_param_group_count();
    if (group_count > ENGINE_INSTRUMENT_PARAM_GROUP_COUNT) {
        group_count = ENGINE_INSTRUMENT_PARAM_GROUP_COUNT;
    }
    if (group_count < 0) {
        group_count = 0;
    }
    layout->group_tab_count = group_count;
    EngineInstrumentParamGroupId active_group = state
        ? state->midi_editor_ui.instrument_active_group
        : ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT;
    if (active_group < 0 || active_group >= ENGINE_INSTRUMENT_PARAM_GROUP_COUNT) {
        active_group = ENGINE_INSTRUMENT_PARAM_GROUP_OUTPUT;
    }
    layout->active_group = active_group;

    int group_y = content.y + header_h + 6;
    int group_h = instrument_panel_max_int(22, ui_font_line_height(0.8f) + 8);
    if (group_y + group_h > content.y + content.h) {
        group_h = instrument_panel_max_int(0, content.y + content.h - group_y);
    }
    int group_gap = 6;
    int group_x = content.x;
    for (int i = 0; i < group_count; ++i) {
        const char* group_name = engine_instrument_param_group_display_name((EngineInstrumentParamGroupId)i);
        int tab_w = ui_measure_text_width(group_name, 0.8f) + 18;
        if (tab_w < 56) {
            tab_w = 56;
        }
        if (group_x + tab_w > content.x + content.w) {
            tab_w = instrument_panel_max_int(0, content.x + content.w - group_x);
        }
        layout->group_tab_rects[i] = (SDL_Rect){group_x, group_y, tab_w, group_h};
        group_x += tab_w + group_gap;
    }

    int body_y = group_y + group_h + 6;
    int body_h = content.y + content.h - body_y;
    if (body_h < 0) {
        body_h = 0;
    }
    int preset_param_count = engine_instrument_preset_param_count(preset);
    int param_count = 0;
    for (int i = 0; i < preset_param_count && param_count < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_LEVEL;
        EngineInstrumentParamSpec spec = {0};
        if (!engine_instrument_preset_param_id_at(preset, i, &param) ||
            !engine_instrument_param_spec(param, &spec) ||
            spec.group != active_group) {
            continue;
        }
        layout->param_ids[param_count++] = param;
    }
    layout->instrument_param_count = param_count;

    int columns = content.w >= 700 ? 4 : 2;
    if (columns > param_count && param_count > 0) {
        columns = param_count;
    }
    if (columns < 1) {
        columns = 1;
    }
    int rows = param_count > 0 ? (param_count + columns - 1) / columns : 0;
    int widget_gap = 8;
    int min_param_h = rows > 0
        ? rows * MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT + widget_gap * (rows - 1)
        : 0;
    int param_h = body_h >= 150
        ? MIDI_INSTRUMENT_PANEL_PARAM_HEIGHT
        : instrument_panel_min_int(body_h, MIDI_INSTRUMENT_PANEL_PARAM_HEIGHT);
    if (param_h < min_param_h) {
        param_h = min_param_h;
    }
    if (param_h > body_h) {
        param_h = body_h;
    }
    if (param_h < MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT && body_h >= MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT) {
        param_h = MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT;
    }
    int scope_gap = body_h >= 130 ? 10 : 6;
    int compact_scope_min_h = 40;
    if (body_h >= MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT + scope_gap + compact_scope_min_h &&
        body_h - param_h - scope_gap < compact_scope_min_h) {
        param_h = body_h - scope_gap - compact_scope_min_h;
    }
    int scope_h = body_h - param_h - scope_gap;
    if (param_h < MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT) {
        param_h = body_h;
        scope_h = 0;
    }
    layout->param_grid_rect = (SDL_Rect){content.x, body_y, content.w, param_h};
    if (scope_h > 0) {
        layout->scope_rect = (SDL_Rect){content.x, body_y + param_h + scope_gap, content.w, scope_h};
    }

    int widget_w = columns > 0 ? (layout->param_grid_rect.w - widget_gap * (columns - 1)) / columns : 0;
    int widget_h = rows > 0 ? (layout->param_grid_rect.h - widget_gap * (rows - 1)) / rows : 0;
    if (widget_h < MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT) {
        widget_h = MIDI_INSTRUMENT_PANEL_MIN_PARAM_HEIGHT;
    }
    for (int i = 0; i < param_count; ++i) {
        int row = i / columns;
        int col = i % columns;
        SDL_Rect widget = {
            layout->param_grid_rect.x + col * (widget_w + widget_gap),
            layout->param_grid_rect.y + row * (widget_h + widget_gap),
            widget_w,
            widget_h
        };
        if (col == columns - 1) {
            widget.w = layout->param_grid_rect.x + layout->param_grid_rect.w - widget.x;
        }
        if (row == rows - 1 && widget.y + widget.h > layout->param_grid_rect.y + layout->param_grid_rect.h) {
            widget.h = layout->param_grid_rect.y + layout->param_grid_rect.h - widget.y;
        }
        if (widget.h < 0) {
            widget.h = 0;
        }
        layout->param_widget_rects[i] = widget;
        int knob_d = instrument_panel_min_int(widget.h - 22, widget.w / 3);
        knob_d = instrument_panel_min_int(knob_d, 46);
        if (knob_d < 28) {
            knob_d = instrument_panel_min_int(widget.h, widget.w);
        }
        SDL_Rect knob = {
            widget.x + 12,
            widget.y + instrument_panel_max_int(0, (widget.h - knob_d) / 2),
            knob_d,
            knob_d
        };
        layout->param_knob_rects[i] = knob;
        layout->param_slider_rects[i] = knob;
        int text_x = knob.x + knob.w + 10;
        int text_w = widget.x + widget.w - text_x - 10;
        if (text_w < 0) {
            text_w = 0;
        }
        layout->param_label_rects[i] = (SDL_Rect){text_x, widget.y + 12, text_w, 16};
        layout->param_value_rects[i] = (SDL_Rect){text_x, widget.y + widget.h - 28, text_w, 16};
    }
}

static void instrument_panel_draw_knob(SDL_Renderer* renderer,
                                       SDL_Rect rect,
                                       float t,
                                       const DawThemePalette* theme) {
    if (!renderer || !theme || !instrument_panel_rect_valid(&rect)) {
        return;
    }
    t = instrument_panel_clamp_float(t, 0.0f, 1.0f);
    int radius = instrument_panel_min_int(rect.w, rect.h) / 2;
    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2;
    int ring_radius = radius - 2;
    if (ring_radius <= 0) {
        return;
    }
    SDL_Color fill = instrument_panel_color_mix(theme->control_fill, theme->inspector_fill, 3, 1);
    SDL_Color track = instrument_panel_color_mix(theme->slider_track, theme->control_border, 2, 1);
    SDL_Color active = instrument_panel_color_mix(theme->accent_primary, theme->text_primary, 4, 1);
    instrument_panel_draw_disc(renderer, cx, cy, radius, fill, true);
    instrument_panel_draw_disc(renderer, cx, cy, radius, theme->control_border, false);

    const float start = 2.35619449f;
    const float span = 4.71238898f;
    instrument_panel_draw_arc(renderer, cx, cy, ring_radius + 3, start, start + span, track);
    instrument_panel_draw_arc(renderer, cx, cy, ring_radius + 3, start, start + span * t, active);

    float pointer_angle = start + span * t;
    int px = cx + (int)(cosf(pointer_angle) * (float)(ring_radius - 4));
    int py = cy + (int)(sinf(pointer_angle) * (float)(ring_radius - 4));
    SDL_SetRenderDrawColor(renderer, active.r, active.g, active.b, active.a);
    SDL_RenderDrawLine(renderer, cx, cy, px, py);
    instrument_panel_draw_disc(renderer, cx, cy, 2, theme->text_primary, true);
}

static float instrument_panel_preset_wave(EngineInstrumentPresetId preset, float phase, float tone) {
    const float pi = 3.14159265358979323846f;
    phase = phase - floorf(phase);
    float sine = sinf(phase * pi * 2.0f);
    float harmonic = sinf(phase * pi * 4.0f) * tone * 0.30f;
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB: {
        float saw = phase * 2.0f - 1.0f;
        float detuned = sinf((phase * (1.0f + tone * 0.08f)) * pi * 2.0f);
        float sub = sinf(phase * pi) * 0.55f;
        return sine * (1.0f - tone * 0.38f) +
               saw * tone * 0.50f +
               detuned * tone * 0.24f +
               sub * tone * 0.20f;
    }
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE: {
        float square = sine >= 0.0f ? 1.0f : -1.0f;
        return sine * (0.72f - tone * 0.28f) + square * (0.28f + tone * 0.28f);
    }
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD: {
        float saw = phase * 2.0f - 1.0f;
        return saw * (0.72f + tone * 0.22f) + sine * (0.18f - tone * 0.08f) + harmonic;
    }
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS: {
        float folded = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
        return sine * 0.70f + folded * (0.22f + tone * 0.18f);
    }
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD: {
        float detuned = sinf((phase * (1.0f + tone * 0.05f)) * pi * 2.0f);
        float sub = sinf(phase * pi) * 0.22f;
        return sine * 0.64f + detuned * (0.24f + tone * 0.12f) + sub * tone;
    }
    case ENGINE_INSTRUMENT_PRESET_PLUCK: {
        float triangle = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
        return triangle * (0.54f + tone * 0.24f) + sine * (0.30f - tone * 0.08f);
    }
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD: {
        float saw = phase * 2.0f - 1.0f;
        float square = sine >= 0.0f ? 1.0f : -1.0f;
        return saw * (0.68f + tone * 0.20f) + square * tone * 0.22f + harmonic;
    }
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS: {
        float rounded = tanhf(sine * (1.0f + tone * 1.8f));
        return rounded * 0.72f + harmonic * 0.55f;
    }
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE: {
        float sub = sinf(phase * pi) * 0.85f;
        return sub + sine * (0.18f + tone * 0.18f);
    }
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    case ENGINE_INSTRUMENT_PRESET_COUNT:
    default:
        return sine + harmonic;
    }
}

static bool instrument_panel_preset_uses_synth_envelope(EngineInstrumentPresetId preset) {
    switch (engine_instrument_preset_clamp(preset)) {
    case ENGINE_INSTRUMENT_PRESET_SYNTH_LAB:
    case ENGINE_INSTRUMENT_PRESET_SOFT_PAD:
    case ENGINE_INSTRUMENT_PRESET_PLUCK:
    case ENGINE_INSTRUMENT_PRESET_BRIGHT_LEAD:
    case ENGINE_INSTRUMENT_PRESET_WARM_KEYS:
    case ENGINE_INSTRUMENT_PRESET_SUB_DRONE:
        return true;
    case ENGINE_INSTRUMENT_PRESET_PURE_SINE:
    case ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE:
    case ENGINE_INSTRUMENT_PRESET_SAW_LEAD:
    case ENGINE_INSTRUMENT_PRESET_SIMPLE_BASS:
    case ENGINE_INSTRUMENT_PRESET_COUNT:
    default:
        return false;
    }
}

static float instrument_panel_envelope_at(float t, float attack_ms, float release_ms) {
    float attack = instrument_panel_clamp_float(attack_ms / 250.0f, 0.0f, 1.0f);
    float release = instrument_panel_clamp_float(release_ms / 500.0f, 0.0f, 1.0f);
    float attack_w = 0.02f + attack * 0.32f;
    float release_w = 0.03f + release * 0.36f;
    if (t < attack_w) {
        return attack_w > 0.0f ? t / attack_w : 1.0f;
    }
    if (t > 1.0f - release_w) {
        return release_w > 0.0f ? (1.0f - t) / release_w : 0.0f;
    }
    return 1.0f;
}

static float instrument_panel_adsr_envelope_at(float t, const EngineInstrumentParams* params) {
    if (!params) {
        return 1.0f;
    }
    float attack = 0.02f + instrument_panel_clamp_float(params->attack_ms / 250.0f, 0.0f, 1.0f) * 0.24f;
    float decay = 0.04f + instrument_panel_clamp_float(params->decay_ms / 800.0f, 0.0f, 1.0f) * 0.22f;
    float release = 0.03f + instrument_panel_clamp_float(params->release_ms / 500.0f, 0.0f, 1.0f) * 0.28f;
    float sustain = instrument_panel_clamp_float(params->sustain, 0.0f, 1.0f);
    if (t < attack) {
        return attack > 0.0f ? t / attack : 1.0f;
    }
    if (t < attack + decay) {
        float dt = decay > 0.0f ? (t - attack) / decay : 1.0f;
        return 1.0f + (sustain - 1.0f) * dt;
    }
    if (t > 1.0f - release) {
        float rt = release > 0.0f ? (1.0f - t) / release : 0.0f;
        return sustain * instrument_panel_clamp_float(rt, 0.0f, 1.0f);
    }
    return sustain;
}

static void instrument_panel_draw_preview(SDL_Renderer* renderer,
                                          SDL_Rect rect,
                                          EngineInstrumentPresetId preset,
                                          EngineInstrumentParams params,
                                          const DawThemePalette* theme) {
    if (!renderer || !theme || !instrument_panel_rect_valid(&rect)) {
        return;
    }
    instrument_panel_draw_rect(renderer,
                               rect,
                               instrument_panel_color_mix(theme->control_fill, theme->inspector_fill, 1, 3),
                               true);
    instrument_panel_draw_rect(renderer, rect, theme->control_border, false);

    SDL_Rect plot = instrument_panel_inset_rect(rect, 14, 12);
    if (!instrument_panel_rect_valid(&plot)) {
        return;
    }
    int mid_y = plot.y + plot.h / 2;
    SDL_Color guide = instrument_panel_color_mix(theme->grid_minor, theme->inspector_fill, 2, 1);
    SDL_SetRenderDrawColor(renderer, guide.r, guide.g, guide.b, guide.a);
    SDL_RenderDrawLine(renderer, plot.x, mid_y, plot.x + plot.w, mid_y);

    float level = instrument_panel_clamp_float(params.level / 1.5f, 0.0f, 1.0f);
    float tone = instrument_panel_clamp_float(params.tone, 0.0f, 1.0f);
    float cycles = 2.5f + tone * 5.5f;
    int amp_px = (int)((float)(plot.h / 2 - 6) * level + 0.5f);
    if (amp_px < 0) {
        amp_px = 0;
    }

    SDL_Color envelope_color = instrument_panel_color_mix(theme->text_muted, theme->grid_minor, 1, 2);
    SDL_SetRenderDrawColor(renderer, envelope_color.r, envelope_color.g, envelope_color.b, envelope_color.a);
    int last_top_x = plot.x;
    int last_top_y = mid_y;
    int last_bottom_y = mid_y;
    for (int i = 0; i <= plot.w; ++i) {
        float t = plot.w > 0 ? (float)i / (float)plot.w : 0.0f;
        float env = instrument_panel_preset_uses_synth_envelope(preset)
            ? instrument_panel_adsr_envelope_at(t, &params)
            : instrument_panel_envelope_at(t, params.attack_ms, params.release_ms);
        int x = plot.x + i;
        int top_y = mid_y - (int)((float)amp_px * env + 0.5f);
        int bottom_y = mid_y + (int)((float)amp_px * env + 0.5f);
        if (i > 0) {
            SDL_RenderDrawLine(renderer, last_top_x, last_top_y, x, top_y);
            SDL_RenderDrawLine(renderer, last_top_x, last_bottom_y, x, bottom_y);
        }
        last_top_x = x;
        last_top_y = top_y;
        last_bottom_y = bottom_y;
    }

    SDL_Color wave = instrument_panel_color_mix(theme->accent_primary, theme->text_primary, 3, 1);
    SDL_SetRenderDrawColor(renderer, wave.r, wave.g, wave.b, wave.a);
    int last_x = plot.x;
    int last_y = mid_y;
    for (int i = 0; i <= plot.w; ++i) {
        float t = plot.w > 0 ? (float)i / (float)plot.w : 0.0f;
        float env = instrument_panel_preset_uses_synth_envelope(preset)
            ? instrument_panel_adsr_envelope_at(t, &params)
            : instrument_panel_envelope_at(t, params.attack_ms, params.release_ms);
        float preview_phase = t * cycles;
        if (instrument_panel_preset_uses_synth_envelope(preset)) {
            preview_phase += sinf(t * params.vibrato_rate * 6.28318530718f) *
                             (params.vibrato_depth / 1200.0f);
        }
        float sample = instrument_panel_preset_wave(preset, preview_phase, tone);
        if (instrument_panel_preset_uses_synth_envelope(preset)) {
            sample += sinf(t * cycles * 3.14159265358979323846f) * params.sub_mix * 0.30f;
            sample = tanhf(sample * (1.0f + params.drive * 2.0f));
        }
        sample = instrument_panel_clamp_float(sample, -1.15f, 1.15f);
        int x = plot.x + i;
        int y = mid_y - (int)(sample * env * (float)amp_px);
        if (i > 0) {
            SDL_RenderDrawLine(renderer, last_x, last_y, x, y);
        }
        last_x = x;
        last_y = y;
    }

    if (tone > 0.05f && amp_px > 2) {
        SDL_Color accent = instrument_panel_color_mix(theme->accent_primary, theme->grid_major, 1, 1);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
        int tick_count = 8 + (int)(tone * 18.0f);
        for (int i = 1; i < tick_count; ++i) {
            int x = plot.x + (plot.w * i) / tick_count;
            int h = 3 + (int)(tone * 9.0f);
            SDL_RenderDrawLine(renderer, x, mid_y - h, x, mid_y + h);
        }
    }
}

void midi_instrument_panel_render(SDL_Renderer* renderer,
                                  const AppState* state,
                                  const MidiInstrumentPanelLayout* layout) {
    if (!renderer || !state || !layout || !instrument_panel_rect_valid(&layout->panel_rect)) {
        return;
    }
    MidiEditorSelection selection = {0};
    if (!midi_editor_get_selection(state, &selection)) {
        return;
    }

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
    SDL_Color header_bg = instrument_panel_color_mix(theme.control_fill, theme.inspector_fill, 2, 1);
    instrument_panel_draw_rect(renderer, layout->panel_rect, panel_bg, true);
    instrument_panel_draw_rect(renderer, layout->panel_rect, theme.pane_border, false);
    instrument_panel_draw_rect(renderer, layout->header_rect, header_bg, true);
    instrument_panel_draw_rect(renderer, layout->header_rect, theme.pane_border, false);

    int title_h = ui_font_line_height(1.0f);
    int title_y = layout->title_rect.y + instrument_panel_max_int(0, (layout->title_rect.h - title_h) / 2);
    ui_draw_text_clipped(renderer,
                         layout->title_rect.x + 6,
                         title_y,
                         "Instrument",
                         theme.text_primary,
                         1.0f,
                         layout->title_rect.w - 8);

    int sample_rate = state->runtime_cfg.sample_rate > 0 ? state->runtime_cfg.sample_rate : 48000;
    double duration_seconds = (double)selection.clip->duration_frames / (double)sample_rate;
    char summary[160];
    snprintf(summary,
             sizeof(summary),
             "%s  Track %d  Length %.2fs",
             selection.clip->name[0] ? selection.clip->name : "MIDI Region",
             selection.track_index + 1,
             duration_seconds);
    ui_draw_text_clipped(renderer,
                         layout->summary_rect.x,
                         title_y,
                         summary,
                         theme.text_muted,
                         0.85f,
                         layout->summary_rect.w);

    char preset_label[80];
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(state->engine,
                                                                                   selection.track_index,
                                                                                   selection.clip_index);
    bool inherits_track = engine_clip_midi_inherits_track_instrument(selection.clip);
    snprintf(preset_label,
             sizeof(preset_label),
             inherits_track ? "Preset: %s (Track)" : "Preset: %s",
             engine_instrument_preset_display_name(preset));
    instrument_panel_draw_button(renderer,
                                 layout->preset_button_rect,
                                 preset_label,
                                 state->midi_editor_ui.instrument_menu_open,
                                 &theme);
    instrument_panel_draw_button(renderer, layout->notes_button_rect, "Notes", false, &theme);

    for (int i = 0; i < layout->group_tab_count; ++i) {
        SDL_Rect tab = layout->group_tab_rects[i];
        if (!instrument_panel_rect_valid(&tab)) {
            continue;
        }
        bool active = (EngineInstrumentParamGroupId)i == layout->active_group;
        instrument_panel_draw_button(renderer,
                                     tab,
                                     engine_instrument_param_group_display_name((EngineInstrumentParamGroupId)i),
                                     active,
                                     &theme);
    }

    EngineInstrumentParams params = engine_clip_midi_effective_instrument_params(state->engine,
                                                                                 selection.track_index,
                                                                                 selection.clip_index);
    instrument_panel_draw_rect(renderer,
                               layout->param_grid_rect,
                               instrument_panel_color_mix(theme.control_fill, theme.inspector_fill, 1, 2),
                               true);
    for (int i = 0; i < layout->instrument_param_count; ++i) {
        EngineInstrumentParamSpec spec = {0};
        EngineInstrumentParamId param = layout->param_ids[i];
        if (!engine_instrument_param_spec(param, &spec)) {
            continue;
        }
        SDL_Rect widget = layout->param_widget_rects[i];
        SDL_Rect label = layout->param_label_rects[i];
        SDL_Rect knob = layout->param_knob_rects[i];
        SDL_Rect value_rect = layout->param_value_rects[i];
        if (!instrument_panel_rect_valid(&widget) || !instrument_panel_rect_valid(&knob)) {
            continue;
        }
        float value = engine_instrument_params_get(params, param);
        float denom = spec.max_value - spec.min_value;
        float t = denom > 0.0f ? (value - spec.min_value) / denom : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        instrument_panel_draw_rect(renderer, widget, theme.control_fill, true);
        instrument_panel_draw_rect(renderer, widget, theme.control_border, false);
        ui_draw_text_clipped(renderer, label.x, label.y, spec.display_name, theme.text_muted, 0.8f, label.w);
        instrument_panel_draw_knob(renderer, knob, t, &theme);

        char value_text[32];
        if (spec.unit && strcmp(spec.unit, "ms") == 0) {
            snprintf(value_text, sizeof(value_text), "%.0f ms", value);
        } else {
            snprintf(value_text, sizeof(value_text), "%.2f", value);
        }
        ui_draw_text_clipped(renderer, value_rect.x, value_rect.y, value_text, theme.text_primary, 0.8f, value_rect.w);
    }
    instrument_panel_draw_rect(renderer, layout->param_grid_rect, theme.pane_border, false);

    instrument_panel_draw_preview(renderer,
                                  layout->scope_rect,
                                  preset,
                                  params,
                                  &theme);

    if (state->midi_editor_ui.instrument_menu_open) {
        instrument_panel_draw_preset_menu(renderer, layout, state, &selection, &theme);
    }
}
