#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

bool midi_instrument_panel_input_handle_event(struct InputManager* manager,
                                              struct AppState* state,
                                              const SDL_Event* event);
void midi_instrument_panel_input_update(struct InputManager* manager,
                                        struct AppState* state,
                                        bool left_was_down,
                                        bool left_is_down);
