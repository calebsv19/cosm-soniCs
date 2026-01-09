#pragma once

#include <stdbool.h>

struct AppState;
struct InputManager;

void timeline_input_mouse_drag_end(struct AppState* state);
void timeline_input_mouse_drag_update(struct InputManager* manager,
                                      struct AppState* state,
                                      bool was_down,
                                      bool is_down);
