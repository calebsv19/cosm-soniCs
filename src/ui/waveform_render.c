#include "ui/waveform_render.h"

#include <math.h>

// Snaps samples-per-pixel to a nearby power of two for cache reuse.
static int waveform_quantize_samples_per_pixel(int spp) {
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

bool waveform_render_view(SDL_Renderer* renderer,
                          WaveformCache* cache,
                          const AudioMediaClip* clip,
                          const char* path,
                          const SDL_Rect* rect,
                          uint64_t view_start_frame,
                          uint64_t view_frame_count,
                          SDL_Color color) {
    if (!renderer || !cache || !clip || !path || !rect) {
        return false;
    }
    if (rect->w <= 0 || rect->h <= 0 || view_frame_count == 0) {
        return false;
    }

    double exact_spp = (double)view_frame_count / (double)rect->w;
    if (exact_spp <= 0.0) {
        return false;
    }
    int samples_per_pixel = (int)llround(exact_spp);
    if (samples_per_pixel < 1) {
        samples_per_pixel = 1;
    }
    int bucket_spp = waveform_quantize_samples_per_pixel(samples_per_pixel);
    const WaveformCacheEntry* entry = waveform_cache_get(cache, clip, path, bucket_spp);
    if (!entry || entry->bucket_count <= 0) {
        return false;
    }

    double bucket_scale = exact_spp / (double)bucket_spp;
    double bucket_start = (double)view_start_frame / (double)bucket_spp;
    int mid_y = rect->y + rect->h / 2;
    int amp = rect->h / 2 - 1;
    if (amp < 1) {
        amp = 1;
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    bool line_mode = exact_spp <= 2.0;
    int prev_x = 0;
    int prev_y = 0;
    for (int px = 0; px < rect->w; ++px) {
        double pos = bucket_start + (double)px * bucket_scale;
        int bucket = (int)floor(pos);
        if (bucket < 0 || bucket >= entry->bucket_count) {
            continue;
        }
        if (bucket + 1 >= entry->bucket_count) {
            break;
        }
        double t = pos - (double)bucket;
        float min_a = entry->mins[bucket];
        float max_a = entry->maxs[bucket];
        float min_b = entry->mins[bucket + 1];
        float max_b = entry->maxs[bucket + 1];
        float min_v = (float)((1.0 - t) * min_a + t * min_b);
        float max_v = (float)((1.0 - t) * max_a + t * max_b);
        int x = rect->x + px;
        if (line_mode) {
            float v = 0.5f * (min_v + max_v);
            int y = mid_y - (int)llround((double)v * (double)amp);
            if (px > 0) {
                SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
        } else {
            int y_top = mid_y - (int)llround((double)max_v * (double)amp);
            int y_bot = mid_y - (int)llround((double)min_v * (double)amp);
            if (y_top > y_bot) {
                int tmp = y_top;
                y_top = y_bot;
                y_bot = tmp;
            }
            SDL_RenderDrawLine(renderer, x, y_top, x, y_bot);
        }
    }
    return true;
}

bool waveform_render_samples_view(SDL_Renderer* renderer,
                                  const AudioMediaClip* clip,
                                  const SDL_Rect* rect,
                                  uint64_t view_start_frame,
                                  uint64_t view_frame_count,
                                  SDL_Color color) {
    if (!renderer || !clip || !clip->samples || !rect || clip->channels <= 0) {
        return false;
    }
    if (rect->w <= 0 || rect->h <= 0 || view_frame_count == 0 || clip->frame_count == 0) {
        return false;
    }
    if (view_start_frame >= clip->frame_count) {
        return false;
    }
    uint64_t view_end_frame = view_start_frame + view_frame_count;
    if (view_end_frame < view_start_frame || view_end_frame > clip->frame_count) {
        view_end_frame = clip->frame_count;
    }
    if (view_end_frame <= view_start_frame) {
        return false;
    }

    int mid_y = rect->y + rect->h / 2;
    int amp = rect->h / 2 - 1;
    if (amp < 1) {
        amp = 1;
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    bool line_mode = view_frame_count <= (uint64_t)rect->w * 2u;
    int prev_x = 0;
    int prev_y = 0;
    bool have_prev = false;
    for (int px = 0; px < rect->w; ++px) {
        uint64_t start = view_start_frame + ((uint64_t)px * view_frame_count) / (uint64_t)rect->w;
        uint64_t end = view_start_frame + ((uint64_t)(px + 1) * view_frame_count) / (uint64_t)rect->w;
        if (end <= start) {
            end = start + 1;
        }
        if (start >= view_end_frame) {
            break;
        }
        if (end > view_end_frame) {
            end = view_end_frame;
        }

        float min_v = 1.0f;
        float max_v = -1.0f;
        for (uint64_t f = start; f < end; ++f) {
            float sum = 0.0f;
            uint64_t base = f * (uint64_t)clip->channels;
            for (int ch = 0; ch < clip->channels; ++ch) {
                sum += clip->samples[base + (uint64_t)ch];
            }
            float v = sum / (float)clip->channels;
            if (v < -1.0f) v = -1.0f;
            if (v > 1.0f) v = 1.0f;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
        }

        int x = rect->x + px;
        if (line_mode) {
            float v = 0.5f * (min_v + max_v);
            int y = mid_y - (int)llround((double)v * (double)amp);
            if (have_prev) {
                SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
            have_prev = true;
        } else {
            int y_top = mid_y - (int)llround((double)max_v * (double)amp);
            int y_bot = mid_y - (int)llround((double)min_v * (double)amp);
            if (y_top > y_bot) {
                int tmp = y_top;
                y_top = y_bot;
                y_bot = tmp;
            }
            SDL_RenderDrawLine(renderer, x, y_top, x, y_bot);
        }
    }
    return true;
}
