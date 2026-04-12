#pragma once

#include "app_state.h"

#include <SDL2/SDL.h>

void ui_render_project_prompt_overlay(SDL_Renderer* renderer, AppState* state);
void ui_render_project_load_overlay(SDL_Renderer* renderer, AppState* state);
