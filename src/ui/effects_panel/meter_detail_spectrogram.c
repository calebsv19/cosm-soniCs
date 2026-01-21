#include "ui/effects_panel_meter_views.h"

#include "app_state.h"
#include "ui/font.h"
#include "vk_renderer.h"

#include <math.h>
#include <stdio.h>

// Clamps a value between bounds for stable color mapping.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Linearly interpolates between two colors.
static SDL_Color color_lerp(SDL_Color a, SDL_Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    SDL_Color out = {
        (Uint8)lroundf(a.r + (b.r - a.r) * t),
        (Uint8)lroundf(a.g + (b.g - a.g) * t),
        (Uint8)lroundf(a.b + (b.b - a.b) * t),
        255
    };
    return out;
}

// Maps a normalized intensity to a color palette.
static SDL_Color spectrogram_color(float t, float age_fade, int palette_mode) {
    float eased = powf(clampf(t, 0.0f, 1.0f), 0.7f);
    SDL_Color out = {236, 240, 248, 255};
    if (palette_mode == FX_METER_SPECTROGRAM_BLACK_WHITE) {
        eased = 1.0f - eased;
    }
    if (palette_mode == FX_METER_SPECTROGRAM_HEAT) {
        SDL_Color c0 = {20, 40, 90, 255};
        SDL_Color c1 = {40, 160, 120, 255};
        SDL_Color c2 = {220, 200, 80, 255};
        SDL_Color c3 = {220, 60, 40, 255};
        if (eased < 0.4f) {
            out = color_lerp(c0, c1, eased / 0.4f);
        } else if (eased < 0.7f) {
            out = color_lerp(c1, c2, (eased - 0.4f) / 0.3f);
        } else {
            out = color_lerp(c2, c3, (eased - 0.7f) / 0.3f);
        }
    } else {
        SDL_Color base = {236, 240, 248, 255};
        SDL_Color hi = {10, 14, 22, 255};
        out = color_lerp(base, hi, eased);
    }
    float r = out.r * age_fade;
    float g = out.g * age_fade;
    float b = out.b * age_fade;
    SDL_Color faded = {
        (Uint8)lroundf(clampf(r, 0.0f, 255.0f)),
        (Uint8)lroundf(clampf(g, 0.0f, 255.0f)),
        (Uint8)lroundf(clampf(b, 0.0f, 255.0f)),
        255
    };
    return faded;
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

// effects_meter_render_spectrogram draws a scrolling spectrogram heatmap.
void effects_meter_render_spectrogram(SDL_Renderer* renderer,
                                      const SDL_Rect* rect,
                                      const EngineSpectrogramSnapshot* spectrogram,
                                      const float* frames,
                                      int palette_mode,
                                      SDL_Color label_color,
                                      SDL_Color dim_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color border = {70, 75, 92, 255};
    SDL_Color fill = {22, 24, 30, 255};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 16;
    const int label_w = 22;
    const int meter_w = 10;
    const int history_gap = 0;
    int header_h = 36;
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

    ui_draw_text(renderer, rect->x + pad, rect->y + 6, "Spectrogram", label_color, 1.2f);
    ui_draw_text(renderer, rect->x + pad, rect->y + 22, "Track Output", dim_color, 1.0f);

    SDL_SetRenderDrawColor(renderer, 26, 28, 36, 255);
    SDL_RenderFillRect(renderer, &history_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);

    SDL_SetRenderDrawColor(renderer, 50, 54, 66, 255);
    SDL_RenderFillRect(renderer, &meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &meter_rect);

    SDL_SetRenderDrawColor(renderer, 130, 150, 170, 255);
    SDL_RenderDrawLine(renderer, meter_rect.x + meter_rect.w / 2, meter_rect.y,
                       meter_rect.x + meter_rect.w / 2, meter_rect.y + meter_rect.h);

    char freq_buf[16];
    float min_hz = ENGINE_SPECTROGRAM_MIN_HZ;
    float max_hz = ENGINE_SPECTROGRAM_MAX_HZ;
    float mid_hz = sqrtf(min_hz * max_hz);
    format_hz_label(max_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text(renderer, rect->x + pad, meter_rect.y - 6, freq_buf, dim_color, 1.0f);
    format_hz_label(mid_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h / 2 - 6, freq_buf, dim_color, 1.0f);
    format_hz_label(min_hz, freq_buf, sizeof(freq_buf));
    ui_draw_text(renderer, rect->x + pad, meter_rect.y + meter_rect.h - 10, freq_buf, dim_color, 1.0f);

    if (!spectrogram || !frames || spectrogram->frames <= 0 || spectrogram->bins <= 0 || history_rect.h <= 0) {
        ui_draw_text(renderer, rect->x + pad, rect->y + 40, "No data", dim_color, 1.0f);
        return;
    }

    int bins = spectrogram->bins;
    int frame_count = spectrogram->frames;
    int max_frames = ENGINE_SPECTROGRAM_HISTORY;
    float floor_db = spectrogram->db_floor;
    float ceil_db = spectrogram->db_ceil;
    float range = (ceil_db - floor_db);
    if (range <= 0.0f) {
        range = 1.0f;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, max_frames, bins, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return;
    }
    Uint32* pixels = (Uint32*)surface->pixels;
    SDL_PixelFormat* fmt = surface->format;
    for (int frame = 0; frame < max_frames; ++frame) {
        float age = max_frames > 1 ? (float)frame / (float)(max_frames - 1) : 0.0f;
        float age_fade = 1.0f - 0.35f * age;
        for (int bin = 0; bin < bins; ++bin) {
            SDL_Color c = {26, 28, 36, 255};
            if (frame < frame_count) {
                float db = frames[frame * bins + bin];
                float norm = (db - floor_db) / range;
                c = spectrogram_color(norm, age_fade, palette_mode);
            }
            int y = bins - 1 - bin;
            pixels[y * max_frames + frame] = SDL_MapRGBA(fmt, c.r, c.g, c.b, c.a);
        }
    }

    VkRendererTexture tex;
    if (vk_renderer_upload_sdl_surface_with_filter(renderer, surface, &tex, VK_FILTER_NEAREST) == VK_SUCCESS) {
        SDL_Rect src = {0, 0, max_frames, bins};
        vk_renderer_draw_texture(renderer, &tex, &src, &history_rect);
        vk_renderer_queue_texture_destroy(renderer, &tex);
    }
    SDL_FreeSurface(surface);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &history_rect);
}
