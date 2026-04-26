#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

void daw_text_register_font_source(TTF_Font* font,
                                   const char* path,
                                   int logical_point_size,
                                   int loaded_point_size,
                                   int kerning_enabled);
void daw_text_unregister_font_source(TTF_Font* font);
void daw_text_invalidate_cache(SDL_Renderer* renderer);
void daw_text_shutdown(void);
int daw_text_measure_utf8(SDL_Renderer* renderer,
                          TTF_Font* font,
                          const char* text,
                          int* out_w,
                          int* out_h);
int daw_text_draw_utf8_at(SDL_Renderer* renderer,
                          TTF_Font* font,
                          const char* text,
                          int x,
                          int y,
                          SDL_Color color);
int daw_text_draw_utf8_clipped(SDL_Renderer* renderer,
                               TTF_Font* font,
                               const char* text,
                               int x,
                               int y,
                               SDL_Color color,
                               int max_width);
