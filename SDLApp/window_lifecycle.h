#pragma once
#include "sdl_app_framework.h"
void daw_window_refresh(AppContext*);
bool daw_window_event(AppContext*,const SDL_Event*);
void daw_window_probe_tick(AppContext*);
