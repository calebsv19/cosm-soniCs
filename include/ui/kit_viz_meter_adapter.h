#ifndef UI_KIT_VIZ_METER_ADAPTER_H
#define UI_KIT_VIZ_METER_ADAPTER_H

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>

#include "core_base.h"
#include "kit_viz.h"

// DawKitVizMeterPlotRange describes the scalar input range mapped into a target rect.
typedef struct DawKitVizMeterPlotRange {
    float min_value;
    float max_value;
} DawKitVizMeterPlotRange;

// DawKitVizMeterScopeMode selects vectorscope interpretation for XY points.
typedef enum DawKitVizMeterScopeMode {
    DAW_KIT_VIZ_METER_SCOPE_LEFT_RIGHT = 0,
    DAW_KIT_VIZ_METER_SCOPE_MID_SIDE
} DawKitVizMeterScopeMode;

// DawKitVizMeterSpectrogramPalette selects spectrogram color mapping behavior.
typedef enum DawKitVizMeterSpectrogramPalette {
    DAW_KIT_VIZ_METER_SPECTROGRAM_WHITE_BLACK = 0,
    DAW_KIT_VIZ_METER_SPECTROGRAM_BLACK_WHITE,
    DAW_KIT_VIZ_METER_SPECTROGRAM_HEAT
} DawKitVizMeterSpectrogramPalette;

// daw_kit_viz_meter_plot_line_from_y_samples converts scalar samples into line segments.
CoreResult daw_kit_viz_meter_plot_line_from_y_samples(const float* samples,
                                                      uint32_t sample_count,
                                                      const SDL_Rect* rect,
                                                      DawKitVizMeterPlotRange range,
                                                      KitVizVecSegment* out_segments,
                                                      size_t max_segments,
                                                      size_t* out_segment_count);

// daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots maps sample indices against
// a fixed slot domain so partially-filled histories do not stretch to full width.
CoreResult daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(const float* samples,
                                                                  uint32_t sample_count,
                                                                  uint32_t total_slots,
                                                                  const SDL_Rect* rect,
                                                                  DawKitVizMeterPlotRange range,
                                                                  KitVizVecSegment* out_segments,
                                                                  size_t max_segments,
                                                                  size_t* out_segment_count);

// daw_kit_viz_meter_plot_scope_segments converts XY scope points into line segments.
CoreResult daw_kit_viz_meter_plot_scope_segments(const float* xs,
                                                 const float* ys,
                                                 uint32_t point_count,
                                                 const SDL_Rect* rect,
                                                 DawKitVizMeterScopeMode mode,
                                                 float scale,
                                                 KitVizVecSegment* out_segments,
                                                 size_t max_segments,
                                                 size_t* out_segment_count);

// daw_kit_viz_meter_build_spectrogram_rgba converts frame-major dB spectrogram rows
// into RGBA image pixels using normalized palette/range handling.
CoreResult daw_kit_viz_meter_build_spectrogram_rgba(const float* frames,
                                                    uint32_t frame_count,
                                                    uint32_t bins,
                                                    uint32_t max_frames,
                                                    float db_floor,
                                                    float db_ceil,
                                                    DawKitVizMeterSpectrogramPalette palette,
                                                    uint8_t* out_rgba,
                                                    size_t out_rgba_size);

// daw_kit_viz_meter_render_segments draws prebuilt segments with a single color.
void daw_kit_viz_meter_render_segments(SDL_Renderer* renderer,
                                       const KitVizVecSegment* segments,
                                       size_t segment_count,
                                       SDL_Color color);

#endif
