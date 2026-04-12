#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "time/tempo.h"
#include "engine/engine.h"
#include "app_state.h"

bool timeline_view_compute_tempo_overlay_rect(const SDL_Rect* timeline_rect,
                                              int track_y,
                                              int track_height,
                                              int content_left,
                                              int content_width,
                                              SDL_Rect* out_rect);

void timeline_view_draw_tempo_overlay(SDL_Renderer* renderer,
                                      const SDL_Rect* overlay_rect,
                                      const TempoMap* tempo_map,
                                      int selected_index,
                                      float window_start_seconds,
                                      float window_end_seconds,
                                      int content_left,
                                      float pixels_per_second,
                                      bool draw_labels);

void timeline_view_draw_automation_overlay(SDL_Renderer* renderer,
                                           const SDL_Rect* rect,
                                           int track_y,
                                           int track_height,
                                           int track_spacing,
                                           int track_count,
                                           int content_left,
                                           int content_width);

void timeline_view_draw_clip_automation(SDL_Renderer* renderer,
                                        const SDL_Rect* clip_rect,
                                        const EngineClip* clip,
                                        uint64_t clip_frames,
                                        EngineAutomationTarget target,
                                        const AutomationUIState* automation_ui,
                                        int track_index,
                                        int clip_index,
                                        int sample_rate,
                                        float window_start_seconds,
                                        float window_end_seconds,
                                        int content_left,
                                        int content_right,
                                        float pixels_per_second);
