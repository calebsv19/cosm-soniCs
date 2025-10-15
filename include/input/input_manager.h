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
} InputManager;

void input_manager_init(InputManager* manager);
void input_manager_update(InputManager* manager, struct AppState* state);
