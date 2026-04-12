#pragma once

#include "app_state.h"
#include "sdl_app_framework.h"

void perform_bounce(AppContext* ctx, AppState* state, void (*handle_render)(AppContext* ctx));
