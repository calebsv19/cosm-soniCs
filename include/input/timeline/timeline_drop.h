#pragma once

#include <stdbool.h>

struct AppState;
struct InputManager;

void timeline_drop_update_hint(struct AppState* state);
void timeline_drop_handle_library_drag(struct InputManager* manager,
                                       struct AppState* state,
                                       bool was_down,
                                       bool is_down);
