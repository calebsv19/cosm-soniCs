#pragma once

#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

typedef struct DawResolvedFont {
    TTF_Font* font;
    const char* path;
    int logical_point_size;
    int kerning_enabled;
} DawResolvedFont;

bool daw_font_bridge_set_active(const char* path, int base_point_size);
void daw_font_bridge_shutdown(void);
bool daw_font_bridge_acquire(float scale, DawResolvedFont* out_font);
