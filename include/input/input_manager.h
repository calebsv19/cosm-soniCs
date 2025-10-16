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
    Uint32 last_click_ticks;
    int last_click_track;
    int last_click_clip;
    Uint32 last_header_click_ticks;
    int last_header_click_track;
    bool prev_horiz_slider_down;
    bool prev_vert_slider_down;
} InputManager;

void input_manager_init(InputManager* manager);
void input_manager_update(InputManager* manager, struct AppState* state);
void input_manager_handle_event(InputManager* manager, struct AppState* state, const SDL_Event* event);
