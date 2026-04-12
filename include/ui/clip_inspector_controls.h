#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

#include "ui/clip_inspector.h"
#include "ui/shared_theme_font_adapter.h"

struct AppState;
struct EngineClip;

void clip_inspector_render_controls_panel(SDL_Renderer* renderer,
                                          struct AppState* state,
                                          const ClipInspectorLayout* layout,
                                          const struct EngineClip* clip,
                                          int sample_rate,
                                          uint64_t clip_frames,
                                          uint64_t total_frames,
                                          const char* display_name,
                                          const char* source_name,
                                          const DawThemePalette* theme);
