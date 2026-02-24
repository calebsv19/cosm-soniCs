#include "ui/kit_viz_waveform_adapter.h"

#include "kit_viz.h"

#include <math.h>
#include <stdlib.h>

// Snaps samples-per-pixel to nearby powers of two for cache reuse.
static int quantize_samples_per_pixel(int spp) {
    if (spp < 1) {
        return 1;
    }
    int upper = 1;
    while (upper < spp && upper < (1 << 30)) {
        upper <<= 1;
    }
    int lower = upper >> 1;
    if (lower < 1) {
        return upper;
    }
    return (spp - lower) < (upper - spp) ? lower : upper;
}

DawKitVizWaveformResult daw_kit_viz_render_waveform_ex(const DawKitVizWaveformRequest *request) {
    if (!request || !request->renderer || !request->cache || !request->clip || !request->source_path ||
        !request->target_rect || request->view_frame_count == 0) {
        return DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST;
    }
    if (request->target_rect->w <= 0 || request->target_rect->h <= 0) {
        return DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST;
    }

    double exact_spp = (double)request->view_frame_count / (double)request->target_rect->w;
    if (!(exact_spp > 0.0)) {
        return DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST;
    }
    int spp = (int)llround(exact_spp);
    if (spp < 1) {
        spp = 1;
    }

    int bucket_spp = quantize_samples_per_pixel(spp);
    const WaveformCacheEntry *entry = waveform_cache_get(request->cache,
                                                         request->clip,
                                                         request->source_path,
                                                         bucket_spp);
    if (!entry || entry->bucket_count < 2 || !entry->mins || !entry->maxs) {
        return DAW_KIT_VIZ_WAVEFORM_MISSING_CACHE;
    }

    int width = request->target_rect->w;
    float *samples_min = (float *)malloc((size_t)width * sizeof(float));
    float *samples_max = (float *)malloc((size_t)width * sizeof(float));
    if (!samples_min || !samples_max) {
        free(samples_min);
        free(samples_max);
        return DAW_KIT_VIZ_WAVEFORM_SAMPLING_FAILED;
    }

    double bucket_scale = exact_spp / (double)bucket_spp;
    double bucket_start = (double)request->view_start_frame / (double)bucket_spp;
    CoreResult r = kit_viz_sample_waveform_envelope(entry->mins,
                                                    entry->maxs,
                                                    (uint32_t)entry->bucket_count,
                                                    bucket_start,
                                                    bucket_scale,
                                                    (uint32_t)width,
                                                    samples_min,
                                                    samples_max,
                                                    (size_t)width);
    if (r.code != CORE_OK) {
        free(samples_min);
        free(samples_max);
        return DAW_KIT_VIZ_WAVEFORM_SAMPLING_FAILED;
    }

    int mid_y = request->target_rect->y + request->target_rect->h / 2;
    int amp = request->target_rect->h / 2 - 1;
    if (amp < 1) {
        amp = 1;
    }

    SDL_SetRenderDrawColor(request->renderer, request->color.r, request->color.g, request->color.b, request->color.a);
    bool line_mode = exact_spp <= 2.0;
    int prev_x = 0;
    int prev_y = 0;
    for (int px = 0; px < width; ++px) {
        int x = request->target_rect->x + px;
        float min_v = samples_min[px];
        float max_v = samples_max[px];

        if (line_mode) {
            float v = 0.5f * (min_v + max_v);
            int y = mid_y - (int)llround((double)v * (double)amp);
            if (px > 0) {
                SDL_RenderDrawLine(request->renderer, prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
            continue;
        }

        int y_top = mid_y - (int)llround((double)max_v * (double)amp);
        int y_bot = mid_y - (int)llround((double)min_v * (double)amp);
        if (y_top > y_bot) {
            int tmp = y_top;
            y_top = y_bot;
            y_bot = tmp;
        }
        SDL_RenderDrawLine(request->renderer, x, y_top, x, y_bot);
    }

    free(samples_min);
    free(samples_max);
    return DAW_KIT_VIZ_WAVEFORM_RENDERED;
}

bool daw_kit_viz_render_waveform(const DawKitVizWaveformRequest *request) {
    return daw_kit_viz_render_waveform_ex(request) == DAW_KIT_VIZ_WAVEFORM_RENDERED;
}
