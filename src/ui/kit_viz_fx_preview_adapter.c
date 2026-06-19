#include "ui/kit_viz_fx_preview_adapter.h"

#include "kit_viz_adapter_common.h"

CoreResult daw_kit_viz_plot_line_from_y_samples(const float* samples,
                                                uint32_t sample_count,
                                                const SDL_Rect* rect,
                                                DawKitVizPlotRange range,
                                                KitVizVecSegment* out_segments,
                                                size_t max_segments,
                                                size_t* out_segment_count) {
    return daw_kit_viz_plot_line_segments_from_y_samples(samples,
                                                         sample_count,
                                                         sample_count,
                                                         rect,
                                                         (DawKitVizScalarRange){range.min_value, range.max_value},
                                                         out_segments,
                                                         max_segments,
                                                         out_segment_count,
                                                         "invalid line plot request");
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
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "invalid envelope plot request");
    }

    if (max_segments < (size_t)sample_count) {
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }
    if (!isfinite(range.min_value) || !isfinite(range.max_value)) {
        return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "invalid plot range");
    }

    float width = (float)(rect->w - 1);
    float height = (float)(rect->h - 1);
    DawKitVizScalarRange scalar_range = {range.min_value, range.max_value};
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (!isfinite(mins[i]) || !isfinite(maxs[i])) {
            return daw_kit_viz_error_result(CORE_ERR_INVALID_ARG, "non-finite envelope sample");
        }
        float t = sample_count > 1 ? (float)i / (float)(sample_count - 1u) : 0.0f;
        float nmin = daw_kit_viz_normalize_value(mins[i], scalar_range);
        float nmax = daw_kit_viz_normalize_value(maxs[i], scalar_range);
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
    daw_kit_viz_render_segments_common(renderer, segments, segment_count, color);
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
