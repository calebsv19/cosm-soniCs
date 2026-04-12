#include "ui/font.h"

#include <SDL2/SDL_ttf.h>
#include "vk_renderer.h"
#include <math.h>
#include <string.h>

typedef struct {
    int size;
    TTF_Font* font;
} FontCacheEntry;

// TextCacheEntry stores rendered text textures to avoid per-frame uploads.
typedef struct {
    char text[128];
    SDL_Color color;
    float scale;
    float raster_scale;
    int width;
    int height;
    VkRendererTexture texture;
    uint32_t stamp;
    bool in_use;
} TextCacheEntry;

static char g_font_path[256] = "include/fonts/Montserrat/Montserrat-Regular.ttf";
static int g_base_point_size = 14;
static FontCacheEntry g_cache[12];
static int g_cache_count = 0;
static TextCacheEntry g_text_cache[128];
static uint32_t g_text_cache_stamp = 0;
static SDL_Renderer* g_text_cache_renderer = NULL;
static bool g_text_cache_clear_pending = false;
static const float k_max_text_raster_scale = 4.0f;

static void clear_cache(void) {
    for (int i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].font) {
            TTF_CloseFont(g_cache[i].font);
            g_cache[i].font = NULL;
        }
    }
    g_cache_count = 0;
}

// text_cache_clear releases cached text textures and resets tracking state.
static void text_cache_clear(SDL_Renderer* renderer) {
    for (size_t i = 0; i < (sizeof(g_text_cache) / sizeof(g_text_cache[0])); ++i) {
        if (g_text_cache[i].in_use) {
            if (renderer) {
                vk_renderer_texture_destroy(renderer, &g_text_cache[i].texture);
            }
            g_text_cache[i] = (TextCacheEntry){0};
        }
    }
    g_text_cache_stamp = 0;
}

// text_cache_find returns the cached entry matching the render parameters.
static TextCacheEntry* text_cache_find(const char* text,
                                       SDL_Color color,
                                       float scale,
                                       float raster_scale) {
    if (!text) {
        return NULL;
    }
    for (size_t i = 0; i < (sizeof(g_text_cache) / sizeof(g_text_cache[0])); ++i) {
        TextCacheEntry* entry = &g_text_cache[i];
        if (!entry->in_use) {
            continue;
        }
        if (entry->color.r != color.r || entry->color.g != color.g ||
            entry->color.b != color.b || entry->color.a != color.a) {
            continue;
        }
        if (entry->scale != scale) {
            continue;
        }
        if (fabsf(entry->raster_scale - raster_scale) > 0.01f) {
            continue;
        }
        if (strncmp(entry->text, text, sizeof(entry->text)) == 0) {
            return entry;
        }
    }
    return NULL;
}

// text_cache_pick_slot chooses a cache slot, evicting the least recently used entry if needed.
static TextCacheEntry* text_cache_pick_slot(void) {
    TextCacheEntry* oldest = NULL;
    for (size_t i = 0; i < (sizeof(g_text_cache) / sizeof(g_text_cache[0])); ++i) {
        TextCacheEntry* entry = &g_text_cache[i];
        if (!entry->in_use) {
            return entry;
        }
        if (!oldest || entry->stamp < oldest->stamp) {
            oldest = entry;
        }
    }
    return oldest;
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
    if (g_text_cache_renderer) {
        text_cache_clear(g_text_cache_renderer);
        g_text_cache_clear_pending = false;
    } else {
        g_text_cache_clear_pending = true;
    }
    return true;
}

void ui_font_shutdown(void) {
    clear_cache();
    if (g_text_cache_renderer) {
        text_cache_clear(g_text_cache_renderer);
        g_text_cache_clear_pending = false;
    } else {
        g_text_cache_clear_pending = true;
    }
}

// ui_font_invalidate_cache drops cached text textures tied to the current renderer.
void ui_font_invalidate_cache(SDL_Renderer* renderer) {
    if (renderer) {
        g_text_cache_renderer = renderer;
    }
    text_cache_clear(renderer);
    g_text_cache_clear_pending = false;
}

static float daw_vk_text_raster_scale(SDL_Renderer* renderer) {
    const VkRenderer* vk = (const VkRenderer*)renderer;
    float logical_w = 0.0f;
    float logical_h = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float raster_scale = 1.0f;

    if (!renderer) {
        return 1.0f;
    }
    logical_w = vk->draw_state.logical_size[0];
    logical_h = vk->draw_state.logical_size[1];
    if (logical_w > 0.0f) {
        scale_x = (float)vk->context.swapchain.extent.width / logical_w;
    }
    if (logical_h > 0.0f) {
        scale_y = (float)vk->context.swapchain.extent.height / logical_h;
    }
    raster_scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (!isfinite(raster_scale) || raster_scale < 1.0f) {
        raster_scale = 1.0f;
    }
    if (raster_scale > k_max_text_raster_scale) {
        raster_scale = k_max_text_raster_scale;
    }
    return raster_scale;
}

static VkFilter daw_vk_text_upload_filter(float raster_scale) {
    if (raster_scale > 1.0f) {
        return VK_FILTER_NEAREST;
    }
    return VK_FILTER_LINEAR;
}

static TTF_Font* get_font_for_scale(float scale, float raster_scale) {
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    if (!isfinite(raster_scale) || raster_scale < 1.0f) {
        raster_scale = 1.0f;
    }
    if (raster_scale > k_max_text_raster_scale) {
        raster_scale = k_max_text_raster_scale;
    }
    int size = (int)lroundf((float)g_base_point_size * scale * raster_scale);
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
                        int* out_h,
                        float raster_scale) {
    if (!renderer || !font || !text || !out_texture) {
        return false;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return false;
    }
    int draw_w = surface->w;
    int draw_h = surface->h;
    if (raster_scale > 1.0f) {
        draw_w = (int)lroundf((float)surface->w / raster_scale);
        draw_h = (int)lroundf((float)surface->h / raster_scale);
        if (draw_w < 1) draw_w = 1;
        if (draw_h < 1) draw_h = 1;
    }
    if (out_w) *out_w = draw_w;
    if (out_h) *out_h = draw_h;
    VkResult result = vk_renderer_upload_sdl_surface_with_filter((VkRenderer*)renderer,
                                                                  surface,
                                                                  out_texture,
                                                                  daw_vk_text_upload_filter(raster_scale));
    SDL_FreeSurface(surface);
    return result == VK_SUCCESS;
}

void ui_draw_text(SDL_Renderer* renderer, int x, int y, const char* text, SDL_Color color, float scale) {
    float raster_scale = 1.0f;
    if (!renderer || !text) {
        return;
    }
    g_text_cache_renderer = renderer;
    if (g_text_cache_clear_pending) {
        text_cache_clear(renderer);
        g_text_cache_clear_pending = false;
    }
    raster_scale = daw_vk_text_raster_scale(renderer);
    TTF_Font* font = get_font_for_scale(scale, raster_scale);
    if (!font) {
        return;
    }
    if (strlen(text) >= sizeof(g_text_cache[0].text)) {
        int w = 0, h = 0;
        VkRendererTexture tex;
        if (!render_text(renderer, font, text, color, &tex, &w, &h, raster_scale)) {
            return;
        }
        SDL_Rect dst = {x, y, w, h};
        vk_renderer_draw_texture(renderer, &tex, NULL, &dst);
        vk_renderer_queue_texture_destroy(renderer, &tex);
        return;
    }

    TextCacheEntry* entry = text_cache_find(text, color, scale, raster_scale);
    if (!entry) {
        entry = text_cache_pick_slot();
        if (!entry) {
            return;
        }
        if (entry->in_use) {
            vk_renderer_texture_destroy(renderer, &entry->texture);
        }
        VkRendererTexture tex;
        int w = 0, h = 0;
        if (!render_text(renderer, font, text, color, &tex, &w, &h, raster_scale)) {
            return;
        }
        *entry = (TextCacheEntry){
            .color = color,
            .scale = scale,
            .raster_scale = raster_scale,
            .width = w,
            .height = h,
            .texture = tex,
            .stamp = ++g_text_cache_stamp,
            .in_use = true
        };
        strncpy(entry->text, text, sizeof(entry->text) - 1);
        entry->text[sizeof(entry->text) - 1] = '\0';
    }
    entry->stamp = ++g_text_cache_stamp;
    SDL_Rect dst = {x, y, entry->width, entry->height};
    vk_renderer_draw_texture(renderer, &entry->texture, NULL, &dst);
}

int ui_measure_text_width(const char* text, float scale) {
    if (!text) {
        return 0;
    }
    TTF_Font* font = get_font_for_scale(scale, 1.0f);
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
    float raster_scale = 1.0f;
    if (!renderer || !text || max_width <= 0) {
        return;
    }
    g_text_cache_renderer = renderer;
    if (g_text_cache_clear_pending) {
        text_cache_clear(renderer);
        g_text_cache_clear_pending = false;
    }
    raster_scale = daw_vk_text_raster_scale(renderer);
    TTF_Font* font = get_font_for_scale(scale, raster_scale);
    if (!font) {
        return;
    }
    if (strlen(text) >= sizeof(g_text_cache[0].text)) {
        int w = 0, h = 0;
        VkRendererTexture tex;
        if (!render_text(renderer, font, text, color, &tex, &w, &h, raster_scale)) {
            return;
        }
        int clip_w = w < max_width ? w : max_width;
        SDL_Rect src = {0, 0, clip_w, h};
        SDL_Rect dst = {x, y, clip_w, h};
        if (w > 0) {
            src.w = (int)lroundf((float)clip_w * ((float)tex.width / (float)w));
            if (src.w < 1) src.w = 1;
        }
        src.h = (int)tex.height;
        vk_renderer_draw_texture(renderer, &tex, &src, &dst);
        vk_renderer_queue_texture_destroy(renderer, &tex);
        return;
    }

    TextCacheEntry* entry = text_cache_find(text, color, scale, raster_scale);
    if (!entry) {
        entry = text_cache_pick_slot();
        if (!entry) {
            return;
        }
        if (entry->in_use) {
            vk_renderer_texture_destroy(renderer, &entry->texture);
        }
        VkRendererTexture tex;
        int w = 0, h = 0;
        if (!render_text(renderer, font, text, color, &tex, &w, &h, raster_scale)) {
            return;
        }
        *entry = (TextCacheEntry){
            .color = color,
            .scale = scale,
            .raster_scale = raster_scale,
            .width = w,
            .height = h,
            .texture = tex,
            .stamp = ++g_text_cache_stamp,
            .in_use = true
        };
        strncpy(entry->text, text, sizeof(entry->text) - 1);
        entry->text[sizeof(entry->text) - 1] = '\0';
    }
    entry->stamp = ++g_text_cache_stamp;
    int clip_w = entry->width < max_width ? entry->width : max_width;
    SDL_Rect src = {0, 0, clip_w, entry->height};
    SDL_Rect dst = {x, y, clip_w, entry->height};
    if (entry->width > 0) {
        src.w = (int)lroundf((float)clip_w * ((float)entry->texture.width / (float)entry->width));
        if (src.w < 1) src.w = 1;
    }
    src.h = (int)entry->texture.height;
    vk_renderer_draw_texture(renderer, &entry->texture, &src, &dst);
}

int ui_font_line_height(float scale) {
    TTF_Font* font = get_font_for_scale(scale, 1.0f);
    if (!font) {
        return 0;
    }
    return TTF_FontHeight(font);
}
