#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

#include "ui/clip_inspector.h"
#include "ui/shared_theme_font_adapter.h"

struct AppState;
struct EngineClip;

void clip_inspector_render_waveform_panel(SDL_Renderer* renderer,
                                          struct AppState* state,
                                          const ClipInspectorLayout* layout,
                                          const struct EngineClip* clip,
                                          uint64_t clip_frames,
                                          uint64_t fade_in_frames,
                                          uint64_t fade_out_frames,
                                          const char* source_path,
                                          const DawThemePalette* theme);
