#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

// Aggregates layout and hover state for the transport bar UI.
typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect load_rect;
    SDL_Rect save_rect;
    SDL_Rect play_rect;
    SDL_Rect stop_rect;
    SDL_Rect grid_rect;
    SDL_Rect time_label_rect;
    SDL_Rect bpm_rect;
    SDL_Rect ts_rect;
    SDL_Rect beat_toggle_rect;
    SDL_Rect seek_track_rect;
    SDL_Rect seek_handle_rect;
    SDL_Rect window_track_rect;
    SDL_Rect window_handle_rect;
    SDL_Rect horiz_track_rect;
    SDL_Rect horiz_handle_rect;
    SDL_Rect vert_track_rect;
    SDL_Rect vert_handle_rect;
    SDL_Rect fit_width_rect;
    SDL_Rect fit_height_rect;
    bool load_hovered;
    bool save_hovered;
    bool play_hovered;
    bool stop_hovered;
    bool grid_hovered;
    bool bpm_hovered;
    bool ts_hovered;
    bool beat_toggle_hovered;
    bool seek_hovered;
    bool window_hovered;
    bool horiz_hovered;
    bool vert_hovered;
    bool adjusting_horizontal;
    bool adjusting_vertical;
    bool adjusting_seek;
    bool adjusting_window;
    bool fit_width_hovered;
    bool fit_height_hovered;
} TransportUI;

// Initialize transport UI rectangles and hover flags.
void transport_ui_init(TransportUI* ui);
// Compute transport bar layout from the parent container rect.
void transport_ui_layout(TransportUI* ui, const SDL_Rect* container);
// Update hover flags based on the mouse position.
void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y);
// Sync transport UI handles to engine and timeline state.
void transport_ui_sync(TransportUI* ui, const struct AppState* state);
// Render the transport bar controls and labels.
void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, const struct AppState* state, bool is_playing);
// Return true when the play button is clicked.
bool transport_ui_click_play(const TransportUI* ui, int mouse_x, int mouse_y);
// Return true when the stop button is clicked.
bool transport_ui_click_stop(const TransportUI* ui, int mouse_x, int mouse_y);
