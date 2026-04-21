#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "app_state.h"

// Renders a cached Vulkan texture for the Levels meter history lane.
// Returns true when the cached path rendered successfully.
bool effects_meter_history_cache_render_levels(SDL_Renderer* renderer,
                                               const SDL_Rect* history_rect,
                                               const EffectsMeterHistory* history,
                                               float min_db,
                                               float max_db,
                                               SDL_Color peak_color,
                                               SDL_Color rms_color);

// Renders a cached Vulkan texture for the Correlation meter history lane.
bool effects_meter_history_cache_render_correlation(SDL_Renderer* renderer,
                                                    const SDL_Rect* history_rect,
                                                    const EffectsMeterHistory* history,
                                                    SDL_Color trace_color);

// Renders a cached Vulkan texture for the LUFS meter history lane.
bool effects_meter_history_cache_render_lufs(SDL_Renderer* renderer,
                                             const SDL_Rect* history_rect,
                                             const EffectsMeterHistory* history,
                                             int lufs_mode,
                                             float min_db,
                                             float max_db,
                                             SDL_Color trace_color);

// Renders cached Vulkan textures for Mid/Side meter history lanes.
bool effects_meter_history_cache_render_mid_side(SDL_Renderer* renderer,
                                                 const SDL_Rect* mid_history_rect,
                                                 const SDL_Rect* side_history_rect,
                                                 const EffectsMeterHistory* history,
                                                 SDL_Color mid_color,
                                                 SDL_Color side_color);

// Drops cached textures/buffers. Pass the active renderer when available.
void effects_meter_history_cache_shutdown(SDL_Renderer* renderer);

// Invalidates cache state after renderer/device resets.
void effects_meter_history_cache_invalidate(SDL_Renderer* renderer);
