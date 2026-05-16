#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "audio/media_clip.h"
#include "ui/timeline_waveform.h"

// Renders a cached waveform for a source-frame view window into a target rect.
bool waveform_render_view(SDL_Renderer* renderer,
                          WaveformCache* cache,
                          const AudioMediaClip* clip,
                          const char* path,
                          const SDL_Rect* rect,
                          uint64_t view_start_frame,
                          uint64_t view_frame_count,
                          SDL_Color color);

// Renders a transient waveform directly from sample memory without caching.
bool waveform_render_samples_view(SDL_Renderer* renderer,
                                  const AudioMediaClip* clip,
                                  const SDL_Rect* rect,
                                  uint64_t view_start_frame,
                                  uint64_t view_frame_count,
                                  SDL_Color color);
