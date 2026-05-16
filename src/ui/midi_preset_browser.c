#include "ui/midi_preset_browser.h"

#include "ui/font.h"

#include <stdio.h>
#include <string.h>

static int preset_browser_max_int(int a, int b) {
    return a > b ? a : b;
}

static int preset_browser_min_int(int a, int b) {
    return a < b ? a : b;
}

static bool preset_browser_rect_valid(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static SDL_Color preset_browser_color_mix(SDL_Color a, SDL_Color b, int a_weight, int b_weight) {
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

static void preset_browser_draw_rect(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color, bool fill) {
    if (!renderer || !preset_browser_rect_valid(&rect)) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (fill) {
        SDL_RenderFillRect(renderer, &rect);
    } else {
        SDL_RenderDrawRect(renderer, &rect);
    }
}

static bool preset_browser_category_has_presets(EngineInstrumentPresetCategoryId category) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        if (engine_instrument_preset_category((EngineInstrumentPresetId)i) == category) {
            return true;
        }
    }
    return false;
}

static bool preset_browser_category_valid(EngineInstrumentPresetCategoryId category) {
    return category >= 0 && category < engine_instrument_preset_category_count();
}

static int preset_browser_build_rows(MidiPresetBrowserRow* rows,
                                     int row_capacity,
                                     EngineInstrumentPresetCategoryId expanded_category) {
    if (!rows || row_capacity <= 0) {
        return 0;
    }
    static const EngineInstrumentPresetCategoryId category_order[] = {
        ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC,
        ENGINE_INSTRUMENT_PRESET_CATEGORY_BASS,
        ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD,
        ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS,
        ENGINE_INSTRUMENT_PRESET_CATEGORY_PADS,
        ENGINE_INSTRUMENT_PRESET_CATEGORY_PLUCK
    };
    int count = 0;
    int category_count = engine_instrument_preset_category_count();
    for (int c = 0; c < (int)(sizeof(category_order) / sizeof(category_order[0])); ++c) {
        EngineInstrumentPresetCategoryId category = category_order[c];
        if (category < 0 || category >= category_count ||
            !preset_browser_category_has_presets(category)) {
            continue;
        }
        bool expanded = category == expanded_category;
        if (count >= row_capacity) {
            return count;
        }
        int row_index = count;
        rows[count++] = (MidiPresetBrowserRow){
            .type = MIDI_PRESET_BROWSER_ROW_CATEGORY,
            .category = category,
            .preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE,
            .absolute_row = row_index,
            .expanded = expanded
        };
        if (!expanded) {
            continue;
        }
        for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
            EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
            if (engine_instrument_preset_category(preset) != category) {
                continue;
            }
            if (count >= row_capacity) {
                return count;
            }
            row_index = count;
            rows[count++] = (MidiPresetBrowserRow){
                .type = MIDI_PRESET_BROWSER_ROW_PRESET,
                .category = category,
                .preset = preset,
                .absolute_row = row_index,
                .expanded = false
            };
        }
    }
    return count;
}

int midi_preset_browser_clamp_scroll(int requested_scroll_row,
                                     int visible_capacity,
                                     EngineInstrumentPresetCategoryId expanded_category) {
    MidiPresetBrowserRow rows[MIDI_PRESET_BROWSER_ROW_CAPACITY];
    if (!preset_browser_category_valid(expanded_category)) {
        expanded_category = ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
    }
    int total = preset_browser_build_rows(rows, MIDI_PRESET_BROWSER_ROW_CAPACITY, expanded_category);
    if (visible_capacity < 1) {
        visible_capacity = 1;
    }
    int max_scroll = total - visible_capacity;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (requested_scroll_row < 0) {
        return 0;
    }
    if (requested_scroll_row > max_scroll) {
        return max_scroll;
    }
    return requested_scroll_row;
}

void midi_preset_browser_compute_layout(SDL_Rect button_rect,
                                        int menu_bottom,
                                        int requested_scroll_row,
                                        EngineInstrumentPresetCategoryId expanded_category,
                                        MidiPresetBrowserLayout* out_layout) {
    if (!out_layout) {
        return;
    }
    memset(out_layout, 0, sizeof(*out_layout));
    if (!preset_browser_rect_valid(&button_rect)) {
        return;
    }
    if (!preset_browser_category_valid(expanded_category)) {
        expanded_category = ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT;
    }

    MidiPresetBrowserRow all_rows[MIDI_PRESET_BROWSER_ROW_CAPACITY];
    int total_rows = preset_browser_build_rows(all_rows, MIDI_PRESET_BROWSER_ROW_CAPACITY, expanded_category);
    int row_h = preset_browser_max_int(24, ui_font_line_height(0.9f) + 10);
    int max_visible_rows = 8;
    int available_h = menu_bottom - (button_rect.y + button_rect.h + 4);
    int visible_capacity = row_h > 0 ? available_h / row_h : 0;
    if (visible_capacity > max_visible_rows) {
        visible_capacity = max_visible_rows;
    }
    if (visible_capacity > total_rows) {
        visible_capacity = total_rows;
    }
    if (visible_capacity < 0) {
        visible_capacity = 0;
    }
    int scroll_row = midi_preset_browser_clamp_scroll(requested_scroll_row, visible_capacity, expanded_category);

    out_layout->total_row_count = total_rows;
    out_layout->first_visible_row = scroll_row;
    out_layout->visible_capacity = visible_capacity;
    out_layout->row_height = row_h;
    out_layout->expanded_category = expanded_category;
    out_layout->has_scrollbar = visible_capacity > 0 && total_rows > visible_capacity;
    out_layout->menu_rect = (SDL_Rect){
        button_rect.x,
        button_rect.y + button_rect.h + 4,
        button_rect.w,
        row_h * visible_capacity
    };

    int visible_index = 0;
    for (int i = scroll_row; i < total_rows && visible_index < visible_capacity; ++i) {
        MidiPresetBrowserRow row = all_rows[i];
        row.absolute_row = i;
        row.rect = (SDL_Rect){
            out_layout->menu_rect.x,
            out_layout->menu_rect.y + row_h * visible_index,
            out_layout->menu_rect.w,
            row_h
        };
        out_layout->rows[visible_index++] = row;
    }
    out_layout->row_count = visible_index;
}

int midi_preset_browser_scroll_delta(const MidiPresetBrowserLayout* layout, int current_scroll, int wheel_delta) {
    if (!layout || wheel_delta == 0 || layout->visible_capacity <= 0) {
        return current_scroll;
    }
    int next = current_scroll;
    if (wheel_delta > 0) {
        next -= wheel_delta;
    } else {
        next += -wheel_delta;
    }
    return midi_preset_browser_clamp_scroll(next, layout->visible_capacity, layout->expanded_category);
}

bool midi_preset_browser_preset_at(const MidiPresetBrowserLayout* layout,
                                   int x,
                                   int y,
                                   EngineInstrumentPresetId* out_preset) {
    if (!layout) {
        return false;
    }
    SDL_Point point = {x, y};
    for (int i = 0; i < layout->row_count; ++i) {
        const MidiPresetBrowserRow* row = &layout->rows[i];
        if (row->type == MIDI_PRESET_BROWSER_ROW_PRESET &&
            SDL_PointInRect(&point, &row->rect)) {
            if (out_preset) {
                *out_preset = engine_instrument_preset_clamp(row->preset);
            }
            return true;
        }
    }
    return false;
}

bool midi_preset_browser_category_at(const MidiPresetBrowserLayout* layout,
                                     int x,
                                     int y,
                                     EngineInstrumentPresetCategoryId* out_category) {
    if (!layout) {
        return false;
    }
    SDL_Point point = {x, y};
    for (int i = 0; i < layout->row_count; ++i) {
        const MidiPresetBrowserRow* row = &layout->rows[i];
        if (row->type == MIDI_PRESET_BROWSER_ROW_CATEGORY &&
            SDL_PointInRect(&point, &row->rect)) {
            if (out_category) {
                *out_category = row->category;
            }
            return true;
        }
    }
    return false;
}

SDL_Rect midi_preset_browser_rect_for_preset(const MidiPresetBrowserLayout* layout,
                                             EngineInstrumentPresetId preset) {
    SDL_Rect empty = {0};
    if (!layout) {
        return empty;
    }
    preset = engine_instrument_preset_clamp(preset);
    for (int i = 0; i < layout->row_count; ++i) {
        if (layout->rows[i].type == MIDI_PRESET_BROWSER_ROW_PRESET &&
            engine_instrument_preset_clamp(layout->rows[i].preset) == preset) {
            return layout->rows[i].rect;
        }
    }
    return empty;
}

void midi_preset_browser_draw(SDL_Renderer* renderer,
                              const MidiPresetBrowserLayout* layout,
                              EngineInstrumentPresetId active_preset,
                              const DawThemePalette* theme) {
    if (!renderer || !layout || !theme || !preset_browser_rect_valid(&layout->menu_rect)) {
        return;
    }
    SDL_Color fill = preset_browser_color_mix(theme->control_fill, theme->inspector_fill, 3, 1);
    SDL_Color active_fill = preset_browser_color_mix(theme->accent_primary, theme->control_fill, 1, 2);
    SDL_Color header_fill = preset_browser_color_mix(theme->control_fill, theme->pane_border, 4, 1);
    preset_browser_draw_rect(renderer, layout->menu_rect, fill, true);
    for (int i = 0; i < layout->row_count; ++i) {
        const MidiPresetBrowserRow* row = &layout->rows[i];
        if (!preset_browser_rect_valid(&row->rect)) {
            continue;
        }
        if (row->type == MIDI_PRESET_BROWSER_ROW_CATEGORY) {
            preset_browser_draw_rect(renderer, row->rect, header_fill, true);
            int text_h = ui_font_line_height(0.75f);
            char label[80];
            snprintf(label,
                     sizeof(label),
                     "%s %s",
                     row->expanded ? "- " : "+ ",
                     engine_instrument_preset_category_display_name(row->category));
            ui_draw_text_clipped(renderer,
                                 row->rect.x + 8,
                                 row->rect.y + preset_browser_max_int(0, (row->rect.h - text_h) / 2),
                                 label,
                                 theme->text_muted,
                                 0.75f,
                                 row->rect.w - 14);
            continue;
        }
        if (row->type != MIDI_PRESET_BROWSER_ROW_PRESET) {
            continue;
        }
        bool active = engine_instrument_preset_clamp(row->preset) ==
                      engine_instrument_preset_clamp(active_preset);
        if (active) {
            preset_browser_draw_rect(renderer, row->rect, active_fill, true);
        }
        preset_browser_draw_rect(renderer,
                                 row->rect,
                                 active ? theme->text_primary : theme->control_border,
                                 false);
        int text_h = ui_font_line_height(0.9f);
        ui_draw_text_clipped(renderer,
                             row->rect.x + 12,
                             row->rect.y + preset_browser_max_int(0, (row->rect.h - text_h) / 2),
                             engine_instrument_preset_display_name(row->preset),
                             active ? theme->text_primary : theme->text_muted,
                             0.9f,
                             row->rect.w - 18);
    }
    if (layout->has_scrollbar) {
        int bar_w = 4;
        SDL_Rect track = {
            layout->menu_rect.x + layout->menu_rect.w - bar_w - 2,
            layout->menu_rect.y + 2,
            bar_w,
            preset_browser_max_int(0, layout->menu_rect.h - 4)
        };
        preset_browser_draw_rect(renderer, track, theme->control_border, true);
        int max_scroll = layout->total_row_count - layout->visible_capacity;
        int thumb_h = layout->total_row_count > 0
            ? preset_browser_max_int(12, (track.h * layout->visible_capacity) / layout->total_row_count)
            : track.h;
        int movable = preset_browser_max_int(0, track.h - thumb_h);
        int thumb_y = track.y;
        if (max_scroll > 0) {
            thumb_y += (movable * layout->first_visible_row) / max_scroll;
        }
        SDL_Rect thumb = {track.x, thumb_y, track.w, preset_browser_min_int(thumb_h, track.h)};
        preset_browser_draw_rect(renderer, thumb, theme->text_muted, true);
    }
    preset_browser_draw_rect(renderer, layout->menu_rect, theme->pane_border, false);
}
