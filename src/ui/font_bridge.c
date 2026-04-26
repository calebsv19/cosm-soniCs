#include "ui/font_bridge.h"

#include "ui/text_draw.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <string.h>

typedef struct DawFontCacheEntry {
    int logical_point_size;
    TTF_Font* font;
} DawFontCacheEntry;

static char g_font_path[256] = "include/fonts/Montserrat/Montserrat-Regular.ttf";
static int g_base_point_size = 14;
static DawFontCacheEntry g_font_cache[12];
static int g_font_cache_count = 0;

static void daw_font_bridge_reset_slot(DawFontCacheEntry* entry) {
    if (!entry || !entry->font) {
        return;
    }
    daw_text_unregister_font_source(entry->font);
    TTF_CloseFont(entry->font);
    entry->font = NULL;
    entry->logical_point_size = 0;
}

static void daw_font_bridge_clear_cache(void) {
    int i = 0;
    for (i = 0; i < g_font_cache_count; ++i) {
        daw_font_bridge_reset_slot(&g_font_cache[i]);
    }
    g_font_cache_count = 0;
}

static void daw_font_bridge_copy_path(const char* path) {
    if (!path || !path[0]) {
        return;
    }
    strncpy(g_font_path, path, sizeof(g_font_path) - 1);
    g_font_path[sizeof(g_font_path) - 1] = '\0';
}

static int daw_font_bridge_resolve_point_size(float scale) {
    int point_size = 0;

    if (!isfinite(scale) || scale <= 0.0f) {
        scale = 1.0f;
    }
    point_size = (int)lroundf((float)g_base_point_size * scale);
    if (point_size < 4) {
        point_size = 4;
    }
    return point_size;
}

static TTF_Font* daw_font_bridge_open_font(int logical_point_size) {
    TTF_Font* font = NULL;

    font = TTF_OpenFont(g_font_path, logical_point_size);
    if (!font) {
        SDL_Log("Failed to load font %s @ %d: %s", g_font_path, logical_point_size, TTF_GetError());
        return NULL;
    }
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    TTF_SetFontKerning(font, 1);
    daw_text_register_font_source(font,
                                  g_font_path,
                                  logical_point_size,
                                  logical_point_size,
                                  1);
    return font;
}

bool daw_font_bridge_set_active(const char* path, int base_point_size) {
    if (base_point_size > 0) {
        g_base_point_size = base_point_size;
    }
    daw_font_bridge_copy_path(path);
    daw_font_bridge_clear_cache();
    return true;
}

void daw_font_bridge_shutdown(void) {
    daw_font_bridge_clear_cache();
}

bool daw_font_bridge_acquire(float scale, DawResolvedFont* out_font) {
    int logical_point_size = 0;
    int i = 0;
    TTF_Font* font = NULL;

    if (!out_font) {
        return false;
    }
    memset(out_font, 0, sizeof(*out_font));

    logical_point_size = daw_font_bridge_resolve_point_size(scale);
    for (i = 0; i < g_font_cache_count; ++i) {
        if (g_font_cache[i].font && g_font_cache[i].logical_point_size == logical_point_size) {
            out_font->font = g_font_cache[i].font;
            out_font->path = g_font_path;
            out_font->logical_point_size = logical_point_size;
            out_font->kerning_enabled = 1;
            return true;
        }
    }

    if (g_font_cache_count >= (int)(sizeof(g_font_cache) / sizeof(g_font_cache[0]))) {
        daw_font_bridge_reset_slot(&g_font_cache[0]);
        memmove(&g_font_cache[0],
                &g_font_cache[1],
                (size_t)(g_font_cache_count - 1) * sizeof(g_font_cache[0]));
        g_font_cache_count -= 1;
    }

    font = daw_font_bridge_open_font(logical_point_size);
    if (!font) {
        return false;
    }
    g_font_cache[g_font_cache_count].logical_point_size = logical_point_size;
    g_font_cache[g_font_cache_count].font = font;
    g_font_cache_count += 1;

    out_font->font = font;
    out_font->path = g_font_path;
    out_font->logical_point_size = logical_point_size;
    out_font->kerning_enabled = 1;
    return true;
}
