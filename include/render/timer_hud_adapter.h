#pragma once

#include "sdl_app_framework.h"

// timer_hud_register_backend wires the shared TimerHUD backend to the DAW renderer.
void timer_hud_register_backend(void);
// timer_hud_bind_context stores the active render context for TimerHUD draws.
void timer_hud_bind_context(const AppContext* ctx);
