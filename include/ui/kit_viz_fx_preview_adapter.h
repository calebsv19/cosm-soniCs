#ifndef UI_KIT_VIZ_FX_PREVIEW_ADAPTER_H
#define UI_KIT_VIZ_FX_PREVIEW_ADAPTER_H

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>

#include "core_base.h"
#include "kit_viz.h"

// DawKitVizPlotRange describes the input value range mapped into a target rect.
typedef struct DawKitVizPlotRange {
    float min_value;
    float max_value;
} DawKitVizPlotRange;

// daw_kit_viz_plot_line_from_y_samples converts scalar samples into line segments.
CoreResult daw_kit_viz_plot_line_from_y_samples(const float* samples,
                                                uint32_t sample_count,
                                                const SDL_Rect* rect,
                                                DawKitVizPlotRange range,
                                                KitVizVecSegment* out_segments,
                                                size_t max_segments,
                                                size_t* out_segment_count);

// daw_kit_viz_plot_envelope_from_min_max converts min/max sample pairs into vertical segments.
CoreResult daw_kit_viz_plot_envelope_from_min_max(const float* mins,
                                                  const float* maxs,
                                                  uint32_t sample_count,
                                                  const SDL_Rect* rect,
                                                  DawKitVizPlotRange range,
                                                  KitVizVecSegment* out_segments,
                                                  size_t max_segments,
                                                  size_t* out_segment_count);

// daw_kit_viz_render_segments draws prebuilt segments with a single color.
void daw_kit_viz_render_segments(SDL_Renderer* renderer,
                                 const KitVizVecSegment* segments,
                                 size_t segment_count,
                                 SDL_Color color);

// daw_kit_viz_draw_center_line draws a horizontal center line through a rect.
void daw_kit_viz_draw_center_line(SDL_Renderer* renderer,
                                  const SDL_Rect* rect,
                                  SDL_Color color);

#endif
