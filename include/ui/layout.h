#pragma once

#include "app_state.h"
#include "ui/resize.h"

#include <SDL2/SDL.h>

void ui_init_panes(AppState* state);
void ui_apply_shared_theme(AppState* state);
void ui_layout_panes(AppState* state, int width, int height);
// Ensures pane layout is synchronized to window size and returns true if geometry changed.
bool ui_ensure_layout(AppState* state, SDL_Window* window, SDL_Renderer* renderer);
// Returns header height for panes with title rendering enabled.
int ui_layout_pane_header_height(const Pane* pane);
// Returns pane content bounds after removing dynamic title/header region.
SDL_Rect ui_layout_pane_content_rect(const Pane* pane);
void ui_render_panes(SDL_Renderer* renderer, const AppState* state);
void ui_render_overlays(SDL_Renderer* renderer, AppState* state);
void ui_render_controls(SDL_Renderer* renderer, AppState* state);
bool ui_layout_handle_pointer(AppState* state, Uint32 prev_buttons, Uint32 curr_buttons, int mouse_x, int mouse_y);
void ui_layout_update_zones(AppState* state);
const Pane* ui_layout_get_pane(const AppState* state, int index);
void ui_layout_handle_hover(AppState* state, int mouse_x, int mouse_y);
// Validates pane and key control geometry invariants for debug/runtime smoke usage.
bool ui_layout_debug_validate(const AppState* state);
