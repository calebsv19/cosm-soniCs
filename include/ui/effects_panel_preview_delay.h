#ifndef UI_EFFECTS_PANEL_PREVIEW_DELAY_H
#define UI_EFFECTS_PANEL_PREVIEW_DELAY_H

#include <SDL2/SDL.h>

#include "ui/effects_panel_preview.h"

void effects_slot_preview_render_delay(SDL_Renderer* renderer,
                                       const FxSlotUIState* slot,
                                       const SDL_Rect* rect,
                                       SDL_Color label_color,
                                       const char* title);

#endif
