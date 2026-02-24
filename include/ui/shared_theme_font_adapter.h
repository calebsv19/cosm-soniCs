#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct DawThemePalette {
    SDL_Color menu_fill;
    SDL_Color timeline_fill;
    SDL_Color inspector_fill;
    SDL_Color library_fill;
    SDL_Color title_text;
} DawThemePalette;

bool daw_shared_theme_resolve_palette(DawThemePalette* out_palette);
SDL_Color daw_shared_theme_title_color(void);
bool daw_shared_font_resolve_ui_regular(char* out_path, size_t out_path_size, int* out_point_size);
