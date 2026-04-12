#ifndef UI_EFFECTS_PANEL_PREVIEW_TIME_DOMAIN_H
#define UI_EFFECTS_PANEL_PREVIEW_TIME_DOMAIN_H

#include <SDL2/SDL.h>

#include "ui/effects_panel_preview.h"

void effects_slot_preview_render_lfo(SDL_Renderer* renderer,
                                     const FxSlotUIState* slot,
                                     const SDL_Rect* rect,
                                     SDL_Color label_color,
                                     const char* title);

void effects_slot_preview_render_reverb(SDL_Renderer* renderer,
                                        const FxSlotUIState* slot,
                                        const SDL_Rect* rect,
                                        SDL_Color label_color,
                                        const char* title);

#endif
