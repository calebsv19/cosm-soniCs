#pragma once

#include <SDL2/SDL.h>

#include "core_base.h"
#include "kit_viz.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct DawKitVizScalarRange {
    float min_value;
    float max_value;
} DawKitVizScalarRange;

static inline float daw_kit_viz_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float daw_kit_viz_normalize_value(float value, DawKitVizScalarRange range) {
    float denom = range.max_value - range.min_value;
    if (!(denom > 0.0f)) {
        return 0.5f;
    }
    return daw_kit_viz_clampf((value - range.min_value) / denom, 0.0f, 1.0f);
}

static inline CoreResult daw_kit_viz_error_result(CoreError code, const char* message) {
    CoreResult r = {code, message};
    return r;
}

static inline CoreResult daw_kit_viz_plot_line_segments_from_y_samples(const float* samples,
                                                                       uint32_t sample_count,
                                                                       uint32_t total_slots,
                                                                       const SDL_Rect* rect,
                                                                       DawKitVizScalarRange range,
                                                                       KitVizVecSegment* out_segments,
                                                                       size_t max_segments,
                                                                       size_t* out_segment_count,
                                                                       const char* invalid_message) {
    if (!samples || sample_count < 2 || total_slots < 2 || sample_count > total_slots ||
        !rect || rect->w <= 0 || rect->h <= 0 || !out_segments || !out_segment_count) {
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, invalid_message);
    }
    if (max_segments < (size_t)(sample_count - 1u)) {
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }
    if (!isfinite(range.min_value) || !isfinite(range.max_value)) {
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "invalid plot range");
    }

    float width = (float)(rect->w - 1);
    float height = (float)(rect->h - 1);
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (!isfinite(samples[i])) {
            return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "non-finite sample");
        }
        float t = (float)i / (float)(total_slots - 1u);
        float n = daw_kit_viz_normalize_value(samples[i], range);
        float x = (float)rect->x + t * width;
        float y = (float)rect->y + (1.0f - n) * height;
        if (i > 0) {
            out_segments[i - 1u] = (KitVizVecSegment){prev_x, prev_y, x, y};
        }
        prev_x = x;
        prev_y = y;
    }

    *out_segment_count = (size_t)(sample_count - 1u);
    return core_result_ok();
}

static inline void daw_kit_viz_render_segments_common(SDL_Renderer* renderer,
                                                      const KitVizVecSegment* segments,
                                                      size_t segment_count,
                                                      SDL_Color color) {
    if (!renderer || !segments || segment_count == 0) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (size_t i = 0; i < segment_count; ++i) {
        const KitVizVecSegment* s = &segments[i];
        SDL_RenderDrawLine(renderer,
                           (int)lroundf(s->x0),
                           (int)lroundf(s->y0),
                           (int)lroundf(s->x1),
                           (int)lroundf(s->y1));
    }
}
