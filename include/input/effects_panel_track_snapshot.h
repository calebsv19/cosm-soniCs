#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "ui/effects_panel.h"

struct AppState;

bool effects_panel_track_snapshot_handle_mouse_down(struct AppState* state,
                                                    const EffectsPanelLayout* layout,
                                                    const SDL_Event* event);
bool effects_panel_track_snapshot_handle_mouse_up(struct AppState* state, const SDL_Event* event);
bool effects_panel_track_snapshot_handle_mouse_motion(struct AppState* state,
                                                      const EffectsPanelLayout* layout,
                                                      const SDL_Event* event);
