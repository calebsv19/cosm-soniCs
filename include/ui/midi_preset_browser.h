#pragma once

#include "engine/instrument.h"
#include "ui/shared_theme_font_adapter.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
    MIDI_PRESET_BROWSER_ROW_EMPTY = 0,
    MIDI_PRESET_BROWSER_ROW_CATEGORY,
    MIDI_PRESET_BROWSER_ROW_PRESET
} MidiPresetBrowserRowType;

typedef struct {
    SDL_Rect rect;
    MidiPresetBrowserRowType type;
    EngineInstrumentPresetCategoryId category;
    EngineInstrumentPresetId preset;
    int absolute_row;
    bool expanded;
} MidiPresetBrowserRow;

#define MIDI_PRESET_BROWSER_ROW_CAPACITY \
    (ENGINE_INSTRUMENT_PRESET_COUNT + ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT)

typedef struct {
    SDL_Rect menu_rect;
    MidiPresetBrowserRow rows[MIDI_PRESET_BROWSER_ROW_CAPACITY];
    int row_count;
    int total_row_count;
    int first_visible_row;
    int visible_capacity;
    int row_height;
    EngineInstrumentPresetCategoryId expanded_category;
    bool has_scrollbar;
} MidiPresetBrowserLayout;

void midi_preset_browser_compute_layout(SDL_Rect button_rect,
                                        int menu_bottom,
                                        int requested_scroll_row,
                                        EngineInstrumentPresetCategoryId expanded_category,
                                        MidiPresetBrowserLayout* out_layout);
int midi_preset_browser_clamp_scroll(int requested_scroll_row,
                                     int visible_capacity,
                                     EngineInstrumentPresetCategoryId expanded_category);
int midi_preset_browser_scroll_delta(const MidiPresetBrowserLayout* layout, int current_scroll, int wheel_delta);
bool midi_preset_browser_preset_at(const MidiPresetBrowserLayout* layout,
                                   int x,
                                   int y,
                                   EngineInstrumentPresetId* out_preset);
bool midi_preset_browser_category_at(const MidiPresetBrowserLayout* layout,
                                     int x,
                                     int y,
                                     EngineInstrumentPresetCategoryId* out_category);
SDL_Rect midi_preset_browser_rect_for_preset(const MidiPresetBrowserLayout* layout,
                                             EngineInstrumentPresetId preset);
void midi_preset_browser_draw(SDL_Renderer* renderer,
                              const MidiPresetBrowserLayout* layout,
                              EngineInstrumentPresetId active_preset,
                              const DawThemePalette* theme);
