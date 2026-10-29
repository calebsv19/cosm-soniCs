#pragma once
#include "engine/automation.h"
#include <SDL.h>
#include <math.h>

// Maps an authored value onto a centered bipolar editor scale without changing playback units.
static inline float ui_automation_normalized(const EngineAutomationLane* lane, float value) {
    if (lane && lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL && lane->target == ENGINE_AUTOMATION_TARGET_VOLUME)
        value /= value < 0 ? 120.0f : 24.0f;
    return fminf(1, fmaxf(-1, value));
}
// Converts a centered editor gesture back into the lane's declared units.
static inline float ui_automation_authored(const EngineAutomationLane* lane, float normalized) {
    normalized = fminf(1, fmaxf(-1, normalized));
    return lane && lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL && lane->target == ENGINE_AUTOMATION_TARGET_VOLUME ?
        normalized * (normalized < 0 ? 120.0f : 24.0f) : normalized;
}
// Draws the actual engine-evaluated curve over a bounded visible range, including hold and smooth segments.
static inline void ui_automation_draw_curve(SDL_Renderer* renderer, const SDL_Rect* rect,
                                            const EngineAutomationLane* lane, uint64_t duration,
                                            double first_frame, double visible_frames) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0 || visible_frames <= 0) return;
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) range = 4;
    int previous_x = 0, previous_y = 0;
    bool previous_valid = false;
    for (int x = 0; x <= rect->w; ++x) {
        double at = first_frame + visible_frames * (double)x / rect->w;
        if (at < 0 || at > (double)duration) { previous_valid = false; continue; }
        float value = engine_automation_lane_eval_value(lane, (uint64_t)at, duration);
        int y = baseline - (int)llround((double)ui_automation_normalized(lane, value) * range);
        if (previous_valid) SDL_RenderDrawLine(renderer, previous_x, previous_y, rect->x + x, y);
        previous_x = rect->x + x;
        previous_y = y;
        previous_valid = true;
    }
}
