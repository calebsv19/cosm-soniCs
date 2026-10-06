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

// Chooses one rack spectrogram, preferring the selected card, for the single analyzer stream.
int effects_panel_spectrogram_card_index(const struct AppState* state);

// Connects the rack's visible spectrogram card to the existing worker-owned analyzer.
void effects_panel_update_spectrogram_card_target(const struct AppState* state);

// Draws the spectrogram inside its ordinary rack card, including bypass/selection status.
void effects_panel_spectrogram_card_render(SDL_Renderer* renderer, const struct AppState* state,
                                          int slot_index, const SDL_Rect* rect,
                                          SDL_Color label_color, SDL_Color dim_color);

// Shares the rack palette button geometry between rendering and pointer input.
void effects_panel_spectrogram_card_palette_rects(const SDL_Rect* rect, SDL_Rect buttons[3]);
