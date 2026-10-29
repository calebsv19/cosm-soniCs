#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;

bool timeline_input_mouse_handle_scroll(struct InputManager* manager,
                                        struct AppState* state,
                                        const SDL_Event* event);

// Handles vertical scrollbar capture independently of clip gestures.
bool timeline_scrollbar_handle_event(struct AppState* state, const SDL_Event* event);
// Updates a captured vertical scrollbar from sampled pointer state.
bool timeline_scrollbar_update(struct AppState* state, bool is_down);
