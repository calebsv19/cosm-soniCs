#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "ui/effects_panel.h"

struct AppState;

bool effects_panel_eq_detail_handle_mouse_down(struct AppState* state,
                                               const EffectsPanelLayout* layout,
                                               const SDL_Event* event);
bool effects_panel_eq_detail_handle_mouse_up(struct AppState* state, const SDL_Event* event);
bool effects_panel_eq_detail_handle_mouse_motion(struct AppState* state,
                                                 const EffectsPanelLayout* layout,
                                                 const SDL_Event* event);
