#pragma once

#include <SDL2/SDL.h>

#include "ui/effects_panel.h"

struct AppState;

#define EQ_DETAIL_SELECTOR_W 96
#define EQ_DETAIL_SELECTOR_H 14
#define EQ_DETAIL_SELECTOR_PAD 10

void effects_panel_eq_detail_render(SDL_Renderer* renderer,
                                    const struct AppState* state,
                                    const EffectsPanelLayout* layout);
