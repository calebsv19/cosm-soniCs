#pragma once

#include <SDL2/SDL.h>
#include <math.h>

#include "ui/effects_panel.h"

struct AppState;

#define EQ_DETAIL_SELECTOR_W 96
#define EQ_DETAIL_SELECTOR_H 14
#define EQ_DETAIL_SELECTOR_PAD 10
#define EQ_DETAIL_TOGGLE_SIZE 18
#define EQ_DETAIL_TOGGLE_PAD 10
#define EQ_DETAIL_TOGGLE_GAP 8
#define EQ_DETAIL_MIN_HZ 20.0f
#define EQ_DETAIL_MAX_HZ 20000.0f
#define EQ_DETAIL_DB_MIN -20.0f
#define EQ_DETAIL_DB_MAX 20.0f
#define EQ_DETAIL_CURVE_SAMPLES 256

void effects_panel_eq_detail_render(SDL_Renderer* renderer,
                                    const struct AppState* state,
                                    const EffectsPanelLayout* layout);

// Computes master/track selector button rectangles for the EQ detail panel.
void effects_panel_eq_detail_compute_selector_rects(const SDL_Rect* panel,
                                                    SDL_Rect* master,
                                                    SDL_Rect* track);

// Computes low/mid/high band toggle rectangles for the EQ detail panel.
void effects_panel_eq_detail_compute_toggle_rects(const SDL_Rect* panel,
                                                  SDL_Rect* low,
                                                  SDL_Rect mids[4],
                                                  SDL_Rect* high);

// Computes the drawable EQ graph rectangle within the detail panel.
SDL_Rect effects_panel_eq_detail_compute_graph_rect(const SDL_Rect* panel);

static inline float effects_eq_x_to_freq(const SDL_Rect* rect, float x) {
    float min_hz = EQ_DETAIL_MIN_HZ;
    float max_hz = EQ_DETAIL_MAX_HZ;
    float t = (x - (float)rect->x) / (float)rect->w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return min_hz * powf(max_hz / min_hz, t);
}

static inline float effects_eq_freq_to_x(const SDL_Rect* rect, float freq) {
    float min_hz = EQ_DETAIL_MIN_HZ;
    float max_hz = EQ_DETAIL_MAX_HZ;
    float t = logf(freq / min_hz) / logf(max_hz / min_hz);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (float)rect->x + t * (float)rect->w;
}

static inline float effects_eq_db_to_y(const SDL_Rect* rect, float db) {
    float t = (db - EQ_DETAIL_DB_MIN) / (EQ_DETAIL_DB_MAX - EQ_DETAIL_DB_MIN);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (float)rect->y + (1.0f - t) * (float)rect->h;
}

static inline float effects_eq_y_to_db(const SDL_Rect* rect, float y) {
    float t = (y - (float)rect->y) / (float)rect->h;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return EQ_DETAIL_DB_MAX - t * (EQ_DETAIL_DB_MAX - EQ_DETAIL_DB_MIN);
}
