#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect play_rect;
    SDL_Rect stop_rect;
    SDL_Rect grid_rect;
    SDL_Rect time_label_rect;
    SDL_Rect seek_track_rect;
    SDL_Rect seek_handle_rect;
    SDL_Rect horiz_track_rect;
    SDL_Rect horiz_handle_rect;
    SDL_Rect vert_track_rect;
    SDL_Rect vert_handle_rect;
    SDL_Rect fit_width_rect;
    SDL_Rect fit_height_rect;
    bool play_hovered;
    bool stop_hovered;
    bool grid_hovered;
    bool seek_hovered;
    bool horiz_hovered;
    bool vert_hovered;
    bool adjusting_horizontal;
    bool adjusting_vertical;
    bool adjusting_seek;
    bool fit_width_hovered;
    bool fit_height_hovered;
} TransportUI;

void transport_ui_init(TransportUI* ui);
void transport_ui_layout(TransportUI* ui, const SDL_Rect* container);
void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y);
void transport_ui_sync(TransportUI* ui, const struct AppState* state);
void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, const struct AppState* state, bool is_playing);
bool transport_ui_click_play(const TransportUI* ui, int mouse_x, int mouse_y);
bool transport_ui_click_stop(const TransportUI* ui, int mouse_x, int mouse_y);
