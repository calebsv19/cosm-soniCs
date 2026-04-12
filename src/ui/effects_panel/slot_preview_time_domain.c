#include "ui/effects_panel_preview_time_domain.h"

#include "ui/font.h"
#include "ui/kit_viz_fx_preview_adapter.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdlib.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void resolve_preview_shell_theme(SDL_Color* bg,
                                        SDL_Color* plot_bg,
                                        SDL_Color* border,
                                        SDL_Color* grid) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (bg) {
            *bg = theme.control_fill;
        }
        if (plot_bg) {
            *plot_bg = theme.timeline_fill;
        }
        if (border) {
            *border = theme.pane_border;
        }
        if (grid) {
            *grid = theme.grid_major;
        }
        return;
    }
    if (bg) {
        *bg = (SDL_Color){22, 24, 30, 255};
    }
    if (plot_bg) {
        *plot_bg = (SDL_Color){26, 28, 36, 255};
    }
    if (border) {
        *border = (SDL_Color){70, 76, 92, 255};
    }
    if (grid) {
        *grid = (SDL_Color){50, 54, 66, 255};
    }
}

static void resolve_preview_line_theme(SDL_Color* line,
                                       SDL_Color* line_alt,
                                       SDL_Color* line_dim,
                                       SDL_Color* axis) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (line) {
            *line = theme.slider_handle;
            line->a = 255;
        }
        if (line_alt) {
            *line_alt = theme.control_active_fill;
            line_alt->a = 230;
        }
        if (line_dim) {
            *line_dim = theme.control_border;
            line_dim->a = 220;
        }
        if (axis) {
            *axis = theme.grid_major;
            axis->a = 200;
        }
        return;
    }
    if (line) {
        *line = (SDL_Color){110, 190, 240, 220};
    }
    if (line_alt) {
        *line_alt = (SDL_Color){130, 120, 220, 200};
    }
    if (line_dim) {
        *line_dim = (SDL_Color){120, 120, 200, 180};
    }
    if (axis) {
        *axis = (SDL_Color){90, 100, 120, 200};
    }
}

static void preview_draw_line_thick(SDL_Renderer* renderer,
                                    int x0,
                                    int y0,
                                    int x1,
                                    int y1,
                                    SDL_Color color) {
    if (!renderer) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx >= dy) {
        SDL_RenderDrawLine(renderer, x0, y0 + 1, x1, y1 + 1);
    } else {
        SDL_RenderDrawLine(renderer, x0 + 1, y0, x1 + 1, y1);
    }
}

static void preview_render_segments_thick(SDL_Renderer* renderer,
                                          const KitVizVecSegment* segments,
                                          size_t segment_count,
                                          SDL_Color color) {
    if (!renderer || !segments || segment_count == 0) {
        return;
    }
    for (size_t i = 0; i < segment_count; ++i) {
        const KitVizVecSegment* s = &segments[i];
        preview_draw_line_thick(renderer,
                                (int)lroundf(s->x0),
                                (int)lroundf(s->y0),
                                (int)lroundf(s->x1),
                                (int)lroundf(s->y1),
                                color);
    }
}

static float preview_lfo_sample(float phase, float shape) {
    float s = sinf(phase * 2.0f * 3.14159265358979323846f);
    float tri = 2.0f * fabsf(2.0f * (phase - floorf(phase + 0.5f))) - 1.0f;
    float sq = phase < 0.5f ? 1.0f : -1.0f;
    float blend = clampf(shape, 0.0f, 1.0f);
    float mix1 = (1.0f - blend) * s + blend * tri;
    return (1.0f - blend) * mix1 + blend * sq;
}

static float preview_rate_cycles(float rate_hz) {
    float cycles = rate_hz * 0.8f;
    if (cycles < 1.0f) cycles = 1.0f;
    if (cycles > 8.0f) cycles = 8.0f;
    return cycles;
}

static float preview_envelope_from_rt60(float t_sec, float rt60_sec) {
    if (rt60_sec <= 0.0001f) {
        return 0.0f;
    }
    float db = -60.0f * (t_sec / rt60_sec);
    return clampf(powf(10.0f, db / 20.0f), 0.0f, 1.0f);
}

static float preview_mod_ms_value(float base_ms, float depth_ms, float phase, float max_range) {
    float mod = preview_lfo_sample(phase, 0.4f);
    float value = base_ms + mod * depth_ms;
    return clampf(value / max_range, 0.0f, 1.0f);
}

void effects_slot_preview_render_lfo(SDL_Renderer* renderer,
                                     const FxSlotUIState* slot,
                                     const SDL_Rect* rect,
                                     SDL_Color label_color,
                                     const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {0};
    SDL_Color plot_bg = {0};
    SDL_Color border = {0};
    SDL_Color line = {0};
    SDL_Color grid = {0};
    resolve_preview_shell_theme(&bg, &plot_bg, &border, &grid);
    resolve_preview_line_theme(&line, NULL, NULL, NULL);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, plot_bg.r, plot_bg.g, plot_bg.b, plot_bg.a);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);

    float rate_hz = 1.0f;
    float depth = 1.0f;
    float shape = 0.5f;
    float base_ms = 0.0f;
    float depth_ms = 0.0f;
    float max_range = 1.0f;
    float phase_offset = 0.0f;

    switch (slot->type_id) {
        case 70u:
            rate_hz = clampf(slot->param_values[0], 0.1f, 20.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = clampf(slot->param_values[2], 0.0f, 1.0f);
            break;
        case 71u:
            rate_hz = clampf(slot->param_values[0], 0.05f, 5.0f);
            depth_ms = clampf(slot->param_values[1], 0.0f, 15.0f);
            base_ms = clampf(slot->param_values[2], 5.0f, 25.0f);
            max_range = 40.0f;
            break;
        case 72u:
            rate_hz = clampf(slot->param_values[0], 0.05f, 5.0f);
            depth_ms = clampf(slot->param_values[1], 0.1f, 5.0f);
            base_ms = clampf(slot->param_values[2], 0.1f, 5.0f);
            max_range = 10.0f;
            break;
        case 73u:
            rate_hz = clampf(slot->param_values[0], 0.1f, 10.0f);
            depth_ms = clampf(slot->param_values[1], 0.1f, 8.0f);
            base_ms = clampf(slot->param_values[2], 2.0f, 12.0f);
            max_range = 20.0f;
            break;
        case 74u:
            rate_hz = clampf(slot->param_values[0], 1.0f, 5000.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = 0.1f;
            break;
        case 75u:
            rate_hz = clampf(slot->param_values[0], 0.01f, 10.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            phase_offset = clampf(slot->param_values[2], 0.0f, 360.0f) / 360.0f;
            break;
        case 76u:
            rate_hz = clampf(slot->param_values[0], 0.05f, 3.0f);
            depth = clampf(slot->param_values[1], 0.0f, 1.0f);
            shape = 0.2f;
            break;
        default:
            break;
    }

    float cycles = preview_rate_cycles(rate_hz);
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float phase = t * cycles + phase_offset;
        float value = 0.0f;
        if (slot->type_id == 71u || slot->type_id == 72u || slot->type_id == 73u) {
            value = preview_mod_ms_value(base_ms, depth_ms, phase, max_range);
        } else {
            value = preview_lfo_sample(phase, shape) * depth;
        }
        if (slot->type_id == 71u || slot->type_id == 72u || slot->type_id == 73u) {
            value = (value - 0.5f) * 2.0f;
        }
        y_samples[i] = value;
    }

    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    if (!segments) {
        free(y_samples);
        return;
    }
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)plot_rect.w,
                                                                   &plot_rect,
                                                                   (DawKitVizPlotRange){-1.0f, 1.0f},
                                                                   segments,
                                                                   (size_t)plot_rect.w,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        preview_render_segments_thick(renderer, segments, segment_count, line);
        free(segments);
        free(y_samples);
        return;
    }

    int prev_x = plot_rect.x;
    int prev_y = mid_y;
    for (int i = 0; i < plot_rect.w; ++i) {
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h / 2 - (int)lroundf(y_samples[i] * 0.5f * (float)plot_rect.h);
        if (i > 0) {
            preview_draw_line_thick(renderer, prev_x, prev_y, px, py, line);
        }
        prev_x = px;
        prev_y = py;
    }
    free(segments);
    free(y_samples);
}

void effects_slot_preview_render_reverb(SDL_Renderer* renderer,
                                        const FxSlotUIState* slot,
                                        const SDL_Rect* rect,
                                        SDL_Color label_color,
                                        const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {0};
    SDL_Color plot_bg = {0};
    SDL_Color border = {0};
    SDL_Color line = {0};
    SDL_Color line_dim = {0};
    SDL_Color grid = {0};
    resolve_preview_shell_theme(&bg, &plot_bg, &border, &grid);
    resolve_preview_line_theme(&line, NULL, &line_dim, NULL);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {
        rect->x + pad,
        rect->y + pad + header_h,
        rect->w - pad * 2,
        rect->h - pad * 2 - header_h
    };
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, plot_bg.r, plot_bg.g, plot_bg.b, plot_bg.a);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        int text_y = rect->y + 4;
        ui_draw_text(renderer, rect->x + pad, text_y, title, label_color, 1.0f);
    }

    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    float rt60 = 1.5f;
    float damping = 0.4f;
    float predelay_ms = 0.0f;
    float gate_thresh = -24.0f;
    float gate_hold_ms = 0.0f;
    float gate_release_ms = 120.0f;
    bool gated = false;
    bool early = false;
    bool plate = false;

    switch (slot->type_id) {
        case 90u:
            rt60 = clampf(slot->param_values[1], 0.1f, 10.0f);
            damping = clampf(slot->param_values[2], 0.0f, 1.0f);
            predelay_ms = clampf(slot->param_values[3], 0.0f, 100.0f);
            break;
        case 91u:
            predelay_ms = clampf(slot->param_values[0], 0.0f, 100.0f);
            early = true;
            break;
        case 92u: {
            rt60 = clampf(slot->param_values[1], 0.2f, 8.0f);
            float highcut = clampf(slot->param_values[2], 2000.0f, 16000.0f);
            damping = clampf(1.0f - highcut / 16000.0f, 0.0f, 1.0f);
            predelay_ms = clampf(slot->param_values[3], 0.0f, 120.0f);
            plate = true;
            break;
        }
        case 93u:
            rt60 = clampf(slot->param_values[1], 0.2f, 4.0f);
            gate_thresh = clampf(slot->param_values[2], -60.0f, 0.0f);
            gate_hold_ms = clampf(slot->param_values[3], 0.0f, 500.0f);
            gate_release_ms = clampf(slot->param_values[4], 5.0f, 800.0f);
            gated = true;
            break;
        default:
            break;
    }

    float predelay_sec = predelay_ms * 0.001f;
    float total_time = rt60 * 1.2f + predelay_sec;
    if (gated) {
        total_time = predelay_sec + (gate_hold_ms + gate_release_ms) * 0.001f + 0.3f;
    }
    if (early) {
        total_time = predelay_sec + 0.4f;
    }
    if (total_time < 0.5f) total_time = 0.5f;
    if (total_time > 6.0f) total_time = 6.0f;

    int base_y = plot_rect.y + plot_rect.h;
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float time = t * total_time;
        float env = 0.0f;
        if (time < predelay_sec) {
            env = 0.0f;
        } else {
            float tail_t = time - predelay_sec;
            env = preview_envelope_from_rt60(tail_t, rt60);
            float damp_rt60 = fmaxf(0.2f, rt60 * (1.0f - damping * 0.7f));
            float env_dim = preview_envelope_from_rt60(tail_t, damp_rt60);
            if (plate) {
                env = (env + env_dim * 0.8f) * 0.5f;
            } else {
                env = env * 0.6f + env_dim * 0.4f;
            }
            if (early) {
                env *= expf(-tail_t * 6.0f);
            }
            if (gated) {
                float hold = gate_hold_ms * 0.001f;
                float release = gate_release_ms * 0.001f;
                float gate_time = tail_t;
                float gate_env = 1.0f;
                if (gate_time > hold) {
                    float rel_t = gate_time - hold;
                    gate_env = expf(-rel_t / fmaxf(release, 0.02f));
                }
                float gate_scale = (gate_thresh + 60.0f) / 60.0f;
                env = env * fmaxf(gate_env, gate_scale * 0.2f);
            }
        }
        y_samples[i] = env;
    }

    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    if (!segments) {
        free(y_samples);
        return;
    }
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)plot_rect.w,
                                                                   &plot_rect,
                                                                   (DawKitVizPlotRange){0.0f, 1.0f},
                                                                   segments,
                                                                   (size_t)plot_rect.w,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        preview_render_segments_thick(renderer, segments, segment_count, line);
    } else {
        int prev_x = plot_rect.x;
        int prev_y = base_y;
        for (int i = 0; i < plot_rect.w; ++i) {
            int px = plot_rect.x + i;
            int py = base_y - (int)lroundf(y_samples[i] * (float)plot_rect.h);
            if (i > 0) {
                preview_draw_line_thick(renderer, prev_x, prev_y, px, py, line);
            }
            prev_x = px;
            prev_y = py;
        }
    }
    free(segments);
    free(y_samples);

    if (early) {
        int tap_count = 6;
        for (int i = 0; i < tap_count; ++i) {
            float tap_time = predelay_sec + 0.03f * (float)i;
            float amp = 1.0f - 0.12f * (float)i;
            if (tap_time > total_time) {
                break;
            }
            int px = plot_rect.x + (int)lroundf((tap_time / total_time) * (float)plot_rect.w);
            int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
            preview_draw_line_thick(renderer, px, base_y, px, py, line_dim);
        }
    }
}
