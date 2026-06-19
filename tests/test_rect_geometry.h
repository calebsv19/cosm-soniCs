#pragma once

#include <SDL2/SDL.h>

#include <stdbool.h>

static inline bool rect_has_positive_size(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static inline bool rect_contains_rect(const SDL_Rect* outer, const SDL_Rect* inner) {
    if (!outer || !inner) {
        return false;
    }
    return inner->x >= outer->x &&
           inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w &&
           inner->y + inner->h <= outer->y + outer->h;
}

static inline bool rects_overlap_strict(const SDL_Rect* a, const SDL_Rect* b) {
    if (!rect_has_positive_size(a) || !rect_has_positive_size(b)) {
        return false;
    }
    return a->x < b->x + b->w &&
           a->x + a->w > b->x &&
           a->y < b->y + b->h &&
           a->y + a->h > b->y;
}
