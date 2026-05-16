#pragma once

#include "engine/engine.h"
#include "ui/midi_preset_browser.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect header_rect;
    SDL_Rect title_rect;
    SDL_Rect notes_button_rect;
    SDL_Rect preset_button_rect;
    SDL_Rect group_tab_rects[ENGINE_INSTRUMENT_PARAM_GROUP_COUNT];
    SDL_Rect preset_menu_rect;
    SDL_Rect preset_menu_item_rects[ENGINE_INSTRUMENT_PRESET_COUNT];
    MidiPresetBrowserLayout preset_browser;
    SDL_Rect summary_rect;
    SDL_Rect param_grid_rect;
    SDL_Rect param_widget_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_label_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_knob_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_slider_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_value_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect scope_rect;
    int preset_menu_item_count;
    int group_tab_count;
    EngineInstrumentParamGroupId active_group;
    EngineInstrumentParamId param_ids[ENGINE_INSTRUMENT_PARAM_COUNT];
    int instrument_param_count;
} MidiInstrumentPanelLayout;

bool midi_instrument_panel_should_render(const struct AppState* state);
void midi_instrument_panel_compute_layout(const struct AppState* state, MidiInstrumentPanelLayout* layout);
void midi_instrument_panel_render(SDL_Renderer* renderer,
                                  const struct AppState* state,
                                  const MidiInstrumentPanelLayout* layout);
