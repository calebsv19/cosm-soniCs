#pragma once

#include <SDL2/SDL.h>

void ui_draw_text(SDL_Renderer* renderer, int x, int y, const char* text, SDL_Color color, int scale);
int  ui_measure_text_width(const char* text, int scale);
void ui_draw_text_clipped(SDL_Renderer* renderer,
                          int x,
                          int y,
                          const char* text,
                          SDL_Color color,
                          int scale,
                          int max_width);
