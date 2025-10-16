#pragma once

#include <SDL2/SDL.h>

struct AppState;

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect name_rect;
    SDL_Rect gain_track_rect;
    SDL_Rect gain_fill_rect;
    SDL_Rect gain_handle_rect;
} ClipInspectorLayout;

void clip_inspector_compute_layout(const struct AppState* state, ClipInspectorLayout* layout);
void clip_inspector_render(SDL_Renderer* renderer, const struct AppState* state, const ClipInspectorLayout* layout);
