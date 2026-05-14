#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

#include "engine/engine.h"
#include "ui/timeline_view_internal.h"

int timeline_midi_clip_preview_frame_to_x(const SDL_Rect* clip_rect,
                                          uint64_t frame,
                                          uint64_t visible_start_frame,
                                          uint64_t visible_frame_count);

void timeline_midi_clip_preview_render(SDL_Renderer* renderer,
                                       const EngineClip* clip,
                                       const SDL_Rect* clip_rect,
                                       uint64_t visible_start_frame,
                                       uint64_t visible_frame_count,
                                       const TimelineTheme* theme);
