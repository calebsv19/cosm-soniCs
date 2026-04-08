#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

struct AppState;

typedef struct InputManager {
    Uint32 previous_buttons;
    Uint32 current_buttons;
    bool previous_space;
    bool previous_l;
    bool previous_delete;
    bool previous_enter;
    bool previous_c;
    bool previous_b;
    bool previous_folder_b;
    bool previous_s;
    bool previous_f7;
    bool previous_f8;
    bool previous_f9;
    bool previous_theme_next;
    bool previous_theme_prev;
    bool previous_font_zoom_in;
    bool previous_font_zoom_out;
    bool previous_font_zoom_reset;
    Uint32 last_click_ticks;
    int last_click_track;
    int last_click_clip;
    Uint32 last_header_click_ticks;
    int last_header_click_track;
    bool prev_horiz_slider_down;
    bool prev_vert_slider_down;
    bool prev_window_slider_down;
    Uint32 last_library_click_ticks;
    int last_library_click_index;
} InputManager;

void input_manager_init(InputManager* manager);
void input_manager_update(InputManager* manager, struct AppState* state);
void input_manager_handle_event(InputManager* manager, struct AppState* state, const SDL_Event* event);
// Clears meter histories after a forced seek when the debug toggle is enabled.
void input_manager_reset_meter_history_on_seek(struct AppState* state);
