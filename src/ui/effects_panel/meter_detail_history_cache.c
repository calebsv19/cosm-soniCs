#include "ui/effects_panel_meter_history_cache.h"

#include "ui/kit_viz_meter_adapter.h"
#include "vk_renderer.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MeterHistoryLineCache {
    SDL_Renderer* renderer;
    VkRendererTexture texture;
    bool texture_valid;
    uint32_t* pixels;
    SDL_Surface* surface;
    int width;
    int height;
    uint64_t signature;
} MeterHistoryLineCache;

static MeterHistoryLineCache g_levels_cache = {0};
static MeterHistoryLineCache g_corr_cache = {0};
static MeterHistoryLineCache g_lufs_cache = {0};
static MeterHistoryLineCache g_mid_cache = {0};
static MeterHistoryLineCache g_side_cache = {0};

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float meter_db(float linear) {
    if (linear <= 1e-9f) {
        return -90.0f;
    }
    return 20.0f * log10f(linear);
}

static uint64_t fnv1a_mix(uint64_t hash, const void* data, size_t bytes) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static float history_get_peak_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->level_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->level_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LEVEL_HISTORY_POINTS;
    idx %= FX_METER_LEVEL_HISTORY_POINTS;
    return history->peak_values[idx];
}

static float history_get_rms_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->level_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->level_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LEVEL_HISTORY_POINTS;
    idx %= FX_METER_LEVEL_HISTORY_POINTS;
    return history->rms_values[idx];
}

static float history_get_corr_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->corr_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->corr_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_CORR_HISTORY_POINTS;
    idx %= FX_METER_CORR_HISTORY_POINTS;
    return history->corr_values[idx];
}

static float history_get_lufs_by_age(const EffectsMeterHistory* history, int age_index, int mode) {
    int count = history ? history->lufs_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return -90.0f;
    }
    int idx = history->lufs_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_LUFS_HISTORY_POINTS;
    idx %= FX_METER_LUFS_HISTORY_POINTS;
    if (mode == FX_METER_LUFS_INTEGRATED) {
        return history->lufs_i_values[idx];
    }
    if (mode == FX_METER_LUFS_SHORT_TERM) {
        return history->lufs_s_values[idx];
    }
    return history->lufs_m_values[idx];
}

static float history_get_mid_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->mid_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->mid_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_MID_SIDE_HISTORY_POINTS;
    idx %= FX_METER_MID_SIDE_HISTORY_POINTS;
    return history->mid_values[idx];
}

static float history_get_side_by_age(const EffectsMeterHistory* history, int age_index) {
    int count = history ? history->mid_count : 0;
    if (!history || count <= 0 || age_index < 0 || age_index >= count) {
        return 0.0f;
    }
    int idx = history->mid_head - 1 - age_index;
    while (idx < 0) idx += FX_METER_MID_SIDE_HISTORY_POINTS;
    idx %= FX_METER_MID_SIDE_HISTORY_POINTS;
    return history->side_values[idx];
}

static void cache_reset(MeterHistoryLineCache* cache,
                        SDL_Renderer* renderer_for_destroy,
                        bool destroy_texture,
                        bool clear_renderer) {
    if (!cache) {
        return;
    }
    if (destroy_texture && cache->texture_valid && renderer_for_destroy) {
        vk_renderer_texture_destroy((VkRenderer*)renderer_for_destroy, &cache->texture);
    }
    cache->texture_valid = false;
    cache->texture = (VkRendererTexture){0};
    if (cache->surface) {
        SDL_FreeSurface(cache->surface);
        cache->surface = NULL;
    }
    free(cache->pixels);
    cache->pixels = NULL;
    cache->width = 0;
    cache->height = 0;
    cache->signature = 0;
    if (clear_renderer) {
        cache->renderer = NULL;
    }
}

static bool cache_bind_renderer(MeterHistoryLineCache* cache, SDL_Renderer* renderer) {
    if (!cache || !renderer) {
        return false;
    }
    if (cache->renderer && cache->renderer != renderer) {
        cache_reset(cache, NULL, false, true);
    }
    if (!cache->renderer) {
        cache->renderer = renderer;
    }
    return cache->renderer == renderer;
}

static bool cache_ensure_surface(MeterHistoryLineCache* cache, int width, int height) {
    if (!cache || width <= 0 || height <= 0) {
        return false;
    }
    if (cache->width == width &&
        cache->height == height &&
        cache->pixels &&
        cache->surface) {
        return true;
    }

    cache_reset(cache, NULL, false, false);
    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count == 0) {
        return false;
    }
    cache->pixels = (uint32_t*)calloc(pixel_count, sizeof(uint32_t));
    if (!cache->pixels) {
        return false;
    }
    cache->surface = SDL_CreateRGBSurfaceWithFormatFrom(cache->pixels,
                                                         width,
                                                         height,
                                                         32,
                                                         width * 4,
                                                         SDL_PIXELFORMAT_RGBA32);
    if (!cache->surface) {
        free(cache->pixels);
        cache->pixels = NULL;
        return false;
    }
    cache->width = width;
    cache->height = height;
    return true;
}

static bool cache_upload_texture(MeterHistoryLineCache* cache) {
    if (!cache || !cache->renderer || !cache->surface) {
        return false;
    }
    if (cache->texture_valid) {
        vk_renderer_texture_destroy((VkRenderer*)cache->renderer, &cache->texture);
        cache->texture_valid = false;
        cache->texture = (VkRendererTexture){0};
    }

    VkResult upload = vk_renderer_upload_sdl_surface_with_filter((VkRenderer*)cache->renderer,
                                                                  cache->surface,
                                                                  &cache->texture,
                                                                  VK_FILTER_NEAREST);
    if (upload != VK_SUCCESS) {
        cache->texture = (VkRendererTexture){0};
        return false;
    }
    cache->texture_valid = true;
    return true;
}

static void draw_pixel(uint32_t* pixels,
                       int width,
                       int height,
                       int x,
                       int y,
                       uint32_t color) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    pixels[(size_t)y * (size_t)width + (size_t)x] = color;
}

static void draw_line(uint32_t* pixels,
                      int width,
                      int height,
                      float x0f,
                      float y0f,
                      float x1f,
                      float y1f,
                      uint32_t color) {
    int x0 = (int)lroundf(x0f);
    int y0 = (int)lroundf(y0f);
    int x1 = (int)lroundf(x1f);
    int y1 = (int)lroundf(y1f);
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        draw_pixel(pixels, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err << 1;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static bool build_segments_fixed_slots(const float* samples,
                                       int sample_count,
                                       int total_slots,
                                       MeterHistoryLineCache* cache,
                                       float min_value,
                                       float max_value,
                                       KitVizVecSegment* out_segments,
                                       size_t max_segments,
                                       size_t* out_segment_count) {
    if (!samples || sample_count <= 1 || !cache || cache->width <= 0 || cache->height <= 0 ||
        !out_segments || !out_segment_count) {
        return false;
    }
    SDL_Rect local_rect = {0, 0, cache->width, cache->height};
    CoreResult r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(samples,
                                                                           (uint32_t)sample_count,
                                                                           (uint32_t)total_slots,
                                                                           &local_rect,
                                                                           (DawKitVizMeterPlotRange){min_value, max_value},
                                                                           out_segments,
                                                                           max_segments,
                                                                           out_segment_count);
    return r.code == CORE_OK && *out_segment_count > 0;
}

static void draw_segments_with_age_alpha(MeterHistoryLineCache* cache,
                                         const KitVizVecSegment* segments,
                                         size_t segment_count,
                                         int total_slots,
                                         SDL_Color color,
                                         float alpha_new,
                                         float alpha_old) {
    if (!cache || !cache->pixels || !cache->surface || !segments || segment_count == 0) {
        return;
    }
    for (size_t i = 0; i < segment_count; ++i) {
        float age = total_slots > 1
                        ? (float)(i + 1u) / (float)(total_slots - 1)
                        : 0.0f;
        float alpha = alpha_new + (alpha_old - alpha_new) * age;
        Uint8 a = (Uint8)lroundf(clampf(alpha, 0.0f, 255.0f));
        uint32_t px = SDL_MapRGBA(cache->surface->format,
                                  color.r,
                                  color.g,
                                  color.b,
                                  a);
        draw_line(cache->pixels,
                  cache->width,
                  cache->height,
                  segments[i].x0,
                  segments[i].y0,
                  segments[i].x1,
                  segments[i].y1,
                  px);
    }
}

static bool rebuild_single_line_cache(MeterHistoryLineCache* cache,
                                      const float* samples,
                                      int sample_count,
                                      int total_slots,
                                      float min_value,
                                      float max_value,
                                      SDL_Color color,
                                      float alpha_new,
                                      float alpha_old) {
    if (!cache || !cache->pixels || !cache->surface || sample_count <= 1) {
        return false;
    }
    memset(cache->pixels, 0, (size_t)cache->width * (size_t)cache->height * sizeof(uint32_t));

    size_t max_segments = (size_t)((sample_count > 1) ? (sample_count - 1) : 0);
    if (max_segments == 0) {
        return false;
    }
    KitVizVecSegment* segments = (KitVizVecSegment*)malloc(max_segments * sizeof(KitVizVecSegment));
    if (!segments) {
        return false;
    }
    size_t segment_count = 0;
    if (!build_segments_fixed_slots(samples,
                                    sample_count,
                                    total_slots,
                                    cache,
                                    min_value,
                                    max_value,
                                    segments,
                                    max_segments,
                                    &segment_count)) {
        free(segments);
        return false;
    }

    draw_segments_with_age_alpha(cache,
                                 segments,
                                 segment_count,
                                 total_slots,
                                 color,
                                 alpha_new,
                                 alpha_old);
    bool uploaded = cache_upload_texture(cache);
    free(segments);
    return uploaded;
}

static uint64_t levels_signature(const EffectsMeterHistory* history,
                                 float min_db,
                                 float max_db,
                                 SDL_Color peak_color,
                                 SDL_Color rms_color,
                                 int width,
                                 int height) {
    uint64_t hash = 1469598103934665603ull;
    if (!history) {
        return hash;
    }
    hash = fnv1a_mix(hash, &history->active_id, sizeof(history->active_id));
    hash = fnv1a_mix(hash, &history->active_type, sizeof(history->active_type));
    hash = fnv1a_mix(hash, &history->level_head, sizeof(history->level_head));
    hash = fnv1a_mix(hash, &history->level_count, sizeof(history->level_count));
    hash = fnv1a_mix(hash, history->peak_values, sizeof(history->peak_values));
    hash = fnv1a_mix(hash, history->rms_values, sizeof(history->rms_values));
    hash = fnv1a_mix(hash, &min_db, sizeof(min_db));
    hash = fnv1a_mix(hash, &max_db, sizeof(max_db));
    hash = fnv1a_mix(hash, &peak_color, sizeof(peak_color));
    hash = fnv1a_mix(hash, &rms_color, sizeof(rms_color));
    hash = fnv1a_mix(hash, &width, sizeof(width));
    hash = fnv1a_mix(hash, &height, sizeof(height));
    return hash;
}

static uint64_t corr_signature(const EffectsMeterHistory* history,
                               SDL_Color trace_color,
                               int width,
                               int height) {
    uint64_t hash = 1469598103934665603ull;
    if (!history) {
        return hash;
    }
    hash = fnv1a_mix(hash, &history->active_id, sizeof(history->active_id));
    hash = fnv1a_mix(hash, &history->active_type, sizeof(history->active_type));
    hash = fnv1a_mix(hash, &history->corr_head, sizeof(history->corr_head));
    hash = fnv1a_mix(hash, &history->corr_count, sizeof(history->corr_count));
    hash = fnv1a_mix(hash, history->corr_values, sizeof(history->corr_values));
    hash = fnv1a_mix(hash, &trace_color, sizeof(trace_color));
    hash = fnv1a_mix(hash, &width, sizeof(width));
    hash = fnv1a_mix(hash, &height, sizeof(height));
    return hash;
}

static uint64_t lufs_signature(const EffectsMeterHistory* history,
                               int lufs_mode,
                               float min_db,
                               float max_db,
                               SDL_Color trace_color,
                               int width,
                               int height) {
    uint64_t hash = 1469598103934665603ull;
    if (!history) {
        return hash;
    }
    hash = fnv1a_mix(hash, &history->active_id, sizeof(history->active_id));
    hash = fnv1a_mix(hash, &history->active_type, sizeof(history->active_type));
    hash = fnv1a_mix(hash, &history->lufs_head, sizeof(history->lufs_head));
    hash = fnv1a_mix(hash, &history->lufs_count, sizeof(history->lufs_count));
    hash = fnv1a_mix(hash, &lufs_mode, sizeof(lufs_mode));
    if (lufs_mode == FX_METER_LUFS_INTEGRATED) {
        hash = fnv1a_mix(hash, history->lufs_i_values, sizeof(history->lufs_i_values));
    } else if (lufs_mode == FX_METER_LUFS_SHORT_TERM) {
        hash = fnv1a_mix(hash, history->lufs_s_values, sizeof(history->lufs_s_values));
    } else {
        hash = fnv1a_mix(hash, history->lufs_m_values, sizeof(history->lufs_m_values));
    }
    hash = fnv1a_mix(hash, &min_db, sizeof(min_db));
    hash = fnv1a_mix(hash, &max_db, sizeof(max_db));
    hash = fnv1a_mix(hash, &trace_color, sizeof(trace_color));
    hash = fnv1a_mix(hash, &width, sizeof(width));
    hash = fnv1a_mix(hash, &height, sizeof(height));
    return hash;
}

static uint64_t mid_side_signature(const EffectsMeterHistory* history,
                                   bool side_lane,
                                   SDL_Color color,
                                   int width,
                                   int height) {
    uint64_t hash = 1469598103934665603ull;
    if (!history) {
        return hash;
    }
    hash = fnv1a_mix(hash, &history->active_id, sizeof(history->active_id));
    hash = fnv1a_mix(hash, &history->active_type, sizeof(history->active_type));
    hash = fnv1a_mix(hash, &history->mid_head, sizeof(history->mid_head));
    hash = fnv1a_mix(hash, &history->mid_count, sizeof(history->mid_count));
    if (side_lane) {
        hash = fnv1a_mix(hash, history->side_values, sizeof(history->side_values));
    } else {
        hash = fnv1a_mix(hash, history->mid_values, sizeof(history->mid_values));
    }
    hash = fnv1a_mix(hash, &color, sizeof(color));
    hash = fnv1a_mix(hash, &width, sizeof(width));
    hash = fnv1a_mix(hash, &height, sizeof(height));
    return hash;
}

bool effects_meter_history_cache_render_levels(SDL_Renderer* renderer,
                                               const SDL_Rect* history_rect,
                                               const EffectsMeterHistory* history,
                                               float min_db,
                                               float max_db,
                                               SDL_Color peak_color,
                                               SDL_Color rms_color) {
    if (!renderer || !history_rect || history_rect->w <= 0 || history_rect->h <= 0 ||
        !history || history->level_count <= 1) {
        return false;
    }
    if (!cache_bind_renderer(&g_levels_cache, renderer) ||
        !cache_ensure_surface(&g_levels_cache, history_rect->w, history_rect->h)) {
        return false;
    }

    uint64_t signature = levels_signature(history,
                                          min_db,
                                          max_db,
                                          peak_color,
                                          rms_color,
                                          history_rect->w,
                                          history_rect->h);
    if (!g_levels_cache.texture_valid || g_levels_cache.signature != signature) {
        const int count = history->level_count;
        if (count <= 1 || count > FX_METER_LEVEL_HISTORY_POINTS) {
            return false;
        }
        memset(g_levels_cache.pixels,
               0,
               (size_t)g_levels_cache.width * (size_t)g_levels_cache.height * sizeof(uint32_t));

        float peak_samples[FX_METER_LEVEL_HISTORY_POINTS];
        float rms_samples[FX_METER_LEVEL_HISTORY_POINTS];
        KitVizVecSegment peak_segments[FX_METER_LEVEL_HISTORY_POINTS];
        KitVizVecSegment rms_segments[FX_METER_LEVEL_HISTORY_POINTS];
        size_t peak_segment_count = 0;
        size_t rms_segment_count = 0;
        for (int i = 0; i < count; ++i) {
            peak_samples[i] = meter_db(clampf(history_get_peak_by_age(history, i), 0.0f, 2.0f));
            rms_samples[i] = meter_db(clampf(history_get_rms_by_age(history, i), 0.0f, 2.0f));
        }

        bool ok_peak = build_segments_fixed_slots(peak_samples,
                                                  count,
                                                  FX_METER_LEVEL_HISTORY_POINTS,
                                                  &g_levels_cache,
                                                  min_db,
                                                  max_db,
                                                  peak_segments,
                                                  FX_METER_LEVEL_HISTORY_POINTS,
                                                  &peak_segment_count);
        bool ok_rms = build_segments_fixed_slots(rms_samples,
                                                 count,
                                                 FX_METER_LEVEL_HISTORY_POINTS,
                                                 &g_levels_cache,
                                                 min_db,
                                                 max_db,
                                                 rms_segments,
                                                 FX_METER_LEVEL_HISTORY_POINTS,
                                                 &rms_segment_count);
        if (!ok_peak || !ok_rms) {
            return false;
        }

        draw_segments_with_age_alpha(&g_levels_cache,
                                     peak_segments,
                                     peak_segment_count,
                                     FX_METER_LEVEL_HISTORY_POINTS,
                                     peak_color,
                                     220.0f,
                                     110.0f);
        draw_segments_with_age_alpha(&g_levels_cache,
                                     rms_segments,
                                     rms_segment_count,
                                     FX_METER_LEVEL_HISTORY_POINTS,
                                     rms_color,
                                     200.0f,
                                     105.0f);
        if (!cache_upload_texture(&g_levels_cache)) {
            return false;
        }
        g_levels_cache.signature = signature;
    }

    SDL_Rect dst = *history_rect;
    vk_renderer_draw_texture((VkRenderer*)renderer, &g_levels_cache.texture, NULL, &dst);
    return true;
}

bool effects_meter_history_cache_render_correlation(SDL_Renderer* renderer,
                                                    const SDL_Rect* history_rect,
                                                    const EffectsMeterHistory* history,
                                                    SDL_Color trace_color) {
    if (!renderer || !history_rect || history_rect->w <= 0 || history_rect->h <= 0 ||
        !history || history->corr_count <= 1) {
        return false;
    }
    if (!cache_bind_renderer(&g_corr_cache, renderer) ||
        !cache_ensure_surface(&g_corr_cache, history_rect->w, history_rect->h)) {
        return false;
    }

    uint64_t signature = corr_signature(history, trace_color, history_rect->w, history_rect->h);
    if (!g_corr_cache.texture_valid || g_corr_cache.signature != signature) {
        int count = history->corr_count;
        if (count <= 1 || count > FX_METER_CORR_HISTORY_POINTS) {
            return false;
        }
        float samples[FX_METER_CORR_HISTORY_POINTS];
        for (int i = 0; i < count; ++i) {
            samples[i] = clampf(history_get_corr_by_age(history, i), -1.0f, 1.0f);
        }
        if (!rebuild_single_line_cache(&g_corr_cache,
                                       samples,
                                       count,
                                       FX_METER_CORR_HISTORY_POINTS,
                                       -1.0f,
                                       1.0f,
                                       trace_color,
                                       190.0f,
                                       100.0f)) {
            return false;
        }
        g_corr_cache.signature = signature;
    }

    SDL_Rect dst = *history_rect;
    vk_renderer_draw_texture((VkRenderer*)renderer, &g_corr_cache.texture, NULL, &dst);
    return true;
}

bool effects_meter_history_cache_render_lufs(SDL_Renderer* renderer,
                                             const SDL_Rect* history_rect,
                                             const EffectsMeterHistory* history,
                                             int lufs_mode,
                                             float min_db,
                                             float max_db,
                                             SDL_Color trace_color) {
    if (!renderer || !history_rect || history_rect->w <= 0 || history_rect->h <= 0 ||
        !history || history->lufs_count <= 1) {
        return false;
    }
    if (!cache_bind_renderer(&g_lufs_cache, renderer) ||
        !cache_ensure_surface(&g_lufs_cache, history_rect->w, history_rect->h)) {
        return false;
    }

    uint64_t signature = lufs_signature(history,
                                        lufs_mode,
                                        min_db,
                                        max_db,
                                        trace_color,
                                        history_rect->w,
                                        history_rect->h);
    if (!g_lufs_cache.texture_valid || g_lufs_cache.signature != signature) {
        int count = history->lufs_count;
        if (count <= 1 || count > FX_METER_LUFS_HISTORY_POINTS) {
            return false;
        }
        float samples[FX_METER_LUFS_HISTORY_POINTS];
        for (int i = 0; i < count; ++i) {
            samples[i] = history_get_lufs_by_age(history, i, lufs_mode);
        }
        if (!rebuild_single_line_cache(&g_lufs_cache,
                                       samples,
                                       count,
                                       FX_METER_LUFS_HISTORY_POINTS,
                                       min_db,
                                       max_db,
                                       trace_color,
                                       190.0f,
                                       95.0f)) {
            return false;
        }
        g_lufs_cache.signature = signature;
    }

    SDL_Rect dst = *history_rect;
    vk_renderer_draw_texture((VkRenderer*)renderer, &g_lufs_cache.texture, NULL, &dst);
    return true;
}

bool effects_meter_history_cache_render_mid_side(SDL_Renderer* renderer,
                                                 const SDL_Rect* mid_history_rect,
                                                 const SDL_Rect* side_history_rect,
                                                 const EffectsMeterHistory* history,
                                                 SDL_Color mid_color,
                                                 SDL_Color side_color) {
    if (!renderer || !mid_history_rect || !side_history_rect ||
        mid_history_rect->w <= 0 || mid_history_rect->h <= 0 ||
        side_history_rect->w <= 0 || side_history_rect->h <= 0 ||
        !history || history->mid_count <= 1) {
        return false;
    }
    if (!cache_bind_renderer(&g_mid_cache, renderer) ||
        !cache_bind_renderer(&g_side_cache, renderer) ||
        !cache_ensure_surface(&g_mid_cache, mid_history_rect->w, mid_history_rect->h) ||
        !cache_ensure_surface(&g_side_cache, side_history_rect->w, side_history_rect->h)) {
        return false;
    }

    uint64_t mid_sig = mid_side_signature(history,
                                          false,
                                          mid_color,
                                          mid_history_rect->w,
                                          mid_history_rect->h);
    uint64_t side_sig = mid_side_signature(history,
                                           true,
                                           side_color,
                                           side_history_rect->w,
                                           side_history_rect->h);

    int count = history->mid_count;
    if (count <= 1 || count > FX_METER_MID_SIDE_HISTORY_POINTS) {
        return false;
    }

    if (!g_mid_cache.texture_valid || g_mid_cache.signature != mid_sig) {
        float samples[FX_METER_MID_SIDE_HISTORY_POINTS];
        for (int i = 0; i < count; ++i) {
            samples[i] = clampf(history_get_mid_by_age(history, i), 0.0f, 1.0f);
        }
        if (!rebuild_single_line_cache(&g_mid_cache,
                                       samples,
                                       count,
                                       FX_METER_MID_SIDE_HISTORY_POINTS,
                                       0.0f,
                                       1.0f,
                                       mid_color,
                                       190.0f,
                                       95.0f)) {
            return false;
        }
        g_mid_cache.signature = mid_sig;
    }

    if (!g_side_cache.texture_valid || g_side_cache.signature != side_sig) {
        float samples[FX_METER_MID_SIDE_HISTORY_POINTS];
        for (int i = 0; i < count; ++i) {
            samples[i] = clampf(history_get_side_by_age(history, i), 0.0f, 1.0f);
        }
        if (!rebuild_single_line_cache(&g_side_cache,
                                       samples,
                                       count,
                                       FX_METER_MID_SIDE_HISTORY_POINTS,
                                       0.0f,
                                       1.0f,
                                       side_color,
                                       190.0f,
                                       95.0f)) {
            return false;
        }
        g_side_cache.signature = side_sig;
    }

    SDL_Rect mid_dst = *mid_history_rect;
    SDL_Rect side_dst = *side_history_rect;
    vk_renderer_draw_texture((VkRenderer*)renderer, &g_mid_cache.texture, NULL, &mid_dst);
    vk_renderer_draw_texture((VkRenderer*)renderer, &g_side_cache.texture, NULL, &side_dst);
    return true;
}

void effects_meter_history_cache_shutdown(SDL_Renderer* renderer) {
    SDL_Renderer* destroy_renderer = renderer;
    if (!destroy_renderer) {
        destroy_renderer = g_levels_cache.renderer ? g_levels_cache.renderer
                          : g_corr_cache.renderer ? g_corr_cache.renderer
                          : g_lufs_cache.renderer ? g_lufs_cache.renderer
                          : g_mid_cache.renderer ? g_mid_cache.renderer
                          : g_side_cache.renderer;
    }
    cache_reset(&g_levels_cache, destroy_renderer, true, true);
    cache_reset(&g_corr_cache, destroy_renderer, true, true);
    cache_reset(&g_lufs_cache, destroy_renderer, true, true);
    cache_reset(&g_mid_cache, destroy_renderer, true, true);
    cache_reset(&g_side_cache, destroy_renderer, true, true);
}

void effects_meter_history_cache_invalidate(SDL_Renderer* renderer) {
    (void)renderer;
    cache_reset(&g_levels_cache, NULL, false, true);
    cache_reset(&g_corr_cache, NULL, false, true);
    cache_reset(&g_lufs_cache, NULL, false, true);
    cache_reset(&g_mid_cache, NULL, false, true);
    cache_reset(&g_side_cache, NULL, false, true);
}
