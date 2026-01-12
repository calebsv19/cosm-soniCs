#pragma once

#include <SDL2/SDL.h>

#include "ui/effects_panel.h"

struct AppState;

// Computes toggle rectangles for vectorscope mode buttons.
void effects_panel_meter_detail_compute_toggle_rects(const SDL_Rect* detail_rect,
                                                     SDL_Rect* out_mid_side,
                                                     SDL_Rect* out_left_right);

// Renders the meter detail panel for analysis-only FX slots.
void effects_panel_meter_detail_render(SDL_Renderer* renderer,
                                       const struct AppState* state,
                                       const EffectsPanelLayout* layout);
