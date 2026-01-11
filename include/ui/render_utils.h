#ifndef UI_RENDER_UTILS_H
#define UI_RENDER_UTILS_H

#include <SDL2/SDL.h>

static inline void ui_set_blend_mode(SDL_Renderer* renderer, SDL_BlendMode mode) {
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    (void)renderer;
    (void)mode;
#else
    SDL_SetRenderDrawBlendMode(renderer, mode);
#endif
}

static inline SDL_bool ui_clip_is_enabled(SDL_Renderer* renderer) {
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    (void)renderer;
    return SDL_FALSE;
#else
    return SDL_RenderIsClipEnabled(renderer);
#endif
}

static inline void ui_get_clip_rect(SDL_Renderer* renderer, SDL_Rect* rect) {
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    if (rect) {
        *rect = (SDL_Rect){0, 0, 0, 0};
    }
    (void)renderer;
#else
    SDL_RenderGetClipRect(renderer, rect);
#endif
}

static inline int ui_set_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    (void)renderer;
    (void)rect;
    return 0;
#else
    return SDL_RenderSetClipRect(renderer, rect);
#endif
}

#endif // UI_RENDER_UTILS_H
