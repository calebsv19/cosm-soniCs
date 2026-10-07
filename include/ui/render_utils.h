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

// Uses the native Vulkan aliases in app builds and real SDL operations in SDL builds.
static inline SDL_bool ui_clip_is_enabled(SDL_Renderer* renderer) {
    return SDL_RenderIsClipEnabled(renderer);
}

// Reads the actual enclosing clip rather than treating Vulkan clipping as absent.
static inline void ui_get_clip_rect(SDL_Renderer* renderer, SDL_Rect* rect) {
    SDL_RenderGetClipRect(renderer, rect);
}

// Applies native clipping for both the pane host and nested product painters.
static inline int ui_set_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    return SDL_RenderSetClipRect(renderer, rect);
}

// Restricts nested product drawing to its requested region and the enclosing pane clip.
static inline int ui_set_content_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    if (!rect) return -1;
    SDL_Rect clip=*rect, previous;
    if (SDL_RenderIsClipEnabled(renderer)) {
        SDL_RenderGetClipRect(renderer,&previous);
        if (!SDL_IntersectRect(rect,&previous,&clip)) clip=(SDL_Rect){0};
    }
    return SDL_RenderSetClipRect(renderer,&clip);
}

#endif // UI_RENDER_UTILS_H
