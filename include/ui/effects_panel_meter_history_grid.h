#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "time/tempo.h"

// Carries the timeline mapping needed to draw meter history time/beat guides.
typedef struct EffectsMeterHistoryGridContext {
    bool enabled;
    bool beat_mode;
    double history_end_seconds;
    double history_span_seconds;
    const TempoMap* tempo_map;
    const TimeSignatureMap* signature_map;
} EffectsMeterHistoryGridContext;

// Draws subtle vertical time/beat guides over a history rectangle.
void effects_meter_history_grid_draw(SDL_Renderer* renderer,
                                     const SDL_Rect* rect,
                                     const EffectsMeterHistoryGridContext* grid);
