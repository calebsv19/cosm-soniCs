#include "ui/kit_viz_meter_adapter.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static CoreResult error_result(CoreError code, const char* message) {
    CoreResult r = {code, message};
    return r;
}

static void map_grayscale(float t, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t c = (uint8_t)lroundf(clampf(t, 0.0f, 1.0f) * 255.0f);
    *r = c;
    *g = c;
    *b = c;
}

static void map_heat(float t, uint8_t* r, uint8_t* g, uint8_t* b) {
    float s = clampf(t, 0.0f, 1.0f);
    if (s < 0.25f) {
        float k = s / 0.25f;
        *r = 0;
        *g = (uint8_t)lroundf(k * 255.0f);
        *b = 255;
        return;
    }
    if (s < 0.5f) {
        float k = (s - 0.25f) / 0.25f;
        *r = 0;
        *g = 255;
        *b = (uint8_t)lroundf((1.0f - k) * 255.0f);
        return;
    }
    if (s < 0.75f) {
        float k = (s - 0.5f) / 0.25f;
        *r = (uint8_t)lroundf(k * 255.0f);
        *g = 255;
        *b = 0;
        return;
    }
    {
        float k = (s - 0.75f) / 0.25f;
        *r = 255;
        *g = (uint8_t)lroundf((1.0f - k) * 255.0f);
        *b = 0;
    }
}

static float normalize_value(float value, DawKitVizMeterPlotRange range) {
    float denom = range.max_value - range.min_value;
    if (!(denom > 0.0f)) {
        return 0.5f;
    }
    return clampf((value - range.min_value) / denom, 0.0f, 1.0f);
}

CoreResult daw_kit_viz_meter_plot_line_from_y_samples(const float* samples,
                                                      uint32_t sample_count,
                                                      const SDL_Rect* rect,
                                                      DawKitVizMeterPlotRange range,
                                                      KitVizVecSegment* out_segments,
                                                      size_t max_segments,
                                                      size_t* out_segment_count) {
    if (!samples || sample_count < 2 || !rect || rect->w <= 0 || rect->h <= 0 ||
        !out_segments || !out_segment_count) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid meter line request");
    }
    if (max_segments < (size_t)(sample_count - 1u)) {
        return error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }
    if (!isfinite(range.min_value) || !isfinite(range.max_value)) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid plot range");
    }

    float width = (float)(rect->w - 1);
    float height = (float)(rect->h - 1);
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (!isfinite(samples[i])) {
            return error_result(CORE_ERR_INVALID_ARG, "non-finite sample");
        }
        float t = (float)i / (float)(sample_count - 1u);
        float n = normalize_value(samples[i], range);
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

CoreResult daw_kit_viz_meter_plot_scope_segments(const float* xs,
                                                 const float* ys,
                                                 uint32_t point_count,
                                                 const SDL_Rect* rect,
                                                 DawKitVizMeterScopeMode mode,
                                                 float scale,
                                                 KitVizVecSegment* out_segments,
                                                 size_t max_segments,
                                                 size_t* out_segment_count) {
    if (!xs || !ys || point_count < 2 || !rect || rect->w <= 0 || rect->h <= 0 ||
        !out_segments || !out_segment_count || !isfinite(scale) || scale <= 0.0f) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid meter scope request");
    }
    if (max_segments < (size_t)(point_count - 1u)) {
        return error_result(CORE_ERR_INVALID_ARG, "segment buffer too small");
    }

    float cx = (float)rect->x + (float)rect->w * 0.5f;
    float cy = (float)rect->y + (float)rect->h * 0.5f;
    float sx = (float)rect->w * 0.5f * scale;
    float sy = (float)rect->h * 0.5f * scale;
    float prev_px = 0.0f;
    float prev_py = 0.0f;

    for (uint32_t i = 0; i < point_count; ++i) {
        if (!isfinite(xs[i]) || !isfinite(ys[i])) {
            return error_result(CORE_ERR_INVALID_ARG, "non-finite scope point");
        }
        float px = clampf(xs[i], -1.0f, 1.0f);
        float py = clampf(ys[i], -1.0f, 1.0f);
        if (mode == DAW_KIT_VIZ_METER_SCOPE_MID_SIDE) {
            float mid = 0.5f * (px + py);
            float side = 0.5f * (px - py);
            px = mid;
            py = side;
        }
        float x = cx + px * sx;
        float y = cy - py * sy;
        if (i > 0) {
            out_segments[i - 1u] = (KitVizVecSegment){prev_px, prev_py, x, y};
        }
        prev_px = x;
        prev_py = y;
    }

    *out_segment_count = (size_t)(point_count - 1u);
    return core_result_ok();
}

CoreResult daw_kit_viz_meter_build_spectrogram_rgba(const float* frames,
                                                    uint32_t frame_count,
                                                    uint32_t bins,
                                                    uint32_t max_frames,
                                                    float db_floor,
                                                    float db_ceil,
                                                    DawKitVizMeterSpectrogramPalette palette,
                                                    uint8_t* out_rgba,
                                                    size_t out_rgba_size) {
    if (!frames || !out_rgba || bins == 0 || max_frames == 0 || frame_count > max_frames ||
        !isfinite(db_floor) || !isfinite(db_ceil)) {
        return error_result(CORE_ERR_INVALID_ARG, "invalid spectrogram request");
    }
    size_t pixel_count = (size_t)max_frames * (size_t)bins;
    size_t needed = pixel_count * 4u;
    if (out_rgba_size < needed) {
        return error_result(CORE_ERR_INVALID_ARG, "spectrogram buffer too small");
    }

    float range = db_ceil - db_floor;
    if (!(range > 0.0f) || !isfinite(range)) {
        range = 1.0f;
    }

    for (uint32_t frame = 0; frame < max_frames; ++frame) {
        float age = max_frames > 1u ? (float)frame / (float)(max_frames - 1u) : 0.0f;
        float age_fade = 1.0f - 0.35f * age;
        for (uint32_t bin = 0; bin < bins; ++bin) {
            float t = 0.0f;
            if (frame < frame_count) {
                float db = frames[(size_t)frame * (size_t)bins + (size_t)bin];
                if (!isfinite(db)) {
                    return error_result(CORE_ERR_INVALID_ARG, "non-finite spectrogram sample");
                }
                t = clampf((db - db_floor) / range, 0.0f, 1.0f);
            }
            if (palette == DAW_KIT_VIZ_METER_SPECTROGRAM_WHITE_BLACK) {
                t = 1.0f - t;
            }
            uint32_t y = bins - 1u - bin;
            size_t p = ((size_t)y * (size_t)max_frames + (size_t)frame) * 4u;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (palette == DAW_KIT_VIZ_METER_SPECTROGRAM_HEAT) {
                map_heat(t, &r, &g, &b);
            } else {
                map_grayscale(t, &r, &g, &b);
            }
            out_rgba[p + 0] = (uint8_t)lroundf(clampf((float)r * age_fade, 0.0f, 255.0f));
            out_rgba[p + 1] = (uint8_t)lroundf(clampf((float)g * age_fade, 0.0f, 255.0f));
            out_rgba[p + 2] = (uint8_t)lroundf(clampf((float)b * age_fade, 0.0f, 255.0f));
            out_rgba[p + 3] = 255;
        }
    }

    return core_result_ok();
}

void daw_kit_viz_meter_render_segments(SDL_Renderer* renderer,
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
