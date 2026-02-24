#ifndef UI_EFFECTS_PANEL_WIDGETS_H
#define UI_EFFECTS_PANEL_WIDGETS_H

#include <SDL2/SDL.h>
#include "effects/effects_manager.h"

// effects_slot_draw_gr_meter renders a compact gain reduction meter.
void effects_slot_draw_gr_meter(SDL_Renderer* renderer,
                                const SDL_Rect* rect,
                                float gr_db);

// effects_slot_draw_slider renders a slider track, fill, and handle.
void effects_slot_draw_slider(SDL_Renderer* renderer,
                              const SDL_Rect* rect,
                              float t);

// effects_slot_draw_remove_button renders the remove button for a slot.
void effects_slot_draw_remove_button(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     bool highlighted);

// effects_slot_draw_enable_toggle renders the enabled/disabled toggle.
void effects_slot_draw_enable_toggle(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     bool enabled,
                                     bool highlighted);

// effects_slot_draw_mode_toggle renders the native/beat mode selector.
void effects_slot_draw_mode_toggle(SDL_Renderer* renderer,
                                   const SDL_Rect* rect,
                                   FxParamMode mode);

#endif
