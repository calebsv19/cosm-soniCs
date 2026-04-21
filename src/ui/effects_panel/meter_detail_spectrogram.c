#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "ui/kit_viz_meter_adapter.h"
#include "ui/shared_theme_font_adapter.h"
#include "vk_renderer.h"

#include <math.h>
#include <stdio.h>

enum {
    METER_SPECTROGRAM_RGBA_CAPACITY = ENGINE_SPECTROGRAM_HISTORY * ENGINE_SPECTROGRAM_BINS * 4
};

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

// Reused render-thread scratch to avoid per-frame heap allocation.
static uint8_t g_meter_spectrogram_rgba[METER_SPECTROGRAM_RGBA_CAPACITY];
// Reused SDL surface wrapper over the static RGBA staging buffer.
static SDL_Surface* g_meter_spectrogram_surface = NULL;

static void resolve_spectrogram_theme(SDL_Color* fill,
                                      SDL_Color* border,
                                      SDL_Color* history_bg,
                                      SDL_Color* meter_bg,
                                      SDL_Color* axis) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (fill) *fill = theme.inspector_fill;
        if (border) *border = theme.pane_border;
        if (history_bg) *history_bg = theme.timeline_fill;
        if (meter_bg) *meter_bg = theme.slider_track;
        if (axis) *axis = theme.text_muted;
        return;
    }
    if (fill) *fill = (SDL_Color){22, 24, 30, 255};
    if (border) *border = (SDL_Color){70, 75, 92, 255};
    if (history_bg) *history_bg = (SDL_Color){26, 28, 36, 255};
    if (meter_bg) *meter_bg = (SDL_Color){50, 54, 66, 255};
    if (axis) *axis = (SDL_Color){130, 150, 170, 255};
}

static SDL_Surface* meter_spectrogram_get_surface(void) {
    if (g_meter_spectrogram_surface) {
        return g_meter_spectrogram_surface;
    }
    g_meter_spectrogram_surface = SDL_CreateRGBSurfaceWithFormatFrom(g_meter_spectrogram_rgba,
                                                                      ENGINE_SPECTROGRAM_HISTORY,
                                                                      ENGINE_SPECTROGRAM_BINS,
                                                                      32,
                                                                      ENGINE_SPECTROGRAM_HISTORY * 4,
                                                                      SDL_PIXELFORMAT_RGBA32);
    return g_meter_spectrogram_surface;
}

static void format_hz_label(float hz, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    if (hz >= 10000.0f) {
        snprintf(out, out_size, "%.0fk", hz / 1000.0f);
    } else if (hz >= 1000.0f) {
        snprintf(out, out_size, "%.1fk", hz / 1000.0f);
    } else {
        snprintf(out, out_size, "%.0f", hz);
    }
}

static void build_spectrogram_legacy_rgba(const float* frames,
                                          int bins,
                                          int frame_count,
                                          int max_frames,
                                          float floor_db,
                                          float ceil_db,
                                          int palette_mode,
                                          uint8_t* out_rgba,
                                          size_t out_rgba_size) {
    if (!frames || !out_rgba || bins <= 0 || max_frames <= 0) {
        return;
    }
    size_t needed = (size_t)bins * (size_t)max_frames * 4u;
    if (out_rgba_size < needed) {
        return;
    }
    float range = (ceil_db - floor_db);
    if (range <= 0.0f) {
        range = 1.0f;
    }
    for (int frame = 0; frame < max_frames; ++frame) {
        float age = max_frames > 1 ? (float)frame / (float)(max_frames - 1) : 0.0f;
        float age_fade = 1.0f - 0.35f * age;
        for (int bin = 0; bin < bins; ++bin) {
            float norm = 0.0f;
            if (frame < frame_count) {
                float db = frames[frame * bins + bin];
                norm = (db - floor_db) / range;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
            }
            if (palette_mode == FX_METER_SPECTROGRAM_WHITE_BLACK) {
                norm = 1.0f - norm;
            }
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (palette_mode == FX_METER_SPECTROGRAM_HEAT) {
                if (norm < 0.25f) {
                    float k = norm / 0.25f;
                    r = 0;
                    g = (uint8_t)(k * 255.0f);
                    b = 255;
                } else if (norm < 0.5f) {
                    float k = (norm - 0.25f) / 0.25f;
                    r = 0;
                    g = 255;
                    b = (uint8_t)((1.0f - k) * 255.0f);
                } else if (norm < 0.75f) {
                    float k = (norm - 0.5f) / 0.25f;
                    r = (uint8_t)(k * 255.0f);
                    g = 255;
                    b = 0;
                } else {
                    float k = (norm - 0.75f) / 0.25f;
                    r = 255;
                    g = (uint8_t)((1.0f - k) * 255.0f);
                    b = 0;
                }
            } else {
                uint8_t c = (uint8_t)lroundf(norm * 255.0f);
                r = c;
                g = c;
                b = c;
            }
            r = (uint8_t)lroundf((float)r * age_fade);
            g = (uint8_t)lroundf((float)g * age_fade);
            b = (uint8_t)lroundf((float)b * age_fade);
            int y = bins - 1 - bin;
            size_t p = ((size_t)y * (size_t)max_frames + (size_t)frame) * 4u;
            out_rgba[p + 0] = r;
            out_rgba[p + 1] = g;
            out_rgba[p + 2] = b;
            out_rgba[p + 3] = 255;
        }
    }
}

static void render_spectrogram_surface(SDL_Renderer* renderer,
                                       SDL_Surface* surface,
                                       const SDL_Rect* history_rect,
                                       int bins,
                                       int max_frames,
                                       SDL_Color border) {
    if (!renderer || !surface || !history_rect || bins <= 0 || max_frames <= 0) {
        return;
    }
    VkRendererTexture tex;
    if (vk_renderer_upload_sdl_surface_with_filter(renderer, surface, &tex, VK_FILTER_NEAREST) == VK_SUCCESS) {
        SDL_Rect src = {0, 0, max_frames, bins};
        vk_renderer_draw_texture(renderer, &tex, &src, history_rect);
        vk_renderer_queue_texture_destroy(renderer, &tex);
    }
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, history_rect);
}

// effects_meter_render_spectrogram draws a scrolling spectrogram heatmap.
void effects_meter_render_spectrogram(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const EngineSpectrogramSnapshot* spectrogram,
                                      const float* frames,
                                      int palette_mode,
                                      const EffectsMeterHistoryGridContext* history_grid,
                                      SDL_Color label_color,
                                      SDL_Color dim_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color border = {0};
    SDL_Color fill = {0};
    SDL_Color history_bg = {0};
    SDL_Color meter_bg = {0};
    SDL_Color axis = {0};
    resolve_spectrogram_theme(&fill, &border, &history_bg, &meter_bg, &axis);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int body_h = ui_font_line_height(1.0f);
    const int title_h = ui_font_line_height(1.2f);
    const int pad = max_int(12, body_h / 2 + 8);
    const int label_w = max_int(22, ui_measure_text_width("20k", 1.0f) + 8);
    const int meter_w = max_int(10, body_h / 2);
    const int history_gap = max_int(0, body_h / 5);
    const int row_gap = max_int(4, body_h / 3);
    const int section_gap = max_int(8, body_h / 2);
    const int header_text_w = rect->w - pad * 2;
    const int title_y = rect->y + max_int(4, pad / 3);
    const int subtitle_y = title_y + title_h + row_gap;
    int header_h = (subtitle_y - (rect->y + pad)) + body_h + section_gap;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }

    int meter_x = rect->x + pad + label_w;
    int meter_y = rect->y + pad + header_h;
    int meter_h = rect->h - pad * 2 - header_h;
    SDL_Rect meter_rect = {meter_x, meter_y, meter_w, meter_h};
    SDL_Rect history_rect = {meter_rect.x + meter_rect.w + history_gap,
                             meter_rect.y,
                             rect->x + rect->w - pad - (meter_rect.x + meter_rect.w + history_gap),
                             meter_rect.h};
    if (history_rect.w < 0) {
        history_rect.w = 0;
    }
    if (meter_rect.h <= 0 || meter_rect.w <= 0) {
        return;
    }

    ui_draw_text_clipped(renderer,
                         rect->x + pad,
                         title_y,
                         "Spectrogram",
                         label_color,
                         1.2f,
                         header_text_w);
    ui_draw_text_clipped(renderer,
                         rect->x + pad,
                         subtitle_y,
                         "Track Output",
                         dim_color,
                         1.0f,
                         header_text_w);

    SDL_SetRenderDrawColor(renderer, history_bg.r, history_bg.g, history_bg.b, history_bg.a);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);

    SDL_SetRenderDrawColor(renderer, meter_bg.r, meter_bg.g, meter_bg.b, meter_bg.a);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    SDL_SetRenderDrawColor(renderer, axis.r, axis.g, axis.b, axis.a);
    SDL_RenderDrawLine(renderer, meter_rect.x + meter_rect.w / 2, meter_rect.y,
                       meter_rect.x + meter_rect.w / 2, meter_rect.y + meter_rect.h);

    char freq_buf[16];
    float min_hz = ENGINE_SPECTROGRAM_MIN_HZ;
    float max_hz = ENGINE_SPECTROGRAM_MAX_HZ;
    float mid_hz = sqrtf(min_hz * max_hz);
    int top_label_y = meter_rect.y;
    int mid_label_y = meter_rect.y + (meter_rect.h - body_h) / 2;
    int bot_label_y = meter_rect.y + meter_rect.h - body_h;
    format_hz_label(max_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text_clipped(renderer, rect->x + pad, top_label_y, freq_buf, dim_color, 1.0f, label_w);
    format_hz_label(mid_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text_clipped(renderer, rect->x + pad, mid_label_y, freq_buf, dim_color, 1.0f, label_w);
    format_hz_label(min_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text_clipped(renderer, rect->x + pad, bot_label_y, freq_buf, dim_color, 1.0f, label_w);

    if (!spectrogram || !frames || spectrogram->frames <= 0 || spectrogram->bins <= 0 || history_rect.h <= 0) {
        effects_meter_history_grid_draw(renderer, &history_rect, history_grid);
        ui_draw_text_clipped(renderer,
                             history_rect.x + max_int(6, pad / 3),
                             history_rect.y + max_int(6, pad / 3),
                             "No data",
                             dim_color,
                             1.0f,
                             history_rect.w - max_int(12, pad / 2));
        return;
    }

    int bins = spectrogram->bins;
    int frame_count = spectrogram->frames;
    int max_frames = ENGINE_SPECTROGRAM_HISTORY;
    float floor_db = spectrogram->db_floor;
    float ceil_db = spectrogram->db_ceil;
    if (bins > ENGINE_SPECTROGRAM_BINS || frame_count > ENGINE_SPECTROGRAM_HISTORY || bins <= 0 || frame_count < 0) {
        build_spectrogram_legacy_rgba(frames,
                                      bins,
                                      frame_count,
                                      max_frames,
                                      floor_db,
                                      ceil_db,
                                      palette_mode,
                                      g_meter_spectrogram_rgba,
                                      METER_SPECTROGRAM_RGBA_CAPACITY);
        render_spectrogram_surface(renderer,
                                   meter_spectrogram_get_surface(),
                                   &history_rect,
                                   bins,
                                   max_frames,
                                   border);
        effects_meter_history_grid_draw(renderer, &history_rect, history_grid);
        return;
    }
    size_t rgba_size = (size_t)max_frames * (size_t)bins * 4u;
    if (rgba_size > (size_t)METER_SPECTROGRAM_RGBA_CAPACITY) {
        build_spectrogram_legacy_rgba(frames,
                                      bins,
                                      frame_count,
                                      max_frames,
                                      floor_db,
                                      ceil_db,
                                      palette_mode,
                                      g_meter_spectrogram_rgba,
                                      METER_SPECTROGRAM_RGBA_CAPACITY);
        render_spectrogram_surface(renderer,
                                   meter_spectrogram_get_surface(),
                                   &history_rect,
                                   bins,
                                   max_frames,
                                   border);
        effects_meter_history_grid_draw(renderer, &history_rect, history_grid);
        return;
    }

    DawKitVizMeterSpectrogramPalette palette = DAW_KIT_VIZ_METER_SPECTROGRAM_WHITE_BLACK;
    if (palette_mode == FX_METER_SPECTROGRAM_BLACK_WHITE) {
        palette = DAW_KIT_VIZ_METER_SPECTROGRAM_BLACK_WHITE;
    } else if (palette_mode == FX_METER_SPECTROGRAM_HEAT) {
        palette = DAW_KIT_VIZ_METER_SPECTROGRAM_HEAT;
    }
    CoreResult rgba_r = daw_kit_viz_meter_build_spectrogram_rgba(frames,
                                                                 (uint32_t)frame_count,
                                                                 (uint32_t)bins,
                                                                 (uint32_t)max_frames,
                                                                 floor_db,
                                                                 ceil_db,
                                                                 palette,
                                                                 g_meter_spectrogram_rgba,
                                                                 rgba_size);
    if (rgba_r.code != CORE_OK) {
        build_spectrogram_legacy_rgba(frames,
                                      bins,
                                      frame_count,
                                      max_frames,
                                      floor_db,
                                      ceil_db,
                                      palette_mode,
                                      g_meter_spectrogram_rgba,
                                      METER_SPECTROGRAM_RGBA_CAPACITY);
    }

    SDL_Surface* surface = meter_spectrogram_get_surface();
    if (!surface) {
        return;
    }
    render_spectrogram_surface(renderer, surface, &history_rect, bins, max_frames, border);
    effects_meter_history_grid_draw(renderer, &history_rect, history_grid);
}
