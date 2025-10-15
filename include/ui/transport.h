#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect play_rect;
    SDL_Rect stop_rect;
    bool play_hovered;
    bool stop_hovered;
} TransportUI;

void transport_ui_init(TransportUI* ui);
void transport_ui_layout(TransportUI* ui, const SDL_Rect* container);
void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y);
void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, bool is_playing);
bool transport_ui_click_play(const TransportUI* ui, int mouse_x, int mouse_y);
bool transport_ui_click_stop(const TransportUI* ui, int mouse_x, int mouse_y);
