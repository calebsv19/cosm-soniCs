#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
// Visual geometry is independent of input targets and product value mapping.
typedef struct DawSliderChrome { SDL_Rect rail, marker, hit; } DawSliderChrome;
DawSliderChrome daw_slider_chrome(SDL_Rect track,int position_x);
bool daw_slider_hit(SDL_Rect track,int x,int y);
void daw_slider_draw(SDL_Renderer*,SDL_Rect track,int position_x,SDL_Color rail,SDL_Color marker);
void daw_control_frame(SDL_Renderer*,const SDL_Rect*,SDL_Color fill,SDL_Color border);
// Draws measured captions within shared rounded insets, centered or leading aligned.
void daw_control_text(SDL_Renderer*, SDL_Rect, const char*, SDL_Color, float, bool);
