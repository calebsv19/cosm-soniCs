#pragma once

#include <SDL2/SDL.h>

#include "ui/effects_panel.h"

struct AppState;

// Computes toggle rectangles for vectorscope mode buttons.
void effects_panel_meter_detail_compute_toggle_rects(const SDL_Rect* detail_rect,
                                                     SDL_Rect* out_mid_side,
                                                     SDL_Rect* out_left_right);

// Computes toggle rectangles for LUFS mode buttons.
void effects_panel_meter_detail_compute_lufs_toggle_rects(const SDL_Rect* detail_rect,
                                                          SDL_Rect* out_integrated,
                                                          SDL_Rect* out_short_term,
                                                          SDL_Rect* out_momentary);

// Computes toggle rectangles for spectrogram palette buttons.
void effects_panel_meter_detail_compute_spectrogram_toggle_rects(const SDL_Rect* detail_rect,
                                                                 SDL_Rect* out_white_black,
                                                                 SDL_Rect* out_black_white,
                                                                 SDL_Rect* out_heat);

// Renders the meter detail panel for analysis-only FX slots.
void effects_panel_meter_detail_render(SDL_Renderer* renderer,
                                       const struct AppState* state,
                                       const EffectsPanelLayout* layout);
