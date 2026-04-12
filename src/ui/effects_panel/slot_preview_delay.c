#include "ui/effects_panel_preview_delay.h"

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

static float preview_delay_amp(float feedback, int repeat) {
    float amp = 1.0f;
    for (int i = 0; i < repeat; ++i) {
        amp *= feedback;
    }
    return amp;
}

void effects_slot_preview_render_delay(SDL_Renderer* renderer,
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
    int base_y = plot_rect.y + plot_rect.h;
    SDL_RenderDrawLine(renderer, plot_rect.x, base_y, plot_rect.x + plot_rect.w, base_y);

    float time_ms = 400.0f;
    float feedback = 0.4f;
    float wobble_ms = 0.0f;
    float diffusion = 0.0f;
    bool pingpong = false;
    bool multitap = false;
    bool tape = false;

    switch (slot->type_id) {
        case 50u: // Delay
            time_ms = clampf(slot->param_values[0], 1.0f, 2000.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            break;
        case 51u: // PingPong
            time_ms = clampf(slot->param_values[0], 1.0f, 2000.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            pingpong = true;
            break;
        case 52u: // Echo
            time_ms = clampf(slot->param_values[0], 1.0f, 1500.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.9f);
            break;
        case 53u: // Tape
            time_ms = clampf(slot->param_values[0], 20.0f, 1500.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.97f);
            wobble_ms = clampf(slot->param_values[4], 0.0f, 12.0f);
            tape = true;
            break;
        case 54u: // Multitap
            time_ms = clampf(slot->param_values[0], 20.0f, 800.0f);
            feedback = clampf(slot->param_values[1], 0.0f, 0.95f);
            diffusion = clampf(slot->param_values[2], 0.0f, 0.9f);
            multitap = true;
            break;
        default:
            break;
    }

    float total_time = time_ms * 0.001f * 8.0f;
    if (total_time < 0.6f) total_time = 0.6f;
    if (total_time > 4.0f) total_time = 4.0f;

    float base_time = time_ms * 0.001f;
    {
        float* mins = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        float* max_main = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        float* max_dim = (float*)calloc((size_t)plot_rect.w, sizeof(float));
        KitVizVecSegment* seg_main = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
        KitVizVecSegment* seg_dim = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
        if (mins && max_main && max_dim && seg_main && seg_dim) {
            int max_repeats_accum = 12;
            for (int i = 0; i < max_repeats_accum; ++i) {
                float echo_time = base_time * (float)(i + 1);
                if (echo_time > total_time) {
                    break;
                }
                float amp = preview_delay_amp(feedback, i);
                if (amp < 0.05f) {
                    break;
                }
                int ix = (int)lroundf((echo_time / total_time) * (float)(plot_rect.w - 1));
                if (ix >= 0 && ix < plot_rect.w && amp > max_main[ix]) {
                    max_main[ix] = amp;
                }
                if (pingpong) {
                    float amp_dim = amp * 0.8f;
                    if (ix >= 0 && ix < plot_rect.w && amp_dim > max_dim[ix]) {
                        max_dim[ix] = amp_dim;
                    }
                }
                if (tape && wobble_ms > 0.1f) {
                    float offset = wobble_ms * 0.001f;
                    float x2 = (echo_time + offset) / total_time;
                    if (x2 <= 1.0f) {
                        int ix2 = (int)lroundf(x2 * (float)(plot_rect.w - 1));
                        if (ix2 >= 0 && ix2 < plot_rect.w && amp > max_dim[ix2]) {
                            max_dim[ix2] = amp;
                        }
                    }
                }
                if (diffusion > 0.01f) {
                    int smear = (int)lroundf(amp * diffusion * 4.0f);
                    for (int s = 1; s <= smear; ++s) {
                        int ixs = ix + s;
                        if (ixs >= plot_rect.w) {
                            break;
                        }
                        float amp_dim = amp * 0.5f;
                        if (amp_dim > max_dim[ixs]) {
                            max_dim[ixs] = amp_dim;
                        }
                    }
                }
            }

            if (multitap) {
                float tap2 = clampf(slot->param_values[3], 0.0f, 4.0f);
                float tap3 = clampf(slot->param_values[4], 0.0f, 4.0f);
                float tap4 = clampf(slot->param_values[5], 0.0f, 4.0f);
                float tap1_gain = clampf(slot->param_values[6], 0.0f, 1.0f);
                float tap2_gain = clampf(slot->param_values[7], 0.0f, 1.0f);
                float tap3_gain = clampf(slot->param_values[8], 0.0f, 1.0f);
                float tap4_gain = clampf(slot->param_values[9], 0.0f, 1.0f);
                float taps[4] = {1.0f, tap2, tap3, tap4};
                float gains[4] = {tap1_gain, tap2_gain, tap3_gain, tap4_gain};
                for (int i = 0; i < 4; ++i) {
                    float tap_time = base_time * taps[i];
                    if (tap_time <= 0.0f || tap_time > total_time) {
                        continue;
                    }
                    int ix = (int)lroundf((tap_time / total_time) * (float)(plot_rect.w - 1));
                    if (ix >= 0 && ix < plot_rect.w && gains[i] > max_dim[ix]) {
                        max_dim[ix] = gains[i];
                    }
                }
            }

            size_t seg_count_main = 0;
            size_t seg_count_dim = 0;
            CoreResult main_r = daw_kit_viz_plot_envelope_from_min_max(mins,
                                                                        max_main,
                                                                        (uint32_t)plot_rect.w,
                                                                        &plot_rect,
                                                                        (DawKitVizPlotRange){0.0f, 1.0f},
                                                                        seg_main,
                                                                        (size_t)plot_rect.w,
                                                                        &seg_count_main);
            CoreResult dim_r = daw_kit_viz_plot_envelope_from_min_max(mins,
                                                                       max_dim,
                                                                       (uint32_t)plot_rect.w,
                                                                       &plot_rect,
                                                                       (DawKitVizPlotRange){0.0f, 1.0f},
                                                                       seg_dim,
                                                                       (size_t)plot_rect.w,
                                                                       &seg_count_dim);
            if (main_r.code == CORE_OK && dim_r.code == CORE_OK) {
                preview_render_segments_thick(renderer, seg_main, seg_count_main, line);
                preview_render_segments_thick(renderer, seg_dim, seg_count_dim, line_dim);
                free(seg_dim);
                free(seg_main);
                free(max_dim);
                free(max_main);
                free(mins);
                return;
            }
        }
        free(seg_dim);
        free(seg_main);
        free(max_dim);
        free(max_main);
        free(mins);
    }

    int max_repeats = 12;
    for (int i = 0; i < max_repeats; ++i) {
        float echo_time = base_time * (float)(i + 1);
        if (echo_time > total_time) {
            break;
        }
        float amp = preview_delay_amp(feedback, i);
        if (amp < 0.05f) {
            break;
        }
        float x_norm = echo_time / total_time;
        int px = plot_rect.x + (int)lroundf(x_norm * (float)plot_rect.w);
        int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
        preview_draw_line_thick(renderer, px, base_y, px, py, line);

        if (pingpong) {
            int py_alt = base_y - (int)lroundf(amp * 0.8f * (float)plot_rect.h);
            preview_draw_line_thick(renderer, px, base_y, px, py_alt, line_dim);
        }
        if (tape && wobble_ms > 0.1f) {
            float offset = wobble_ms * 0.001f;
            float x2 = (echo_time + offset) / total_time;
            if (x2 <= 1.0f) {
                int px2 = plot_rect.x + (int)lroundf(x2 * (float)plot_rect.w);
                preview_draw_line_thick(renderer, px2, base_y, px2, py, line_dim);
            }
        }
        if (diffusion > 0.01f) {
            int smear = (int)lroundf(amp * diffusion * 4.0f);
            for (int s = 1; s <= smear; ++s) {
                int pxs = px + s;
                if (pxs >= plot_rect.x + plot_rect.w) {
                    break;
                }
                preview_draw_line_thick(renderer, pxs, base_y, pxs, base_y - (py - base_y) / 2, line_dim);
            }
        }
    }

    if (multitap) {
        float tap2 = clampf(slot->param_values[3], 0.0f, 4.0f);
        float tap3 = clampf(slot->param_values[4], 0.0f, 4.0f);
        float tap4 = clampf(slot->param_values[5], 0.0f, 4.0f);
        float tap1_gain = clampf(slot->param_values[6], 0.0f, 1.0f);
        float tap2_gain = clampf(slot->param_values[7], 0.0f, 1.0f);
        float tap3_gain = clampf(slot->param_values[8], 0.0f, 1.0f);
        float tap4_gain = clampf(slot->param_values[9], 0.0f, 1.0f);
        float taps[4] = {1.0f, tap2, tap3, tap4};
        float gains[4] = {tap1_gain, tap2_gain, tap3_gain, tap4_gain};
        for (int i = 0; i < 4; ++i) {
            float tap_time = base_time * taps[i];
            if (tap_time <= 0.0f || tap_time > total_time) {
                continue;
            }
            float amp = gains[i];
            int px = plot_rect.x + (int)lroundf((tap_time / total_time) * (float)plot_rect.w);
            int py = base_y - (int)lroundf(amp * (float)plot_rect.h);
            preview_draw_line_thick(renderer, px, base_y, px, py, line_dim);
        }
    }
}
