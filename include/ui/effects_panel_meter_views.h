#pragma once

#include <SDL2/SDL.h>

#include "engine/engine.h"

typedef struct EffectsMeterHistory EffectsMeterHistory;

// Renders a correlation meter view for the given snapshot.
void effects_meter_render_correlation(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const EngineFxMeterSnapshot* snapshot,
                                      const EffectsMeterHistory* history,
                                      SDL_Color label_color,
                                      SDL_Color dim_color);

// Renders a mid/side level view for the given snapshot.
void effects_meter_render_mid_side(SDL_Renderer* renderer,
                                   const SDL_Rect* rect,
                                   const EngineFxMeterSnapshot* snapshot,
                                   const EffectsMeterHistory* history,
                                   SDL_Color label_color,
                                   SDL_Color dim_color);

// Renders a vectorscope view for the given snapshot.
void effects_meter_render_vectorscope(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const EngineFxMeterSnapshot* snapshot,
                                      const EffectsMeterHistory* history,
                                      int scope_mode,
                                      SDL_Color label_color,
                                      SDL_Color dim_color);
