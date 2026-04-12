#ifndef UI_EFFECTS_PANEL_PREVIEW_EQ_CURVE_H
#define UI_EFFECTS_PANEL_PREVIEW_EQ_CURVE_H

#include <SDL2/SDL.h>

#include "ui/effects_panel_preview.h"

void effects_slot_preview_render_curve(SDL_Renderer* renderer,
                                       const FxSlotUIState* slot,
                                       const SDL_Rect* rect,
                                       SDL_Color label_color,
                                       const char* title);

void effects_slot_preview_render_eq(SDL_Renderer* renderer,
                                    const FxSlotUIState* slot,
                                    const SDL_Rect* rect,
                                    SDL_Color label_color,
                                    float sample_rate,
                                    const char* title);

#endif
