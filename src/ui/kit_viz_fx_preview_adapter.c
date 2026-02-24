#include "ui/kit_viz_fx_preview_adapter.h"

#include <math.h>
#include <stdlib.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float normalize_value(float value, DawKitVizPlotRange range) {
    float denom = range.max_value - range.min_value;
    if (!(denom > 0.0f)) {
        return 0.5f;
    }
    return clampf((value - range.min_value) / denom, 0.0f, 1.0f);
}

static CoreResult error_result(CoreError code, const char* message) {
    CoreResult r = {code, message};
    return r;
}

CoreResult daw_kit_viz_plot_line_from_y_samples(const float* samples,
                                                uint32_t sample_count,
                                                const SDL_Rect* rect,
                                                DawKitVizPlotRange range,
                                                KitVizVecSegment* out_segments,
                                                size_t max_segments,
                                                size_t* out_segment_count) {
    if (!samples || sample_count < 2 || !rect || rect->w <= 0 || rect->h <= 0 ||
        !out_segments || !out_segment_count) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid line plot request");
    }

    if (max_segments < (size_t)(sample_count - 1u)) {
        return error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }
    if (!isfinite(range.min_value) || !isfinite(range.max_value)) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid plot range");
    }

    float* xs = (float*)malloc((size_t)sample_count * sizeof(float));
    float* ys = (float*)malloc((size_t)sample_count * sizeof(float));
    if (!xs || !ys) {
        free(xs);
        free(ys);
        return error_result(CORE_ERR_OUT_OF_MEMORY, "allocation failed");
    }

    float width = (float)(rect->w - 1);
    float height = (float)(rect->h - 1);
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (!isfinite(samples[i])) {
            free(xs);
            free(ys);
            return error_result(CORE_ERR_INVALID_ARG, "non-finite sample");
        }
        float t = sample_count > 1 ? (float)i / (float)(sample_count - 1u) : 0.0f;
        float n = normalize_value(samples[i], range);
        xs[i] = (float)rect->x + t * width;
        ys[i] = (float)rect->y + (1.0f - n) * height;
    }

    CoreResult r = kit_viz_build_polyline_segments(xs,
                                                   ys,
                                                   sample_count,
                                                   out_segments,
                                                   max_segments,
                                                   out_segment_count);
    free(xs);
    free(ys);
    return r;
}

CoreResult daw_kit_viz_plot_envelope_from_min_max(const float* mins,
                                                  const float* maxs,
                                                  uint32_t sample_count,
                                                  const SDL_Rect* rect,
                                                  DawKitVizPlotRange range,
                                                  KitVizVecSegment* out_segments,
                                                  size_t max_segments,
                                                  size_t* out_segment_count) {
    if (!mins || !maxs || sample_count == 0 || !rect || rect->w <= 0 || rect->h <= 0 ||
        !out_segments || !out_segment_count) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid envelope plot request");
    }

    if (max_segments < (size_t)sample_count) {
        return error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }
    if (!isfinite(range.min_value) || !isfinite(range.max_value)) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid plot range");
    }

    float width = (float)(rect->w - 1);
    float height = (float)(rect->h - 1);
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (!isfinite(mins[i]) || !isfinite(maxs[i])) {
            return error_result(CORE_ERR_INVALID_ARG, "non-finite envelope sample");
        }
        float t = sample_count > 1 ? (float)i / (float)(sample_count - 1u) : 0.0f;
        float nmin = normalize_value(mins[i], range);
        float nmax = normalize_value(maxs[i], range);
        float y0 = (float)rect->y + (1.0f - nmin) * height;
        float y1 = (float)rect->y + (1.0f - nmax) * height;
        if (y0 < y1) {
            float tmp = y0;
            y0 = y1;
            y1 = tmp;
        }
        float x = (float)rect->x + t * width;
        out_segments[i] = (KitVizVecSegment){x, y0, x, y1};
    }

    *out_segment_count = (size_t)sample_count;
    return core_result_ok();
}

void daw_kit_viz_render_segments(SDL_Renderer* renderer,
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

void daw_kit_viz_draw_center_line(SDL_Renderer* renderer,
                                  const SDL_Rect* rect,
                                  SDL_Color color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    int y = rect->y + rect->h / 2;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, rect->x, y, rect->x + rect->w - 1, y);
}
