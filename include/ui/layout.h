#pragma once

#include "app_state.h"
#include "ui/resize.h"

#include <SDL2/SDL.h>

void ui_init_panes(AppState* state);
void ui_layout_panes(AppState* state, int width, int height);
void ui_ensure_layout(AppState* state, SDL_Renderer* renderer);
void ui_render_panes(SDL_Renderer* renderer, const AppState* state);
void ui_render_overlays(SDL_Renderer* renderer, const AppState* state);
void ui_render_controls(SDL_Renderer* renderer, const AppState* state);
bool ui_layout_handle_pointer(AppState* state, Uint32 prev_buttons, Uint32 curr_buttons, int mouse_x, int mouse_y);
void ui_layout_update_zones(AppState* state);
const Pane* ui_layout_get_pane(const AppState* state, int index);
void ui_layout_handle_hover(AppState* state, int mouse_x, int mouse_y);
