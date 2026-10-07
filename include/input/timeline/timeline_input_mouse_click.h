#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

struct AppState;
struct InputManager;

bool timeline_input_mouse_handle_click_event(struct InputManager* manager,
                                             struct AppState* state,
                                             const SDL_Event* event);
void timeline_input_mouse_click_update(struct InputManager* manager,
                                       struct AppState* state,
                                       bool was_down,
                                       bool is_down);

// Executes accepted discrete toolbar controls through their original product owner.
bool timeline_controls_activate_at(struct AppState*, int, int);

// Activates track-header mute/solo by stable runtime identity.
void timeline_track_control_activate(struct AppState*, uint64_t);
