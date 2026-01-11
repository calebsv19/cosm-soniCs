#include "ui/font.h"

#include <SDL2/SDL_ttf.h>
#include "vk_renderer.h"
#include <math.h>
#include <string.h>

typedef struct {
    int size;
    TTF_Font* font;
} FontCacheEntry;

static char g_font_path[256] = "include/fonts/Montserrat/Montserrat-Regular.ttf";
static int g_base_point_size = 14;
static FontCacheEntry g_cache[12];
static int g_cache_count = 0;

static void clear_cache(void) {
    for (int i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].font) {
            TTF_CloseFont(g_cache[i].font);
            g_cache[i].font = NULL;
        }
    }
    g_cache_count = 0;
}

bool ui_font_set(const char* path, int base_point_size) {
    if (path && path[0]) {
        strncpy(g_font_path, path, sizeof(g_font_path) - 1);
        g_font_path[sizeof(g_font_path) - 1] = '\0';
    }
    if (base_point_size > 0) {
        g_base_point_size = base_point_size;
    }
    clear_cache();
    return true;
}

void ui_font_shutdown(void) {
    clear_cache();
}

static TTF_Font* get_font_for_scale(float scale) {
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    int size = (int)lroundf((float)g_base_point_size * scale);
    if (size < 4) {
        size = 4;
    }
    for (int i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].size == size && g_cache[i].font) {
            return g_cache[i].font;
        }
    }
    if (g_cache_count >= (int)(sizeof(g_cache) / sizeof(g_cache[0]))) {
        TTF_CloseFont(g_cache[0].font);
        memmove(&g_cache[0], &g_cache[1], (size_t)(g_cache_count - 1) * sizeof(FontCacheEntry));
        g_cache_count -= 1;
    }
    TTF_Font* font = TTF_OpenFont(g_font_path, size);
    if (!font) {
        SDL_Log("Failed to load font %s @ %d: %s", g_font_path, size, TTF_GetError());
        return NULL;
    }
    g_cache[g_cache_count++] = (FontCacheEntry){.size = size, .font = font};
    return font;
}

static bool render_text(SDL_Renderer* renderer,
                        TTF_Font* font,
                        const char* text,
                        SDL_Color color,
                        VkRendererTexture* out_texture,
                        int* out_w,
                        int* out_h) {
    if (!renderer || !font || !text || !out_texture) {
        return false;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return false;
    }
    if (out_w) *out_w = surface->w;
    if (out_h) *out_h = surface->h;
    VkResult result = vk_renderer_upload_sdl_surface(renderer, surface, out_texture);
    SDL_FreeSurface(surface);
    return result == VK_SUCCESS;
}

void ui_draw_text(SDL_Renderer* renderer, int x, int y, const char* text, SDL_Color color, float scale) {
    if (!renderer || !text) {
        return;
    }
    TTF_Font* font = get_font_for_scale(scale);
    if (!font) {
        return;
    }
    int w = 0, h = 0;
    VkRendererTexture tex;
    if (!render_text(renderer, font, text, color, &tex, &w, &h)) {
        return;
    }
    SDL_Rect dst = {x, y, w, h};
    vk_renderer_draw_texture(renderer, &tex, NULL, &dst);
    vk_renderer_queue_texture_destroy(renderer, &tex);
}

int ui_measure_text_width(const char* text, float scale) {
    if (!text) {
        return 0;
    }
    TTF_Font* font = get_font_for_scale(scale);
    if (!font) {
        return 0;
    }
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) {
        return 0;
    }
    return w;
}

void ui_draw_text_clipped(SDL_Renderer* renderer,
                          int x,
                          int y,
                          const char* text,
                          SDL_Color color,
                          float scale,
                          int max_width) {
    if (!renderer || !text || max_width <= 0) {
        return;
    }
    TTF_Font* font = get_font_for_scale(scale);
    if (!font) {
        return;
    }
    int w = 0, h = 0;
    VkRendererTexture tex;
    if (!render_text(renderer, font, text, color, &tex, &w, &h)) {
        return;
    }
    int clip_w = w < max_width ? w : max_width;
    SDL_Rect src = {0, 0, clip_w, h};
    SDL_Rect dst = {x, y, clip_w, h};
    vk_renderer_draw_texture(renderer, &tex, &src, &dst);
    vk_renderer_queue_texture_destroy(renderer, &tex);
}

int ui_font_line_height(float scale) {
    TTF_Font* font = get_font_for_scale(scale);
    if (!font) {
        return 0;
    }
    return TTF_FontHeight(font);
}
