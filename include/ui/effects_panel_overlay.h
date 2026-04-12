#pragma once

#include "app_state.h"
#include "ui/effects_panel.h"

#include <SDL2/SDL.h>

void effects_panel_compute_overlay_layout(const AppState* state,
                                          const EffectsPanelState* panel,
                                          const SDL_Rect* mixer_rect,
                                          int content_x,
                                          int content_w,
                                          const SDL_Rect* dropdown_button_rect,
                                          EffectsPanelLayout* layout);

void effects_panel_render_overlay(SDL_Renderer* renderer,
                                  const AppState* state,
                                  const EffectsPanelLayout* layout);
