#include "ui/effects_panel_preview_eq_curve.h"

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

static float db_to_lin(float db) {
    return powf(10.0f, db * 0.05f);
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

typedef struct PreviewBiquadCoeffs {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} PreviewBiquadCoeffs;

static float preview_db_to_lin(float db) {
    return powf(10.0f, db * 0.05f);
}

static float preview_biquad_mag(const PreviewBiquadCoeffs* bq, float w) {
    float cw = cosf(w);
    float sw = sinf(w);
    float c2 = cosf(2.0f * w);
    float s2 = sinf(2.0f * w);
    float num_re = bq->b0 + bq->b1 * cw + bq->b2 * c2;
    float num_im = -bq->b1 * sw - bq->b2 * s2;
    float den_re = 1.0f + bq->a1 * cw + bq->a2 * c2;
    float den_im = -bq->a1 * sw - bq->a2 * s2;
    float num = num_re * num_re + num_im * num_im;
    float den = den_re * den_re + den_im * den_im;
    if (den <= 0.0f) {
        return 0.0f;
    }
    return sqrtf(num / den);
}

static PreviewBiquadCoeffs preview_make_peaking(float sr, float freq, float q, float gain_db) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha / A;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_low_shelf(float sr, float freq, float gain_db, float slope) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = 2.0f * A * ((Am - Ap * cosw0));
    const float b2 = A * ((Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = -2.0f * ((Am + Ap * cosw0));
    const float a2 = (Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_low_shelf_half_db(float sr, float freq, float gain_db, float slope) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = 2.0f * A * ((Am - Ap * cosw0));
    const float b2 = A * ((Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = -2.0f * ((Am + Ap * cosw0));
    const float a2 = (Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_high_shelf(float sr, float freq, float gain_db, float slope) {
    const float A = preview_db_to_lin(gain_db);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = -2.0f * A * ((Am + Ap * cosw0));
    const float b2 = A * ((Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = 2.0f * ((Am - Ap * cosw0));
    const float a2 = (Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_high_shelf_half_db(float sr, float freq, float gain_db, float slope) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float Ap = A + 1.0f;
    const float Am = A - 1.0f;
    const float b0 = A * ((Ap + Am * cosw0) + 2.0f * sqrtf(A) * alpha);
    const float b1 = -2.0f * A * ((Am + Ap * cosw0));
    const float b2 = A * ((Ap + Am * cosw0) - 2.0f * sqrtf(A) * alpha);
    const float a0 = (Ap - Am * cosw0) + 2.0f * sqrtf(A) * alpha;
    const float a1 = 2.0f * ((Am - Ap * cosw0));
    const float a2 = (Ap - Am * cosw0) - 2.0f * sqrtf(A) * alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_notch(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = 1.0f;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_lowpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = (1.0f - cosw0) / 2.0f;
    const float b1 = 1.0f - cosw0;
    const float b2 = (1.0f - cosw0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_highpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = (1.0f + cosw0) / 2.0f;
    const float b1 = -(1.0f + cosw0);
    const float b2 = (1.0f + cosw0) / 2.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static PreviewBiquadCoeffs preview_make_bandpass(float sr, float freq, float q) {
    const float w0 = 2.0f * 3.14159265358979323846f * freq / sr;
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;
    PreviewBiquadCoeffs out = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    return out;
}

static float effects_slot_preview_eq_mag(FxTypeId type_id,
                                         const FxSlotUIState* slot,
                                         float freq_hz,
                                         float sample_rate,
                                         float sweep_min_hint) {
    if (!slot || sample_rate <= 0.0f) {
        return 1.0f;
    }
    float mag = 1.0f;
    float q = 0.707f;
    float freq = freq_hz;
    float w = 2.0f * 3.14159265358979323846f * freq_hz / sample_rate;
    switch (type_id) {
        case 30u: {
            float low_gain = clampf(slot->param_values[0], -15.0f, 15.0f);
            float mid_gain = clampf(slot->param_values[1], -15.0f, 15.0f);
            float high_gain = clampf(slot->param_values[2], -15.0f, 15.0f);
            q = clampf(slot->param_values[3], 0.3f, 4.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf(sample_rate, 100.0f, low_gain, 1.0f);
            PreviewBiquadCoeffs mid = preview_make_peaking(sample_rate, 1000.0f, q, mid_gain);
            PreviewBiquadCoeffs high = preview_make_high_shelf(sample_rate, 8000.0f, high_gain, 1.0f);
            mag = preview_biquad_mag(&low, w) * preview_biquad_mag(&mid, w) * preview_biquad_mag(&high, w);
            break;
        }
        case 31u: {
            freq = clampf(slot->param_values[0], 40.0f, 18000.0f);
            q = clampf(slot->param_values[1], 1.0f, 30.0f);
            float depth_db = clampf(slot->param_values[2], -30.0f, 0.0f);
            PreviewBiquadCoeffs notch = preview_make_notch(sample_rate, freq, q);
            PreviewBiquadCoeffs cut = preview_make_peaking(sample_rate, freq, q, depth_db);
            mag = preview_biquad_mag(&notch, w) * preview_biquad_mag(&cut, w);
            break;
        }
        case 32u: {
            float tilt_db = clampf(slot->param_values[0], -12.0f, 12.0f);
            float pivot = clampf(slot->param_values[1], 200.0f, 2000.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf(sample_rate, pivot, -tilt_db * 0.5f, 1.0f);
            PreviewBiquadCoeffs high = preview_make_high_shelf(sample_rate, pivot, tilt_db * 0.5f, 1.0f);
            mag = preview_biquad_mag(&low, w) * preview_biquad_mag(&high, w);
            break;
        }
        case 33u: {
            int ftype = (int)slot->param_values[0];
            float cutoff = clampf(slot->param_values[1], 20.0f, sample_rate * 0.45f);
            q = clampf(slot->param_values[2], 0.3f, 12.0f);
            float gain_db = clampf(slot->param_values[3], -24.0f, 24.0f);
            PreviewBiquadCoeffs bq;
            switch (ftype) {
                case 1: bq = preview_make_highpass(sample_rate, cutoff, q); break;
                case 2: bq = preview_make_bandpass(sample_rate, cutoff, q); break;
                case 3: bq = preview_make_notch(sample_rate, cutoff, q); break;
                default: bq = preview_make_lowpass(sample_rate, cutoff, q); break;
            }
            mag = preview_biquad_mag(&bq, w) * preview_db_to_lin(gain_db);
            break;
        }
        case 40u: {
            float f = clampf(slot->param_values[1], 200.0f, 4000.0f);
            q = clampf(slot->param_values[2], 0.5f, 8.0f);
            float mix = clampf(slot->param_values[5], 0.0f, 1.0f);
            PreviewBiquadCoeffs bp = preview_make_bandpass(sample_rate, f, q);
            float wet = preview_biquad_mag(&bp, w);
            mag = (1.0f - mix) + wet * mix;
            break;
        }
        case 41u: {
            float pivot = clampf(slot->param_values[0], 100.0f, 5000.0f);
            float tilt_db = clampf(slot->param_values[1], -12.0f, 12.0f);
            float mix = clampf(slot->param_values[2], 0.0f, 1.0f);
            PreviewBiquadCoeffs low = preview_make_low_shelf_half_db(sample_rate, pivot, -tilt_db * 0.5f, 0.5f);
            PreviewBiquadCoeffs high = preview_make_high_shelf_half_db(sample_rate, pivot, tilt_db * 0.5f, 0.5f);
            float wet = preview_biquad_mag(&low, w) * preview_biquad_mag(&high, w);
            mag = (1.0f - mix) + wet * mix;
            break;
        }
        case 43u: {
            float sweep_min = sweep_min_hint > 0.0f ? sweep_min_hint : clampf(slot->param_values[0], 200.0f, 2000.0f);
            float sweep_max = clampf(slot->param_values[1], sweep_min + 10.0f, 6000.0f);
            float depth_db = clampf(slot->param_values[2], -12.0f, 12.0f);
            float rate = clampf(slot->param_values[3], 0.1f, 5.0f);
            float phase = (rate > 0.0f) ? sweep_min + (sweep_max - sweep_min) * 0.5f : sweep_min;
            float f = phase;
            PreviewBiquadCoeffs bq = preview_make_peaking(sample_rate, f, 0.7f, depth_db);
            mag = preview_biquad_mag(&bq, w);
            break;
        }
        default:
            break;
    }
    return mag;
}

static float effects_slot_preview_curve_eval(FxTypeId type_id, const FxSlotUIState* slot, float x) {
    if (!slot) {
        return x;
    }
    switch (type_id) {
        case 60u: {
            float thr_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[2];
            float pre = db_to_lin(-thr_db);
            float post = db_to_lin(out_db);
            float y = clampf(x * pre, -1.0f, 1.0f);
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 61u: {
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[2];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float y = tanhf(x * pre);
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 62u: {
            float bits = slot->param_values[0];
            float mix = slot->param_values[3];
            int qbits = (int)lroundf(clampf(bits, 4.0f, 16.0f));
            float q = (qbits >= 16) ? x : roundf(x * (float)(1 << (qbits - 1))) / (float)(1 << (qbits - 1));
            q = clampf(q, -1.0f, 1.0f);
            return (1.0f - mix) * x + mix * q;
        }
        case 63u: {
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[4];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float y = tanhf(x * pre) * 0.8f + tanhf(x * pre * 0.3f) * 0.2f;
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 64u: {
            float drive_db = slot->param_values[0];
            float out_db = slot->param_values[1];
            float mix = slot->param_values[4];
            float shape = slot->param_values[3];
            float bias = slot->param_values[2];
            float pre = db_to_lin(drive_db);
            float post = db_to_lin(out_db);
            float d = x * pre + bias;
            float y = d;
            switch ((int)shape) {
                case 0: y = tanhf(d); break;
                case 1: y = atanf(d) * 0.63662f; break;
                case 2: y = d / (1.0f + fabsf(d)); break;
                case 3:
                    if (d > 1.0f) y = 2.0f - d;
                    else if (d < -1.0f) y = -2.0f - d;
                    break;
                default: y = d; break;
            }
            y *= post;
            return (1.0f - mix) * x + mix * y;
        }
        case 65u: {
            float bit_depth = slot->param_values[1];
            float mix = slot->param_values[4];
            int bits = (int)lroundf(clampf(bit_depth, 4.0f, 24.0f));
            float q = (bits >= 24) ? x : roundf(x * (float)(1 << (bits - 1))) / (float)(1 << (bits - 1));
            q = clampf(q, -1.0f, 1.0f);
            return (1.0f - mix) * x + mix * q;
        }
        default:
            break;
    }
    return x;
}

static int effects_slot_preview_quant_steps(FxTypeId type_id, const FxSlotUIState* slot) {
    if (!slot) {
        return 0;
    }
    if (type_id == 62u) {
        float bits = slot->param_values[0];
        int qbits = (int)lroundf(clampf(bits, 4.0f, 16.0f));
        int steps = 1 << qbits;
        if (steps > 64) steps = 64;
        return steps;
    }
    if (type_id == 65u) {
        float bits = slot->param_values[1];
        int qbits = (int)lroundf(clampf(bits, 4.0f, 24.0f));
        int steps = 1 << qbits;
        if (steps > 64) steps = 64;
        return steps;
    }
    return 0;
}

void effects_slot_preview_render_curve(SDL_Renderer* renderer,
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
    SDL_Color unity = {0};
    resolve_preview_shell_theme(&bg, &plot_bg, &border, &grid);
    resolve_preview_line_theme(&line, NULL, NULL, &unity);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {rect->x + pad, rect->y + pad + header_h, rect->w - pad * 2, rect->h - pad * 2 - header_h};
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, plot_bg.r, plot_bg.g, plot_bg.b, plot_bg.a);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        ui_draw_text(renderer, rect->x + pad, rect->y + 4, title, label_color, 1.0f);
    }
    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    int mid_x = plot_rect.x + plot_rect.w / 2;
    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);
    SDL_RenderDrawLine(renderer, mid_x, plot_rect.y, mid_x, plot_rect.y + plot_rect.h);
    SDL_SetRenderDrawColor(renderer, unity.r, unity.g, unity.b, unity.a);
    SDL_RenderDrawLine(renderer, plot_rect.x, plot_rect.y + plot_rect.h, plot_rect.x + plot_rect.w, plot_rect.y);

    int steps = effects_slot_preview_quant_steps(slot->type_id, slot);
    float* y_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    if (!y_samples) {
        return;
    }
    for (int i = 0; i < plot_rect.w; ++i) {
        float x = ((float)i / (float)(plot_rect.w - 1)) * 2.0f - 1.0f;
        float y = effects_slot_preview_curve_eval(slot->type_id, slot, x);
        if (steps > 0) {
            y = roundf(y * (float)(steps - 1)) / (float)(steps - 1);
        }
        y_samples[i] = y;
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
    int prev_y = plot_rect.y + plot_rect.h;
    for (int i = 0; i < plot_rect.w; ++i) {
        float y = y_samples[i];
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h - (int)lroundf((y + 1.0f) * 0.5f * (float)plot_rect.h);
        if (i > 0) {
            preview_draw_line_thick(renderer, prev_x, prev_y, px, py, line);
        }
        prev_x = px;
        prev_y = py;
    }
    free(segments);
    free(y_samples);
}

void effects_slot_preview_render_eq(SDL_Renderer* renderer,
                                    const FxSlotUIState* slot,
                                    const SDL_Rect* rect,
                                    SDL_Color label_color,
                                    float sample_rate,
                                    const char* title) {
    if (!renderer || !slot || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {0};
    SDL_Color plot_bg = {0};
    SDL_Color border = {0};
    SDL_Color line = {0};
    SDL_Color line_alt = {0};
    SDL_Color grid = {0};
    SDL_Color zero = {0};
    resolve_preview_shell_theme(&bg, &plot_bg, &border, &grid);
    resolve_preview_line_theme(&line, &line_alt, NULL, &zero);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    const int pad = 8;
    int header_h = 16;
    if (header_h > rect->h - pad * 2) {
        header_h = rect->h - pad * 2;
    }
    SDL_Rect plot_rect = {rect->x + pad, rect->y + pad + header_h, rect->w - pad * 2, rect->h - pad * 2 - header_h};
    if (plot_rect.w < 0) plot_rect.w = 0;
    if (plot_rect.h < 0) plot_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, plot_bg.r, plot_bg.g, plot_bg.b, plot_bg.a);
    SDL_RenderFillRect(renderer, &plot_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &plot_rect);

    if (title) {
        ui_draw_text(renderer, rect->x + pad, rect->y + 4, title, label_color, 1.0f);
    }
    if (plot_rect.w <= 1 || plot_rect.h <= 1) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    int mid_y = plot_rect.y + plot_rect.h / 2;
    SDL_SetRenderDrawColor(renderer, zero.r, zero.g, zero.b, zero.a);
    SDL_RenderDrawLine(renderer, plot_rect.x, mid_y, plot_rect.x + plot_rect.w, mid_y);

    float sweep_min = 0.0f;
    float sweep_max = 0.0f;
    bool dual_curve = false;
    if (slot->type_id == 40u) {
        sweep_min = clampf(slot->param_values[0], 200.0f, 2000.0f);
        sweep_max = clampf(slot->param_values[1], sweep_min + 10.0f, 6000.0f);
        dual_curve = true;
    }

    float* db_samples = (float*)malloc((size_t)plot_rect.w * sizeof(float));
    float* db_samples_alt = dual_curve ? (float*)malloc((size_t)plot_rect.w * sizeof(float)) : NULL;
    if (!db_samples || (dual_curve && !db_samples_alt)) {
        free(db_samples);
        free(db_samples_alt);
        return;
    }

    for (int i = 0; i < plot_rect.w; ++i) {
        float t = (float)i / (float)(plot_rect.w - 1);
        float log_min = log10f(20.0f);
        float log_max = log10f(sample_rate * 0.45f);
        float log_f = log_min + t * (log_max - log_min);
        float freq = powf(10.0f, log_f);
        float mag = effects_slot_preview_eq_mag(slot->type_id, slot, freq, sample_rate, dual_curve ? sweep_max : 0.0f);
        float db = 20.0f * log10f(fmaxf(mag, 1e-4f));
        db_samples[i] = db;
        if (dual_curve) {
            float mag_alt = effects_slot_preview_eq_mag(slot->type_id, slot, freq, sample_rate, sweep_min);
            db_samples_alt[i] = 20.0f * log10f(fmaxf(mag_alt, 1e-4f));
        }
    }

    DawKitVizPlotRange range = {-24.0f, 24.0f};
    KitVizVecSegment* segments = (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment));
    KitVizVecSegment* segments_alt = dual_curve ? (KitVizVecSegment*)malloc((size_t)plot_rect.w * sizeof(KitVizVecSegment)) : NULL;
    if (!segments || (dual_curve && !segments_alt)) {
        free(db_samples);
        free(db_samples_alt);
        free(segments);
        free(segments_alt);
        return;
    }

    size_t segment_count = 0;
    CoreResult r_main = daw_kit_viz_plot_line_from_y_samples(db_samples,
                                                             (uint32_t)plot_rect.w,
                                                             &plot_rect,
                                                             range,
                                                             segments,
                                                             (size_t)plot_rect.w,
                                                             &segment_count);
    CoreResult r_alt = core_result_ok();
    size_t segment_count_alt = 0;
    if (dual_curve) {
        r_alt = daw_kit_viz_plot_line_from_y_samples(db_samples_alt,
                                                     (uint32_t)plot_rect.w,
                                                     &plot_rect,
                                                     range,
                                                     segments_alt,
                                                     (size_t)plot_rect.w,
                                                     &segment_count_alt);
    }
    if (r_main.code == CORE_OK && (!dual_curve || r_alt.code == CORE_OK)) {
        preview_render_segments_thick(renderer, segments, segment_count, line);
        if (dual_curve) {
            preview_render_segments_thick(renderer, segments_alt, segment_count_alt, line_alt);
        }
        free(db_samples);
        free(db_samples_alt);
        free(segments);
        free(segments_alt);
        return;
    }

    int prev_x = plot_rect.x;
    int prev_y = mid_y;
    int prev_y_alt = mid_y;
    for (int i = 0; i < plot_rect.w; ++i) {
        float y_norm = clampf((db_samples[i] + 24.0f) / 48.0f, 0.0f, 1.0f);
        int px = plot_rect.x + i;
        int py = plot_rect.y + plot_rect.h - (int)lroundf(y_norm * (float)plot_rect.h);
        int old_prev_x = prev_x;
        if (i > 0) {
            preview_draw_line_thick(renderer, prev_x, prev_y, px, py, line);
        }
        if (dual_curve) {
            float y_alt = clampf((db_samples_alt[i] + 24.0f) / 48.0f, 0.0f, 1.0f);
            int py_alt = plot_rect.y + plot_rect.h - (int)lroundf(y_alt * (float)plot_rect.h);
            if (i > 0) {
                preview_draw_line_thick(renderer, old_prev_x, prev_y_alt, px, py_alt, line_alt);
            }
            prev_y_alt = py_alt;
        }
        prev_x = px;
        prev_y = py;
    }
    free(db_samples);
    free(db_samples_alt);
    free(segments);
    free(segments_alt);
}
