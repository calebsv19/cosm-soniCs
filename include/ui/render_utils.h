#ifndef UI_RENDER_UTILS_H
#define UI_RENDER_UTILS_H

#include <SDL2/SDL.h>
#include <math.h>

#include "engine/fade_curve.h"

// Returns a normalized fade gain for the requested curve at t in [0,1].
static inline float ui_fade_curve_eval(EngineFadeCurve curve, float t) {
    return engine_fade_curve_eval(curve, t);
}

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
