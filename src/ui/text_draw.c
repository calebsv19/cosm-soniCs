#include "ui/text_draw.h"

#include "kit_render_external_text.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "vk_renderer.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct DawTextFontSourceEntry {
    TTF_Font* font;
    char path[384];
    int logical_point_size;
    int loaded_point_size;
    int kerning_enabled;
} DawTextFontSourceEntry;

typedef struct DawRasterFontCacheEntry {
    TTF_Font* base_font;
    TTF_Font* raster_font;
    char path[384];
    int point_size;
    int kerning_enabled;
    uint32_t stamp;
} DawRasterFontCacheEntry;

typedef struct DawClippedTextCacheEntry {
    TTF_Font* font;
    char text[128];
    SDL_Color color;
    int width;
    int height;
    VkRendererTexture texture;
    uint32_t stamp;
    bool in_use;
} DawClippedTextCacheEntry;

static DawTextFontSourceEntry g_font_sources[32];
static DawRasterFontCacheEntry g_raster_font_cache[32];
static uint32_t g_raster_font_cache_stamp = 0;
static DawClippedTextCacheEntry g_clipped_cache[128];
static uint32_t g_clipped_cache_stamp = 0;
static SDL_Renderer* g_clipped_cache_renderer = NULL;
static bool g_clipped_cache_clear_pending = false;

static void daw_text_reset_raster_font_cache_entry(DawRasterFontCacheEntry* entry) {
    if (!entry) {
        return;
    }
    if (entry->raster_font) {
        TTF_CloseFont(entry->raster_font);
    }
    *entry = (DawRasterFontCacheEntry){0};
}

static void daw_text_clear_raster_font_cache(void) {
    size_t i = 0;
    for (i = 0; i < (sizeof(g_raster_font_cache) / sizeof(g_raster_font_cache[0])); ++i) {
        daw_text_reset_raster_font_cache_entry(&g_raster_font_cache[i]);
    }
    g_raster_font_cache_stamp = 0;
}

static void daw_text_cache_clear(SDL_Renderer* renderer) {
    size_t i = 0;
    for (i = 0; i < (sizeof(g_clipped_cache) / sizeof(g_clipped_cache[0])); ++i) {
        if (!g_clipped_cache[i].in_use) {
            continue;
        }
        if (renderer) {
            vk_renderer_texture_destroy(renderer, &g_clipped_cache[i].texture);
        }
        g_clipped_cache[i] = (DawClippedTextCacheEntry){0};
    }
    g_clipped_cache_stamp = 0;
}

static void daw_text_ensure_renderer(SDL_Renderer* renderer) {
    if (!renderer) {
        return;
    }
    g_clipped_cache_renderer = renderer;
    if (g_clipped_cache_clear_pending) {
        daw_text_cache_clear(renderer);
        kit_render_external_text_reset_renderer(renderer);
        g_clipped_cache_clear_pending = false;
    }
}

static DawClippedTextCacheEntry* daw_text_cache_find(TTF_Font* font,
                                                     const char* text,
                                                     SDL_Color color) {
    size_t i = 0;
    if (!font || !text) {
        return NULL;
    }
    for (i = 0; i < (sizeof(g_clipped_cache) / sizeof(g_clipped_cache[0])); ++i) {
        DawClippedTextCacheEntry* entry = &g_clipped_cache[i];
        if (!entry->in_use || entry->font != font) {
            continue;
        }
        if (entry->color.r != color.r || entry->color.g != color.g ||
            entry->color.b != color.b || entry->color.a != color.a) {
            continue;
        }
        if (strncmp(entry->text, text, sizeof(entry->text)) == 0) {
            return entry;
        }
    }
    return NULL;
}

static DawClippedTextCacheEntry* daw_text_cache_pick_slot(void) {
    DawClippedTextCacheEntry* oldest = NULL;
    size_t i = 0;
    for (i = 0; i < (sizeof(g_clipped_cache) / sizeof(g_clipped_cache[0])); ++i) {
        DawClippedTextCacheEntry* entry = &g_clipped_cache[i];
        if (!entry->in_use) {
            return entry;
        }
        if (!oldest || entry->stamp < oldest->stamp) {
            oldest = entry;
        }
    }
    return oldest;
}

static const DawTextFontSourceEntry* daw_text_find_font_source(TTF_Font* font) {
    size_t i = 0;
    if (!font) {
        return NULL;
    }
    for (i = 0; i < (sizeof(g_font_sources) / sizeof(g_font_sources[0])); ++i) {
        const DawTextFontSourceEntry* entry = &g_font_sources[i];
        if (entry->font == font && entry->path[0] != '\0') {
            return entry;
        }
    }
    return NULL;
}

static void daw_text_configure_font(TTF_Font* font, int kerning_enabled) {
    if (!font) {
        return;
    }
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    TTF_SetFontKerning(font, kerning_enabled ? 1 : 0);
}

static float daw_text_vulkan_raster_scale(SDL_Renderer* renderer) {
    const VkRenderer* vk = NULL;
    float logical_w = 0.0f;
    float logical_h = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float raster_scale = 1.0f;

    if (!renderer) {
        return 1.0f;
    }
    vk = (const VkRenderer*)renderer;
    logical_w = vk->draw_state.logical_size[0];
    logical_h = vk->draw_state.logical_size[1];
    if (logical_w > 0.0f) {
        scale_x = (float)vk->context.swapchain.extent.width / logical_w;
    }
    if (logical_h > 0.0f) {
        scale_y = (float)vk->context.swapchain.extent.height / logical_h;
    }
    raster_scale = (scale_x > scale_y) ? scale_x : scale_y;
    if (!isfinite(raster_scale) || raster_scale < 1.0f) {
        raster_scale = 1.0f;
    }
    if (raster_scale > 4.0f) {
        raster_scale = 4.0f;
    }
    return raster_scale;
}

static TTF_Font* daw_text_get_rasterized_font(TTF_Font* font,
                                              SDL_Renderer* renderer,
                                              float* out_raster_scale) {
    const DawTextFontSourceEntry* source = NULL;
    DawRasterFontCacheEntry* open_slot = NULL;
    DawRasterFontCacheEntry* oldest = NULL;
    float raster_scale = 1.0f;
    int requested_point_size = 0;
    size_t i = 0;

    if (out_raster_scale) {
        *out_raster_scale = 1.0f;
    }
    if (!font) {
        return NULL;
    }

    source = daw_text_find_font_source(font);
    if (!source || source->path[0] == '\0' || source->logical_point_size <= 0) {
        return font;
    }

    raster_scale = daw_text_vulkan_raster_scale(renderer);
    requested_point_size = (int)lroundf((float)source->logical_point_size * raster_scale);
    if (requested_point_size < source->logical_point_size) {
        requested_point_size = source->logical_point_size;
    }
    if (out_raster_scale) {
        *out_raster_scale = (float)requested_point_size / (float)source->logical_point_size;
    }

    if (source->loaded_point_size == requested_point_size) {
        return font;
    }

    for (i = 0; i < (sizeof(g_raster_font_cache) / sizeof(g_raster_font_cache[0])); ++i) {
        DawRasterFontCacheEntry* entry = &g_raster_font_cache[i];
        if (!entry->raster_font) {
            if (!open_slot) {
                open_slot = entry;
            }
            continue;
        }
        if (entry->base_font == font &&
            entry->point_size == requested_point_size &&
            entry->kerning_enabled == source->kerning_enabled &&
            strcmp(entry->path, source->path) == 0) {
            entry->stamp = ++g_raster_font_cache_stamp;
            return entry->raster_font;
        }
        if (!oldest || entry->stamp < oldest->stamp) {
            oldest = entry;
        }
    }

    if (!open_slot) {
        open_slot = oldest;
    }
    if (!open_slot) {
        return font;
    }
    daw_text_reset_raster_font_cache_entry(open_slot);

    open_slot->raster_font = TTF_OpenFont(source->path, requested_point_size);
    if (!open_slot->raster_font) {
        return font;
    }
    daw_text_configure_font(open_slot->raster_font, source->kerning_enabled);
    open_slot->base_font = font;
    open_slot->point_size = requested_point_size;
    open_slot->kerning_enabled = source->kerning_enabled;
    open_slot->stamp = ++g_raster_font_cache_stamp;
    strncpy(open_slot->path, source->path, sizeof(open_slot->path) - 1);
    open_slot->path[sizeof(open_slot->path) - 1] = '\0';
    return open_slot->raster_font;
}

static VkFilter daw_text_upload_filter(float raster_scale) {
    if (isfinite(raster_scale) && raster_scale > 1.0f) {
        return VK_FILTER_NEAREST;
    }
    return VK_FILTER_LINEAR;
}

static int daw_text_measure_with_ttf(TTF_Font* font,
                                     const char* text,
                                     int* out_w,
                                     int* out_h) {
    int width = 0;
    int height = 0;

    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!font || !text) {
        return 0;
    }
    if (text[0] == '\0') {
        height = TTF_FontHeight(font);
        if (height <= 0) {
            return 0;
        }
        if (out_h) *out_h = height;
        return 1;
    }
    if (TTF_SizeUTF8(font, text, &width, &height) != 0) {
        return 0;
    }
    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    return 1;
}

static int daw_text_render_local(SDL_Renderer* renderer,
                                 TTF_Font* font,
                                 const char* text,
                                 SDL_Color color,
                                 VkRendererTexture* out_texture,
                                 int* out_w,
                                 int* out_h) {
    SDL_Surface* surface = NULL;
    int width = 0;
    int height = 0;
    float raster_scale = 1.0f;
    TTF_Font* raster_font = NULL;

    if (!renderer || !font || !text || !out_texture) {
        return 0;
    }
    raster_font = daw_text_get_rasterized_font(font, renderer, &raster_scale);
    if (!raster_font) {
        raster_font = font;
    }
    surface = TTF_RenderUTF8_Blended(raster_font, text, color);
    if (!surface) {
        return 0;
    }
    if (!daw_text_measure_with_ttf(font, text, &width, &height)) {
        width = (int)lroundf((float)surface->w / raster_scale);
        height = (int)lroundf((float)surface->h / raster_scale);
    }
    if (vk_renderer_upload_sdl_surface_with_filter((VkRenderer*)renderer,
                                                   surface,
                                                   out_texture,
                                                   daw_text_upload_filter(raster_scale)) != VK_SUCCESS) {
        SDL_FreeSurface(surface);
        return 0;
    }
    SDL_FreeSurface(surface);
    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    return 1;
}

void daw_text_register_font_source(TTF_Font* font,
                                   const char* path,
                                   int logical_point_size,
                                   int loaded_point_size,
                                   int kerning_enabled) {
    size_t i = 0;
    DawTextFontSourceEntry* slot = NULL;
    for (i = 0; i < (sizeof(g_font_sources) / sizeof(g_font_sources[0])); ++i) {
        if (g_font_sources[i].font == font) {
            slot = &g_font_sources[i];
            break;
        }
        if (!slot && g_font_sources[i].font == NULL) {
            slot = &g_font_sources[i];
        }
    }
    if (slot) {
        slot->font = font;
        slot->logical_point_size = logical_point_size;
        slot->loaded_point_size = loaded_point_size;
        slot->kerning_enabled = kerning_enabled;
        if (path) {
            strncpy(slot->path, path, sizeof(slot->path) - 1);
            slot->path[sizeof(slot->path) - 1] = '\0';
        } else {
            slot->path[0] = '\0';
        }
    }
    kit_render_external_text_register_font_source(font,
                                                  path,
                                                  logical_point_size,
                                                  loaded_point_size,
                                                  kerning_enabled);
}

void daw_text_unregister_font_source(TTF_Font* font) {
    size_t i = 0;
    for (i = 0; i < (sizeof(g_font_sources) / sizeof(g_font_sources[0])); ++i) {
        if (g_font_sources[i].font == font) {
            g_font_sources[i] = (DawTextFontSourceEntry){0};
            break;
        }
    }
    for (i = 0; i < (sizeof(g_raster_font_cache) / sizeof(g_raster_font_cache[0])); ++i) {
        if (g_raster_font_cache[i].base_font == font) {
            daw_text_reset_raster_font_cache_entry(&g_raster_font_cache[i]);
        }
    }
    kit_render_external_text_unregister_font_source(font);
}

void daw_text_invalidate_cache(SDL_Renderer* renderer) {
    daw_text_clear_raster_font_cache();
    if (renderer) {
        g_clipped_cache_renderer = renderer;
    }
    if (g_clipped_cache_renderer) {
        daw_text_cache_clear(g_clipped_cache_renderer);
        kit_render_external_text_reset_renderer(g_clipped_cache_renderer);
        g_clipped_cache_clear_pending = false;
        return;
    }
    g_clipped_cache_clear_pending = true;
}

void daw_text_shutdown(void) {
    daw_text_clear_raster_font_cache();
    if (g_clipped_cache_renderer) {
        daw_text_cache_clear(g_clipped_cache_renderer);
        kit_render_external_text_reset_renderer(g_clipped_cache_renderer);
        g_clipped_cache_clear_pending = false;
        return;
    }
    g_clipped_cache_clear_pending = true;
}

int daw_text_measure_utf8(SDL_Renderer* renderer,
                          TTF_Font* font,
                          const char* text,
                          int* out_w,
                          int* out_h) {
    return kit_render_external_text_measure_utf8(renderer, font, text, out_w, out_h);
}

int daw_text_draw_utf8_at(SDL_Renderer* renderer,
                          TTF_Font* font,
                          const char* text,
                          int x,
                          int y,
                          SDL_Color color) {
    daw_text_ensure_renderer(renderer);
    return kit_render_external_text_draw_utf8_at(renderer, font, text, x, y, color);
}

int daw_text_draw_utf8_clipped(SDL_Renderer* renderer,
                               TTF_Font* font,
                               const char* text,
                               int x,
                               int y,
                               SDL_Color color,
                               int max_width) {
    DawClippedTextCacheEntry* entry = NULL;
    int clip_w = 0;
    SDL_Rect src = {0};
    SDL_Rect dst = {0};

    if (!renderer || !font || !text || max_width <= 0) {
        return 0;
    }
    daw_text_ensure_renderer(renderer);

    if (strlen(text) >= sizeof(g_clipped_cache[0].text)) {
        VkRendererTexture texture = {0};
        int width = 0;
        int height = 0;
        if (!daw_text_render_local(renderer, font, text, color, &texture, &width, &height)) {
            return 0;
        }
        clip_w = width < max_width ? width : max_width;
        src = (SDL_Rect){0, 0, clip_w, height};
        dst = (SDL_Rect){x, y, clip_w, height};
        if (width > 0) {
            src.w = (int)lroundf((float)clip_w * ((float)texture.width / (float)width));
            if (src.w < 1) {
                src.w = 1;
            }
        }
        src.h = (int)texture.height;
        vk_renderer_draw_texture((VkRenderer*)renderer, &texture, &src, &dst);
        vk_renderer_queue_texture_destroy((VkRenderer*)renderer, &texture);
        return 1;
    }

    entry = daw_text_cache_find(font, text, color);
    if (!entry) {
        entry = daw_text_cache_pick_slot();
        if (!entry) {
            return 0;
        }
        if (entry->in_use) {
            vk_renderer_texture_destroy(renderer, &entry->texture);
        }
        if (!daw_text_render_local(renderer,
                                   font,
                                   text,
                                   color,
                                   &entry->texture,
                                   &entry->width,
                                   &entry->height)) {
            *entry = (DawClippedTextCacheEntry){0};
            return 0;
        }
        entry->font = font;
        entry->color = color;
        entry->stamp = ++g_clipped_cache_stamp;
        entry->in_use = true;
        strncpy(entry->text, text, sizeof(entry->text) - 1);
        entry->text[sizeof(entry->text) - 1] = '\0';
    }

    entry->stamp = ++g_clipped_cache_stamp;
    clip_w = entry->width < max_width ? entry->width : max_width;
    src = (SDL_Rect){0, 0, clip_w, entry->height};
    dst = (SDL_Rect){x, y, clip_w, entry->height};
    if (entry->width > 0) {
        src.w = (int)lroundf((float)clip_w * ((float)entry->texture.width / (float)entry->width));
        if (src.w < 1) {
            src.w = 1;
        }
    }
    src.h = (int)entry->texture.height;
    vk_renderer_draw_texture((VkRenderer*)renderer, &entry->texture, &src, &dst);
    return 1;
}
