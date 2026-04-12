#include "ui/effects_panel_preview.h"
#include "ui/effects_panel_preview_delay.h"
#include "ui/effects_panel_preview_eq_curve.h"
#include "ui/effects_panel_preview_time_domain.h"
#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/kit_viz_fx_preview_adapter.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>

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

// effects_slot_preview_has_gr returns true if the FX type emits gain reduction scope samples.
bool effects_slot_preview_has_gr(FxTypeId type_id) {
    return type_id == 20u || type_id == 21u || type_id == 22u || type_id == 23u
        || type_id == 24u || type_id == 25u || type_id == 26u || type_id == 27u;
}

// effects_slot_preview_mode returns the preview rendering mode for an effect.
EffectsPreviewMode effects_slot_preview_mode(FxTypeId type_id) {
    switch (type_id) {
        case 7u:
            return FX_PREVIEW_HISTORY_TRIM;
        case 20u:
        case 21u:
        case 22u:
        case 23u:
        case 24u:
        case 25u:
        case 26u:
        case 27u:
            return FX_PREVIEW_HISTORY_GR;
        case 30u:
        case 31u:
        case 32u:
        case 33u:
        case 40u:
        case 41u:
        case 43u:
            return FX_PREVIEW_EQ;
        case 70u:
        case 71u:
        case 72u:
        case 73u:
        case 74u:
        case 75u:
        case 76u:
            return FX_PREVIEW_LFO;
        case 90u:
        case 91u:
        case 92u:
        case 93u:
            return FX_PREVIEW_REVERB;
        case 50u:
        case 51u:
        case 52u:
        case 53u:
        case 54u:
            return FX_PREVIEW_DELAY;
        case 60u:
        case 61u:
        case 62u:
        case 63u:
        case 64u:
        case 65u:
            return FX_PREVIEW_CURVE;
        default:
            return FX_PREVIEW_NONE;
    }
}

// EffectsPreviewStyle holds display configuration for the preview history plot.
typedef struct EffectsPreviewStyle {
    bool bipolar;
    float max_db;
    const char* value_label;
} EffectsPreviewStyle;

// effects_slot_preview_style fills history plot display parameters based on effect type.
static void effects_slot_preview_style(FxTypeId type_id, EffectsPreviewStyle* out) {
    if (!out) {
        return;
    }
    out->bipolar = false;
    out->max_db = 24.0f;
    out->value_label = "GR";
    if (type_id == 7u) {
        out->bipolar = true;
        out->value_label = "Trim";
    }
}

// clampf clamps a float to a range.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// effects_slot_preview_push appends a sample to the preview history buffer.
static void effects_slot_preview_push(EffectsPanelPreviewSlotState* preview, float gr_db) {
    if (!preview) {
        return;
    }
    preview->history[preview->history_write] = gr_db;
    preview->history_write = (preview->history_write + 1) % FX_PANEL_PREVIEW_HISTORY;
    if (preview->history_write == 0) {
        preview->history_filled = true;
    }
}

// effects_slot_preview_render_history draws the history preview panel for a slot.
static void effects_slot_preview_render_history(SDL_Renderer* renderer,
                                                const EffectsPanelPreviewSlotState* preview,
                                                const SDL_Rect* rect,
                                                SDL_Color label_color,
                                                SDL_Color text_dim,
                                                const char* title,
                                                const EffectsPreviewStyle* style) {
    if (!renderer || !preview || !rect || rect->w <= 0 || rect->h <= 0) {
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

    int count = FX_PANEL_PREVIEW_HISTORY;
    if (count <= 1 || plot_rect.w <= 0 || plot_rect.h <= 0) {
        return;
    }
    int start = preview->history_write;
    int draw_h = plot_rect.h;
    int max_y = plot_rect.y + plot_rect.h;
    int min_y = plot_rect.y;

    float latest_gr = preview->history[(start + count - 1) % FX_PANEL_PREVIEW_HISTORY];
    if (title) {
        char buf[32];
        const char* label = (style && style->value_label) ? style->value_label : "GR";
        float display_value = latest_gr;
        if (style && style->bipolar) {
            snprintf(buf, sizeof(buf), "%s %+0.1f dB", label, display_value);
        } else {
            float reduction = -display_value;
            if (reduction < 0.0f) reduction = 0.0f;
            snprintf(buf, sizeof(buf), "%s %0.1f dB", label, reduction);
        }
        int value_w = ui_measure_text_width(buf, 0.9f);
        int text_x = rect->x + rect->w - value_w - pad;
        int text_y = rect->y + 4;
        ui_draw_text(renderer, text_x, text_y, buf, text_dim, 0.9f);
    }

    SDL_SetRenderDrawColor(renderer, grid.r, grid.g, grid.b, grid.a);
    for (int i = 1; i < 4; ++i) {
        int y = plot_rect.y + (plot_rect.h * i) / 4;
        SDL_RenderDrawLine(renderer, plot_rect.x, y, plot_rect.x + plot_rect.w, y);
    }

    float max_db = (style && style->max_db > 0.0f) ? style->max_db : 24.0f;
    if (max_db < 6.0f) max_db = 6.0f;

    float y_samples[FX_PANEL_PREVIEW_HISTORY];
    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % FX_PANEL_PREVIEW_HISTORY;
        float gr_db = preview->history[idx];
        if (style && style->bipolar) {
            y_samples[i] = gr_db;
        } else {
            float reduction = -gr_db;
            if (reduction < 0.0f) reduction = 0.0f;
            y_samples[i] = max_db - reduction;
        }
    }

    DawKitVizPlotRange range = style && style->bipolar
                               ? (DawKitVizPlotRange){-max_db, max_db}
                               : (DawKitVizPlotRange){0.0f, max_db};
    KitVizVecSegment segments[FX_PANEL_PREVIEW_HISTORY];
    size_t segment_count = 0;
    CoreResult plot_result = daw_kit_viz_plot_line_from_y_samples(y_samples,
                                                                   (uint32_t)count,
                                                                   &plot_rect,
                                                                   range,
                                                                   segments,
                                                                   FX_PANEL_PREVIEW_HISTORY,
                                                                   &segment_count);
    if (plot_result.code == CORE_OK) {
        preview_render_segments_thick(renderer, segments, segment_count, line);
        return;
    }

    // Fallback keeps previous direct draw behavior if adapter conversion fails.
    int prev_x = plot_rect.x;
    int prev_y = max_y;
    for (int i = 0; i < count; ++i) {
        float norm = clampf((y_samples[i] - range.min_value) / (range.max_value - range.min_value), 0.0f, 1.0f);
        int x = plot_rect.x + (int)lroundf(((float)i / (float)(count - 1)) * (float)plot_rect.w);
        int y = max_y - (int)lroundf(norm * (float)draw_h);
        if (y < min_y) y = min_y;
        if (y > max_y) y = max_y;
        if (i > 0) {
            preview_draw_line_thick(renderer, prev_x, prev_y, x, y, line);
        }
        prev_x = x;
        prev_y = y;
    }
}

// effects_slot_preview_draw_toggle draws the preview toggle footer button.
static void effects_slot_preview_draw_toggle(SDL_Renderer* renderer,
                                             const SDL_Rect* rect,
                                             bool open,
                                             SDL_Color label_color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color bg = {40, 44, 54, 255};
    SDL_Color border = {80, 85, 100, 200};
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        bg = theme.control_fill;
        border = theme.control_border;
        border.a = 200;
    }
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    const char* label = open ? "Hide Preview" : "Show Preview";
    int text_y = rect->y + (rect->h - ui_font_line_height(1.0f)) / 2;
    int max_w = rect->w - 12;
    if (max_w > 0) {
        ui_draw_text_clipped(renderer, rect->x + 6, text_y, label, label_color, 1.0f, max_w);
    }
}

// effects_slot_preview_render draws the preview panel and toggle for a slot.
void effects_slot_preview_render(SDL_Renderer* renderer,
                                 const FxSlotUIState* slot,
                                 EffectsPanelPreviewSlotState* preview,
                                 const SDL_Rect* preview_rect,
                                 const SDL_Rect* toggle_rect,
                                 SDL_Color label_color,
                                 SDL_Color text_dim,
                                 float sample_rate,
                                 bool have_gr,
                                 float gr_db) {
    if (!renderer || !slot || !preview || !preview_rect || !toggle_rect) {
        return;
    }
    if (preview_rect->w <= 0 || preview_rect->h <= 0) {
        return;
    }

    EffectsPreviewMode preview_mode = effects_slot_preview_mode(slot->type_id);
    if (preview_mode == FX_PREVIEW_HISTORY_GR || preview_mode == FX_PREVIEW_HISTORY_TRIM) {
        effects_slot_preview_push(preview, have_gr ? gr_db : 0.0f);
    }

    if (preview->open) {
        SDL_Rect prev_clip;
        SDL_bool had_clip = ui_clip_is_enabled(renderer);
        ui_get_clip_rect(renderer, &prev_clip);
        ui_set_clip_rect(renderer, preview_rect);
        if (preview_mode == FX_PREVIEW_CURVE) {
            effects_slot_preview_render_curve(renderer,
                                              slot,
                                              preview_rect,
                                              label_color,
                                              "Preview");
        } else if (preview_mode == FX_PREVIEW_EQ) {
            effects_slot_preview_render_eq(renderer,
                                           slot,
                                           preview_rect,
                                           label_color,
                                           sample_rate,
                                           "Preview");
        } else if (preview_mode == FX_PREVIEW_LFO) {
            effects_slot_preview_render_lfo(renderer,
                                            slot,
                                            preview_rect,
                                            label_color,
                                            "Preview");
        } else if (preview_mode == FX_PREVIEW_REVERB) {
            effects_slot_preview_render_reverb(renderer,
                                               slot,
                                               preview_rect,
                                               label_color,
                                               "Preview");
        } else if (preview_mode == FX_PREVIEW_DELAY) {
            effects_slot_preview_render_delay(renderer,
                                              slot,
                                              preview_rect,
                                              label_color,
                                              "Preview");
        } else {
            EffectsPreviewStyle preview_style;
            effects_slot_preview_style(slot->type_id, &preview_style);
            effects_slot_preview_render_history(renderer,
                                                preview,
                                                preview_rect,
                                                label_color,
                                                text_dim,
                                                "Preview",
                                                &preview_style);
        }
        ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
    } else {
        SDL_Color bg = {0};
        SDL_Color plot_bg = {0};
        SDL_Color border = {0};
        SDL_Color grid = {0};
        resolve_preview_shell_theme(&bg, &plot_bg, &border, &grid);
        border.a = 200;
        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(renderer, preview_rect);
        SDL_Rect inset = {
            preview_rect->x + 4,
            preview_rect->y + 18,
            preview_rect->w - 8,
            preview_rect->h - 22
        };
        if (inset.w > 0 && inset.h > 0) {
            SDL_SetRenderDrawColor(renderer, plot_bg.r, plot_bg.g, plot_bg.b, plot_bg.a);
            SDL_RenderFillRect(renderer, &inset);
        }
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, preview_rect);
        int collapsed_max_w = preview_rect->w - 12;
        if (collapsed_max_w > 0) {
            ui_draw_text_clipped(renderer,
                                 preview_rect->x + 6,
                                 preview_rect->y + 2,
                                 "Preview (collapsed)",
                                 text_dim,
                                 0.9f,
                                 collapsed_max_w);
        }
    }

    effects_slot_preview_draw_toggle(renderer, toggle_rect, preview->open, label_color);
}
