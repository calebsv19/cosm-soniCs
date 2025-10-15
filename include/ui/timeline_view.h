#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

#include "engine.h"

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, const Engine* engine);
