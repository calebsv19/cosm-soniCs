#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "audio/media_registry.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/beat_grid.h"
#include "ui/kit_viz_waveform_adapter.h"
#include "ui/shared_theme_font_adapter.h"
#include "ui/time_grid.h"
#include "ui/timeline_waveform.h"
#include "ui/waveform_render.h"
#include "time/tempo.h"
#include "input/timeline/timeline_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOVE_GHOST_ALPHA 140
#define TEMPO_OVERLAY_MIN_BPM 20.0
#define TEMPO_OVERLAY_MAX_BPM 200.0

typedef struct TimelineTheme {
    SDL_Color bg;
    SDL_Color header_fill;
    SDL_Color header_border;
    SDL_Color text;
    SDL_Color text_muted;
    SDL_Color button_fill;
    SDL_Color button_hover_fill;
    SDL_Color button_disabled_fill;
    SDL_Color button_border;
    SDL_Color lane_header_fill;
    SDL_Color lane_header_fill_active;
    SDL_Color lane_header_border;
    SDL_Color clip_fill;
    SDL_Color clip_border;
    SDL_Color clip_border_selected;
    SDL_Color clip_text;
    SDL_Color waveform;
    SDL_Color loop_fill;
    SDL_Color loop_border;
    SDL_Color loop_handle_start;
    SDL_Color loop_handle_end;
    SDL_Color loop_handle_border;
    SDL_Color loop_label;
    SDL_Color playhead;
    SDL_Color toggle_active_loop;
    SDL_Color toggle_active_snap;
    SDL_Color toggle_active_auto;
    SDL_Color toggle_active_tempo;
    SDL_Color toggle_active_label;
} TimelineTheme;

static SDL_Color mix_color(SDL_Color base, SDL_Color accent, float accent_weight) {
    if (accent_weight < 0.0f) {
        accent_weight = 0.0f;
    }
    if (accent_weight > 1.0f) {
        accent_weight = 1.0f;
    }
    float base_weight = 1.0f - accent_weight;
    SDL_Color out = {
        (Uint8)lroundf((float)base.r * base_weight + (float)accent.r * accent_weight),
        (Uint8)lroundf((float)base.g * base_weight + (float)accent.g * accent_weight),
        (Uint8)lroundf((float)base.b * base_weight + (float)accent.b * accent_weight),
        (Uint8)lroundf((float)base.a * base_weight + (float)accent.a * accent_weight)
    };
    return out;
}

static void resolve_timeline_theme(TimelineTheme* out_theme) {
    DawThemePalette theme = {0};
    if (!out_theme) {
        return;
    }
    if (daw_shared_theme_resolve_palette(&theme)) {
        out_theme->bg = theme.timeline_fill;
        out_theme->header_fill = theme.control_fill;
        out_theme->header_border = theme.pane_border;
        out_theme->text = theme.text_primary;
        out_theme->text_muted = theme.text_muted;
        out_theme->button_fill = theme.control_fill;
        out_theme->button_hover_fill = theme.control_hover_fill;
        out_theme->button_disabled_fill = theme.slider_track;
        out_theme->button_border = theme.control_border;
        out_theme->lane_header_fill = theme.control_fill;
        out_theme->lane_header_fill_active = theme.control_hover_fill;
        out_theme->lane_header_border = theme.control_border;
        out_theme->clip_fill = mix_color(theme.timeline_fill, theme.selection_fill, 0.18f);
        out_theme->clip_fill.a = 238;
        out_theme->clip_border = theme.timeline_border;
        out_theme->clip_border_selected = theme.slider_handle;
        out_theme->clip_border_selected.a = 220;
        out_theme->clip_text = theme.text_primary;
        out_theme->waveform = theme.slider_handle;
        out_theme->waveform.a = 200;
        out_theme->loop_fill = theme.accent_primary;
        out_theme->loop_fill.a = 50;
        out_theme->loop_border = theme.accent_primary;
        out_theme->loop_border.a = 180;
        out_theme->loop_handle_start = theme.slider_handle;
        out_theme->loop_handle_end = theme.accent_primary;
        out_theme->loop_handle_border = theme.timeline_border;
        out_theme->loop_label = theme.text_primary;
        out_theme->playhead = theme.accent_error;
        out_theme->toggle_active_loop = theme.accent_warning;
        out_theme->toggle_active_snap = theme.accent_primary;
        out_theme->toggle_active_auto = theme.control_active_fill;
        out_theme->toggle_active_tempo = theme.control_active_fill;
        out_theme->toggle_active_label = theme.slider_handle;
        return;
    }
    *out_theme = (TimelineTheme){
        .bg = {38, 44, 58, 255},
        .header_fill = {28, 32, 40, 255},
        .header_border = {50, 55, 70, 255},
        .text = {220, 220, 230, 255},
        .text_muted = {140, 140, 150, 255},
        .button_fill = {50, 58, 70, 255},
        .button_hover_fill = {72, 82, 100, 255},
        .button_disabled_fill = {34, 36, 40, 255},
        .button_border = {90, 95, 110, 255},
        .lane_header_fill = {30, 30, 38, 255},
        .lane_header_fill_active = {62, 62, 86, 255},
        .lane_header_border = {70, 70, 90, 255},
        .clip_fill = {36, 40, 52, 230},
        .clip_border = {20, 20, 26, 255},
        .clip_border_selected = {200, 220, 255, 220},
        .clip_text = {200, 200, 210, 255},
        .waveform = {120, 140, 170, 200},
        .loop_fill = {60, 120, 140, 50},
        .loop_border = {90, 170, 190, 180},
        .loop_handle_start = {170, 220, 200, 255},
        .loop_handle_end = {160, 190, 230, 255},
        .loop_handle_border = {40, 70, 90, 255},
        .loop_label = {210, 220, 235, 255},
        .playhead = {240, 110, 110, 255},
        .toggle_active_loop = {130, 200, 180, 255},
        .toggle_active_snap = {130, 160, 210, 255},
        .toggle_active_auto = {110, 150, 200, 255},
        .toggle_active_tempo = {120, 170, 210, 255},
        .toggle_active_label = {150, 170, 220, 255}
    };
}

static bool timeline_clip_is_selected(const AppState* state, int track_index, int clip_index) {
    if (!state) {
        return false;
    }
    if (state->selection_count > 0) {
        for (int i = 0; i < state->selection_count; ++i) {
            if (state->selection[i].track_index == track_index && state->selection[i].clip_index == clip_index) {
                return true;
            }
        }
        return false;
    }
    return state->selected_track_index == track_index && state->selected_clip_index == clip_index;
}

static const char* timeline_basename(const char* path, char* scratch, size_t scratch_len) {
    if (!scratch || scratch_len == 0) {
        return "";
    }
    scratch[0] = '\0';
    if (!path || path[0] == '\0') {
        return scratch;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    SDL_strlcpy(scratch, base, scratch_len);
    char* dot = strrchr(scratch, '.');
    if (dot) {
        *dot = '\0';
    }
    return scratch;
}

static const char* timeline_clip_display_name(const AppState* state,
                                              const EngineClip* clip,
                                              char* scratch,
                                              size_t scratch_len) {
    if (!clip) {
        return "Clip";
    }
    if (clip->name[0] != '\0') {
        return clip->name;
    }
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry) {
            if (entry->name[0] != '\0') {
                return entry->name;
            }
            if (entry->path[0] != '\0') {
                return timeline_basename(entry->path, scratch, scratch_len);
            }
        }
    }
    const char* media_path = engine_clip_get_media_path(clip);
    if (media_path && media_path[0] != '\0') {
        return timeline_basename(media_path, scratch, scratch_len);
    }
    return "Clip";
}

#define CLIP_COLOR_R 36
#define CLIP_COLOR_G 40
#define CLIP_COLOR_B 52
#define TRACK_BG_R 38
#define TRACK_BG_G 44
#define TRACK_BG_B 58
#define CLIP_HANDLE_WIDTH 8

#define WAVEFORM_COLOR_R 120
#define WAVEFORM_COLOR_G 140
#define WAVEFORM_COLOR_B 170

static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

typedef struct TimelineTextMetrics {
    bool valid;
    int text_h_1x;
    int width_plus;
    int width_minus;
    int width_loop;
    int width_snap;
    int width_auto;
    int width_pan;
    int width_vol;
    int width_tempo;
    int width_val;
    int width_m;
    int width_s;
    int width_track;
    int width_track_00;
    int width_new_track;
    int clip_label_pad_x;
    int overlay_label_pad_x;
    int label_pad_y;
    int overlay_badge_gap;
    int overlay_edge_pad;
} TimelineTextMetrics;

static TimelineTextMetrics g_timeline_text_metrics = {0};

static void timeline_text_metrics_refresh(void) {
    int text_h = ui_font_line_height(1.0f);
    if (text_h < 1) {
        text_h = 1;
    }
    if (g_timeline_text_metrics.valid &&
        g_timeline_text_metrics.text_h_1x == text_h) {
        return;
    }

    TimelineTextMetrics metrics = {0};
    metrics.valid = true;
    metrics.text_h_1x = text_h;
    metrics.width_plus = ui_measure_text_width("+", 1.0f);
    metrics.width_minus = ui_measure_text_width("-", 1.0f);
    metrics.width_loop = ui_measure_text_width("LOOP", 1.0f);
    metrics.width_snap = ui_measure_text_width("SNAP", 1.0f);
    metrics.width_auto = ui_measure_text_width("AUTO", 1.0f);
    metrics.width_pan = ui_measure_text_width("PAN", 1.0f);
    metrics.width_vol = ui_measure_text_width("VOL", 1.0f);
    metrics.width_tempo = ui_measure_text_width("TEMPO", 1.0f);
    metrics.width_val = ui_measure_text_width("VAL", 1.0f);
    metrics.width_m = ui_measure_text_width("M", 1.0f);
    metrics.width_s = ui_measure_text_width("S", 1.0f);
    metrics.width_track = ui_measure_text_width("Track", 1.0f);
    metrics.width_track_00 = ui_measure_text_width("Track 00", 1.0f);
    metrics.width_new_track = ui_measure_text_width("New Track", 1.0f);
    metrics.clip_label_pad_x = text_h / 2;
    if (metrics.clip_label_pad_x < 6) {
        metrics.clip_label_pad_x = 6;
    }
    metrics.overlay_label_pad_x = text_h / 3;
    if (metrics.overlay_label_pad_x < 4) {
        metrics.overlay_label_pad_x = 4;
    }
    metrics.label_pad_y = text_h / 6;
    if (metrics.label_pad_y < 2) {
        metrics.label_pad_y = 2;
    }
    metrics.overlay_badge_gap = text_h / 2;
    if (metrics.overlay_badge_gap < 6) {
        metrics.overlay_badge_gap = 6;
    }
    metrics.overlay_edge_pad = text_h / 3;
    if (metrics.overlay_edge_pad < 4) {
        metrics.overlay_edge_pad = 4;
    }

    g_timeline_text_metrics = metrics;
}

static const TimelineTextMetrics* timeline_text_metrics(void) {
    timeline_text_metrics_refresh();
    return &g_timeline_text_metrics;
}

static int timeline_label_width_cached(const char* label) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    if (!label) {
        return 0;
    }
    if (strcmp(label, "+") == 0) return metrics->width_plus;
    if (strcmp(label, "-") == 0) return metrics->width_minus;
    if (strcmp(label, "LOOP") == 0) return metrics->width_loop;
    if (strcmp(label, "SNAP") == 0) return metrics->width_snap;
    if (strcmp(label, "AUTO") == 0) return metrics->width_auto;
    if (strcmp(label, "PAN") == 0) return metrics->width_pan;
    if (strcmp(label, "VOL") == 0) return metrics->width_vol;
    if (strcmp(label, "TEMPO") == 0) return metrics->width_tempo;
    if (strcmp(label, "VAL") == 0) return metrics->width_val;
    if (strcmp(label, "M") == 0) return metrics->width_m;
    if (strcmp(label, "S") == 0) return metrics->width_s;
    if (strcmp(label, "Track") == 0) return metrics->width_track;
    if (strcmp(label, "Track 00") == 0) return metrics->width_track_00;
    if (strcmp(label, "New Track") == 0) return metrics->width_new_track;
    return ui_measure_text_width(label, 1.0f);
}

static void timeline_draw_text_in_rect_clipped(SDL_Renderer* renderer,
                                               const SDL_Rect* rect,
                                               const char* text,
                                               SDL_Color color,
                                               int pad_x,
                                               int pad_y,
                                               float scale) {
    int max_w;
    if (!renderer || !rect || !text || text[0] == '\0') {
        return;
    }
    max_w = rect->w - pad_x * 2;
    if (max_w <= 0) {
        return;
    }
    ui_draw_text_clipped(renderer,
                         rect->x + pad_x,
                         rect->y + pad_y,
                         text,
                         color,
                         scale,
                         max_w);
}

static int timeline_button_width(const char* label, int min_width);

static int timeline_controls_compute_layout(int timeline_x,
                                            int timeline_y,
                                            int timeline_width,
                                            TimelineControlsUI* out_controls) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    enum { k_button_count = 8 };
    const int left_pad = 10;
    const int right_pad = 10;
    const int button_gap = 6;
    const int row_pad = 3;
    int text_h = metrics->text_h_1x;
    if (text_h < 10) {
        text_h = 10;
    }
    int button_h = text_h + 4;
    if (button_h < 12) {
        button_h = 12;
    }
    int row_gap = text_h / 4;
    if (row_gap < 3) {
        row_gap = 3;
    }

    int min_controls_h = text_h + 10;
    if (min_controls_h < TIMELINE_CONTROLS_HEIGHT) {
        min_controls_h = TIMELINE_CONTROLS_HEIGHT;
    }

    SDL_Rect local_rects[k_button_count] = {{0}};
    SDL_Rect* button_rects[k_button_count] = {
        &local_rects[0], &local_rects[1], &local_rects[2], &local_rects[3],
        &local_rects[4], &local_rects[5], &local_rects[6], &local_rects[7]
    };
    if (out_controls) {
        SDL_Rect zero = {0, 0, 0, 0};
        out_controls->add_rect = zero;
        out_controls->remove_rect = zero;
        out_controls->loop_toggle_rect = zero;
        out_controls->snap_toggle_rect = zero;
        out_controls->automation_toggle_rect = zero;
        out_controls->automation_target_rect = zero;
        out_controls->tempo_toggle_rect = zero;
        out_controls->automation_label_toggle_rect = zero;
        button_rects[0] = &out_controls->add_rect;
        button_rects[1] = &out_controls->remove_rect;
        button_rects[2] = &out_controls->loop_toggle_rect;
        button_rects[3] = &out_controls->snap_toggle_rect;
        button_rects[4] = &out_controls->automation_toggle_rect;
        button_rects[5] = &out_controls->automation_target_rect;
        button_rects[6] = &out_controls->tempo_toggle_rect;
        button_rects[7] = &out_controls->automation_label_toggle_rect;
    }

    if (timeline_width <= 0) {
        return min_controls_h;
    }

    int available_w = timeline_width - left_pad - right_pad;
    if (available_w <= 0) {
        available_w = 1;
    }

    int widths[k_button_count];
    widths[0] = timeline_button_width("+", text_h + 8);
    widths[1] = timeline_button_width("-", text_h + 8);
    widths[2] = timeline_button_width("LOOP", 36);
    widths[3] = timeline_button_width("SNAP", 40);
    widths[4] = timeline_button_width("AUTO", 44);
    widths[5] = timeline_button_width("PAN", 40);
    {
        int vol_w = timeline_button_width("VOL", 40);
        if (vol_w > widths[5]) {
            widths[5] = vol_w;
        }
    }
    widths[6] = timeline_button_width("TEMPO", 52);
    widths[7] = timeline_button_width("VAL", 40);

    for (int i = 0; i < k_button_count; ++i) {
        if (widths[i] > available_w) {
            widths[i] = available_w;
        }
        if (widths[i] < 1) {
            widths[i] = 1;
        }
    }

    int row = 0;
    int row_x = 0;
    for (int i = 0; i < k_button_count; ++i) {
        if (row_x > 0 && row_x + widths[i] > available_w) {
            row += 1;
            row_x = 0;
        }
        int button_x = timeline_x + left_pad + row_x;
        int button_y = timeline_y + row_pad + row * (button_h + row_gap);
        *button_rects[i] = (SDL_Rect){button_x, button_y, widths[i], button_h};
        row_x += widths[i] + button_gap;
    }

    int controls_h = row_pad * 2 + (row + 1) * button_h + row * row_gap;
    if (controls_h < min_controls_h) {
        int y_shift = (min_controls_h - controls_h) / 2;
        for (int i = 0; i < k_button_count; ++i) {
            button_rects[i]->y += y_shift;
        }
        controls_h = min_controls_h;
    }
    return controls_h;
}

int timeline_view_controls_height(void) {
    return timeline_controls_compute_layout(0, 0, 0, NULL);
}

int timeline_view_controls_height_for_width(int timeline_width) {
    return timeline_controls_compute_layout(0, 0, timeline_width, NULL);
}

int timeline_view_ruler_height(void) {
    int text_h = timeline_text_metrics()->text_h_1x;
    int height = text_h + 6;
    if (height < TIMELINE_RULER_HEIGHT) {
        height = TIMELINE_RULER_HEIGHT;
    }
    return height;
}

int timeline_view_track_header_width(void) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    int label_w = metrics->width_track_00;
    int text_h = metrics->text_h_1x;
    int button_w = metrics->width_m + 8;
    if (button_w < text_h) {
        button_w = text_h;
    }
    if (button_w < 16) {
        button_w = 16;
    }
    int width = 16 + 6 + label_w + 10 + (button_w * 2) + 4;
    if (width < TIMELINE_TRACK_HEADER_WIDTH) {
        width = TIMELINE_TRACK_HEADER_WIDTH;
    }
    return width;
}

int timeline_view_lane_clip_inset(int track_height) {
    int text_h = timeline_text_metrics()->text_h_1x;
    int inset = text_h / 2 + 2;
    int max_inset = track_height / 3;
    if (inset < 6) {
        inset = 6;
    }
    if (max_inset < 2) {
        max_inset = 2;
    }
    if (inset > max_inset) {
        inset = max_inset;
    }
    return inset;
}

void timeline_view_compute_lane_clip_rect(int lane_top,
                                          int track_height,
                                          int clip_x,
                                          int clip_w,
                                          SDL_Rect* out_rect) {
    int inset;
    int clip_h;
    int clip_y;
    if (!out_rect) {
        return;
    }
    inset = timeline_view_lane_clip_inset(track_height);
    clip_h = track_height - inset * 2;
    if (clip_h < 4) {
        clip_h = 4;
    }
    if (clip_h > track_height) {
        clip_h = track_height;
    }
    clip_y = lane_top + (track_height - clip_h) / 2;
    *out_rect = (SDL_Rect){clip_x, clip_y, clip_w, clip_h};
}

void timeline_view_compute_track_header_layout(const SDL_Rect* timeline_rect,
                                               int lane_top,
                                               int track_height,
                                               int header_width,
                                               TimelineTrackHeaderLayout* out_layout) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    TimelineTrackHeaderLayout layout = {0};
    int text_h;
    int button_w;
    int button_h;
    int button_spacing;
    int buttons_total;
    int buttons_x;
    int buttons_min_x;
    int button_y;
    if (!timeline_rect || !out_layout) {
        return;
    }
    layout.header_rect = (SDL_Rect){
        timeline_rect->x + 8,
        lane_top + 4,
        header_width - 16,
        track_height - 8
    };
    if (layout.header_rect.w < 18) {
        layout.header_rect.w = 18;
    }
    if (layout.header_rect.h < 12) {
        layout.header_rect.h = 12;
    }

    text_h = metrics->text_h_1x;
    if (text_h < 10) {
        text_h = 10;
    }
    button_w = metrics->width_m + 8;
    if (button_w < text_h) {
        button_w = text_h;
    }
    if (button_w < 16) {
        button_w = 16;
    }
    button_h = text_h + 4;
    if (button_h > layout.header_rect.h - 2) {
        button_h = layout.header_rect.h - 2;
    }
    if (button_h < 10) {
        button_h = 10;
    }
    button_spacing = 4;
    buttons_total = button_w * 2 + button_spacing;
    buttons_x = layout.header_rect.x + layout.header_rect.w - buttons_total - 8;
    buttons_min_x = layout.header_rect.x + 6 + metrics->width_track + 4;
    if (buttons_x < buttons_min_x) {
        buttons_x = buttons_min_x;
    }
    button_y = layout.header_rect.y + layout.header_rect.h - button_h - 4;
    if (button_y < layout.header_rect.y + 1) {
        button_y = layout.header_rect.y + 1;
    }
    layout.mute_rect = (SDL_Rect){buttons_x, button_y, button_w, button_h};
    layout.solo_rect = (SDL_Rect){buttons_x + button_w + button_spacing, button_y, button_w, button_h};

    layout.text_x = layout.header_rect.x + 6;
    layout.text_max_x = layout.mute_rect.x - 4;
    if (layout.text_max_x < layout.text_x) {
        layout.text_max_x = layout.text_x;
    }
    layout.text_y = layout.header_rect.y + 4;
    if (layout.text_y + text_h > layout.header_rect.y + layout.header_rect.h - 1) {
        layout.text_y = layout.header_rect.y + layout.header_rect.h - text_h - 1;
    }
    if (layout.text_y < layout.header_rect.y + 1) {
        layout.text_y = layout.header_rect.y + 1;
    }
    *out_layout = layout;
}

static int timeline_button_width(const char* label, int min_width) {
    int width = timeline_label_width_cached(label) + 14;
    if (width < min_width) {
        width = min_width;
    }
    return width;
}

static void draw_timeline_button(SDL_Renderer* renderer,
                                 const SDL_Rect* rect,
                                 const char* label,
                                 bool hovered,
                                 bool enabled,
                                 const TimelineTheme* theme) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = theme ? theme->button_fill : (SDL_Color){50, 58, 70, 255};
    SDL_Color disabled = theme ? theme->button_disabled_fill : (SDL_Color){34, 36, 40, 255};
    SDL_Color border = theme ? theme->button_border : (SDL_Color){90, 95, 110, 255};
    SDL_Color text = theme ? theme->text : (SDL_Color){220, 220, 230, 255};
    SDL_Color text_disabled = theme ? theme->text_muted : (SDL_Color){140, 140, 150, 255};

    SDL_Color fill = enabled ? base : disabled;
    SDL_Color border_color = hovered && enabled && theme ? theme->clip_border_selected : border;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Color text_color = enabled ? text : text_disabled;
    int scale = 1;
    int text_w = timeline_label_width_cached(label);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text_color, scale);
}

static void fit_label_ellipsis(const char* src, int max_px, float scale, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!src) return;
    int full_w = ui_measure_text_width(src, scale);
    if (full_w <= max_px) {
        strncpy(out, src, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    const char* ell = "...";
    int ell_w = ui_measure_text_width(ell, scale);
    int target_px = max_px - ell_w;
    if (target_px <= 0) {
        strncpy(out, ell, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t len = strlen(src);
    for (size_t i = len; i > 0; --i) {
        char tmp[ENGINE_CLIP_NAME_MAX];
        snprintf(tmp, sizeof(tmp), "%.*s", (int)i, src);
        if (ui_measure_text_width(tmp, scale) <= target_px) {
            snprintf(out, out_size, "%s%s", tmp, ell);
            out[out_size - 1] = '\0';
            return;
        }
    }
    strncpy(out, ell, out_size - 1);
    out[out_size - 1] = '\0';
}

static void draw_toggle_button(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const char* label,
                               bool active,
                               bool hovered,
                               SDL_Color active_col,
                               const TimelineTheme* theme) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = theme ? theme->button_fill : (SDL_Color){58, 62, 70, 255};
    SDL_Color active_color = active_col;
    SDL_Color text = theme ? theme->text : (SDL_Color){220, 220, 230, 255};
    SDL_Color border = theme ? theme->button_border : (SDL_Color){90, 95, 110, 255};

    SDL_Color fill = base;
    SDL_Color border_color = border;
    if (active) {
        fill = active_color;
        if (theme) {
            border_color = theme->clip_border_selected;
        }
    } else if (hovered) {
        if (theme) {
            border_color = theme->clip_border_selected;
        }
    }
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderDrawRect(renderer, rect);

    int scale = 1;
    int text_w = timeline_label_width_cached(label);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    if (text_x < rect->x + 2) text_x = rect->x + 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

static void draw_timeline_grid(SDL_Renderer* renderer,
                               int x0,
                               int width,
                               int top,
                               int height,
                               float pixels_per_second,
                               float visible_seconds,
                               bool show_all_lines,
                               float window_start_seconds,
                               bool view_in_beats,
                               const TempoMap* tempo_map,
                               const TimeSignatureMap* signature_map) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_Rect border = {x0, top, width, height};
    SDL_RenderDrawRect(renderer, &border);

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_Color minor_line = {65, 65, 85, 255};
    SDL_Color sub_line = {66, 70, 100, 200};
    SDL_Color major_line = {80, 82, 115, 255};
    SDL_Color downbeat_line = {90, 100, 130, 255};
    int label_scale = 1;
    int label_h = ui_font_line_height((float)label_scale);
    int label_y = top - label_h - (label_h / 4 > 2 ? label_h / 4 : 2);
    int min_label_gap = (label_h / 3 > 4) ? (label_h / 3) : 4;
    SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);

    if (!view_in_beats || !tempo_map || tempo_map->event_count <= 0 || tempo_map->sample_rate <= 0.0) {
        if (show_all_lines) {
            float first_minor = floorf(window_start_seconds);
            float last_minor = window_start_seconds + visible_seconds;
            SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
            for (float sec_abs = first_minor; sec_abs <= last_minor + 0.5f; sec_abs += 1.0f) {
                float local = sec_abs - window_start_seconds;
                if (local < 0.0f || local > visible_seconds * 1.5f) {
                    continue;
                }
                int x = x0 + (int)roundf(local * pixels_per_second);
                SDL_RenderDrawLine(renderer, x, top, x, top + height);
            }
        }

        float major_interval = 1.0f;
        if (visible_seconds > 60.0f) {
            major_interval = 10.0f;
        } else if (visible_seconds > 30.0f) {
            major_interval = 5.0f;
        } else if (visible_seconds > 15.0f) {
            major_interval = 2.0f;
        }

        float first_major_sec = floor(window_start_seconds / major_interval) * major_interval;
        float last_major_sec = window_start_seconds + visible_seconds + major_interval * 0.5f;
        int last_label_right = x0 - 4096;
        for (float sec_abs = first_major_sec; sec_abs <= last_major_sec; sec_abs += major_interval) {
            float local = sec_abs - window_start_seconds;
            if (local < 0.0f || local > visible_seconds * 1.5f) {
                continue;
            }
            int x = x0 + (int)roundf(local * pixels_per_second);
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);

            float label_sec = sec_abs;
            int total_seconds = (int)label_sec;
            int minutes = total_seconds / 60;
            int seconds = total_seconds % 60;
            char label[16];
            snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
            int label_x = x + 4;
            int label_w = ui_measure_text_width(label, (float)label_scale);
            if (label_x <= last_label_right + min_label_gap) {
                continue;
            }
            ui_draw_text(renderer, label_x, label_y, label, label_color, (float)label_scale);
            last_label_right = label_x + label_w;
        }
        return;
    }

    double start_beats = tempo_map_seconds_to_beats(tempo_map, (double)window_start_seconds);
    double end_beats = tempo_map_seconds_to_beats(tempo_map, (double)(window_start_seconds + visible_seconds));
    double visible_beats = end_beats - start_beats;
    if (visible_beats < 0.0001) {
        visible_beats = 0.0001;
    }

    // Minor lines at every beat when show_all_lines is on.
    if (show_all_lines) {
        const TimeSignatureEvent* minor_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
        double beat_unit = time_signature_beat_unit(minor_sig);
        if (beat_unit <= 0.0) {
            beat_unit = 1.0;
        }
        double first_minor = floor(start_beats / beat_unit) * beat_unit;
        double last_minor = end_beats;
        SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
        for (double gb = first_minor; gb <= last_minor + beat_unit * 0.5; gb += beat_unit) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    const TimeSignatureEvent* base_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
    double beat_unit = time_signature_beat_unit(base_sig);
    if (beat_unit <= 0.0) {
        beat_unit = 1.0;
    }
    double beats_per_bar = time_signature_beats_per_bar(base_sig);
    if (beats_per_bar <= 0.0) {
        beats_per_bar = 4.0;
    }
    double major_units = 1.0;
    double visible_units = (beat_unit > 0.0) ? (visible_beats / beat_unit) : visible_beats;
    if (visible_units > 128.0) {
        major_units = 8.0;
    } else if (visible_units > 64.0) {
        major_units = 4.0;
    } else if (visible_units > 32.0) {
        major_units = 2.0;
    }
    double major_interval_beats = beat_unit * major_units;

    // Extra subdivision lines at high zoom: quarter-beat at tight zoom, half-beat at medium zoom (no labels).
    double sub_interval = 0.0;
    if (visible_beats <= 8.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.25; // quarter of a beat unit
    } else if (visible_beats <= 32.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.5; // half of a beat unit
    }
    if (sub_interval > 0.0) {
        SDL_SetRenderDrawColor(renderer, sub_line.r, sub_line.g, sub_line.b, sub_line.a);
        double first_sub = floor(start_beats / sub_interval) * sub_interval;
        double last_sub = end_beats + sub_interval * 0.5;
        for (double gb = first_sub; gb <= last_sub; gb += sub_interval) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    if (!show_all_lines) {
        double visible_bars = visible_beats / (double)beats_per_bar;
        int bar_interval = 1;
        if (visible_bars > 128.0) {
            bar_interval = 16;
        } else if (visible_bars > 64.0) {
            bar_interval = 8;
        } else if (visible_bars > 32.0) {
            bar_interval = 4;
        } else if (visible_bars > 16.0) {
            bar_interval = 2;
        }

    int bar = 1;
    int beat_idx = 1;
    double sub = 0.0;
    time_signature_map_beat_to_bar_beat(signature_map, start_beats, &bar, &beat_idx, &sub, NULL, NULL);
    double bar_start = start_beats - (((double)(beat_idx - 1) + sub) * beat_unit);
        if (bar_start < 0.0) {
            bar_start = 0.0;
            bar = 1;
        }
        int last_label_right = x0 - 4096;
        while (bar_start <= end_beats + 0.5) {
            if ((bar - 1) % bar_interval == 0) {
                double sec = tempo_map_beats_to_seconds(tempo_map, bar_start);
                double local_sec = sec - window_start_seconds;
                if (local_sec >= 0.0 && local_sec <= visible_seconds * 1.5) {
                    int x = x0 + (int)round(local_sec * (double)pixels_per_second);
                    SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
                    SDL_RenderDrawLine(renderer, x, top, x, top + height);

                    char label[24];
                    snprintf(label, sizeof(label), "%d", bar);
                    int label_x = x + 4;
                    int label_w = ui_measure_text_width(label, (float)label_scale);
                    if (label_x > last_label_right + min_label_gap) {
                        ui_draw_text(renderer, label_x, label_y, label, label_color, (float)label_scale);
                        last_label_right = label_x + label_w;
                    }
                }
            }
            const TimeSignatureEvent* sig = time_signature_map_event_at_beat(signature_map, bar_start + 1e-6);
            double next_beats_per_bar = time_signature_beats_per_bar(sig);
            if (next_beats_per_bar <= 0.0) {
                next_beats_per_bar = beats_per_bar;
            }
            bar_start += next_beats_per_bar;
            bar += 1;
        }
        return;
    }

    // Major beats aligned to absolute bars/beats (not reset per window).
    double first_major = floor(start_beats / major_interval_beats) * major_interval_beats;
    double last_major = end_beats + major_interval_beats * 0.5;
    int last_label_right = x0 - 4096;
    for (double gb = first_major; gb <= last_major; gb += major_interval_beats) {
        double sec = tempo_map_beats_to_seconds(tempo_map, gb);
        double local_sec = sec - window_start_seconds;
        if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
            continue;
        }
        int x = x0 + (int)round(local_sec * (double)pixels_per_second);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, gb, &bar, &beat_idx, &sub, NULL, NULL);
        bool is_downbeat = beat_idx == 1;
        if (is_downbeat) {
            SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
        } else {
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
        }
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        char label[24];
        snprintf(label, sizeof(label), "%d.%d", bar, beat_idx);
        int label_x = x + 4;
        int label_w = ui_measure_text_width(label, (float)label_scale);
        if (label_x <= last_label_right + min_label_gap) {
            continue;
        }
        SDL_Color c = label_color;
        if (beat_idx == 1) {
            c = (SDL_Color){200, 210, 230, 255};
        }
        ui_draw_text(renderer, label_x, label_y, label, c, (float)label_scale);
        last_label_right = label_x + label_w;
    }
}

static void format_timeline_label(float seconds,
                                  bool view_in_beats,
                                  const TempoMap* tempo_map,
                                  const TimeSignatureMap* signature_map,
                                  char* out,
                                  size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (view_in_beats && tempo_map && tempo_map->event_count > 0 && tempo_map->sample_rate > 0.0) {
        double beats = tempo_map_seconds_to_beats(tempo_map, seconds);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, beats, &bar, &beat_idx, &sub, NULL, NULL);
        if (bar < 1) bar = 1;
        if (beat_idx < 1) beat_idx = 1;
        snprintf(out, out_len, "%d.%02d", bar, beat_idx);
    } else {
        int total_ms = (int)llroundf(seconds * 1000.0f);
        int minutes = total_ms / 60000;
        int seconds_part = (total_ms / 1000) % 60;
        int millis = total_ms % 1000;
        snprintf(out, out_len, "%02d:%02d.%03d", minutes, seconds_part, millis);
    }
    out[out_len - 1] = '\0';
}

// Computes the tempo overlay rectangle positioned over the top track lane.
static bool timeline_compute_tempo_overlay_rect(const SDL_Rect* timeline_rect,
                                                int track_y,
                                                int track_height,
                                                int content_left,
                                                int content_width,
                                                SDL_Rect* out_rect) {
    SDL_Rect lane_rect = {0, 0, 0, 0};
    if (!timeline_rect || !out_rect || track_height <= 0 || content_width <= 0) {
        return false;
    }
    timeline_view_compute_lane_clip_rect(track_y,
                                         track_height,
                                         content_left,
                                         content_width,
                                         &lane_rect);
    if (lane_rect.h < 8) {
        return false;
    }
    *out_rect = lane_rect;
    return true;
}

// Maps a BPM value into a y position within the overlay rectangle.
static int timeline_tempo_value_to_y(const SDL_Rect* rect, double bpm, double min_bpm, double max_bpm) {
    if (!rect || rect->h <= 0) {
        return 0;
    }
    double range = max_bpm - min_bpm;
    if (range <= 0.0) {
        range = 1.0;
    }
    double t = (bpm - min_bpm) / range;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return rect->y + rect->h - (int)llround(t * (double)rect->h);
}

// Draws the tempo overlay lane with step segments and optional value labels.
static void draw_timeline_tempo_overlay(SDL_Renderer* renderer,
                                        const SDL_Rect* overlay_rect,
                                        const TempoMap* tempo_map,
                                        int selected_index,
                                        float window_start_seconds,
                                        float window_end_seconds,
                                        int content_left,
                                        float pixels_per_second,
                                        bool draw_labels) {
    if (!renderer || !overlay_rect || overlay_rect->w <= 0 || overlay_rect->h <= 0 ||
        pixels_per_second <= 0.0f || window_end_seconds <= window_start_seconds) {
        return;
    }
    if (!tempo_map || tempo_map->event_count <= 0) {
        return;
    }
    const double min_bpm = TEMPO_OVERLAY_MIN_BPM;
    const double max_bpm = TEMPO_OVERLAY_MAX_BPM;

    SDL_SetRenderDrawColor(renderer, 12, 16, 22, 140);
    SDL_RenderFillRect(renderer, overlay_rect);
    SDL_SetRenderDrawColor(renderer, 70, 90, 120, 200);
    SDL_RenderDrawRect(renderer, overlay_rect);

    SDL_Color line_color = {180, 210, 230, 220};
    SDL_Color point_color = {200, 230, 250, 255};
    SDL_Color label_color = {220, 235, 255, 255};

    for (int i = 0; i < tempo_map->event_count; ++i) {
        const TempoEvent* evt = &tempo_map->events[i];
        double seg_start = tempo_map_beats_to_seconds(tempo_map, evt->beat);
        double seg_end = window_end_seconds;
        if (i + 1 < tempo_map->event_count) {
            seg_end = tempo_map_beats_to_seconds(tempo_map, tempo_map->events[i + 1].beat);
        }
        if (seg_end < window_start_seconds || seg_start > window_end_seconds) {
            continue;
        }
        double draw_start = seg_start < window_start_seconds ? window_start_seconds : seg_start;
        double draw_end = seg_end > window_end_seconds ? window_end_seconds : seg_end;
        if (draw_end <= draw_start) {
            continue;
        }
        int x0 = content_left + (int)llround((draw_start - window_start_seconds) * pixels_per_second);
        int x1 = content_left + (int)llround((draw_end - window_start_seconds) * pixels_per_second);
        int y = timeline_tempo_value_to_y(overlay_rect, evt->bpm, min_bpm, max_bpm);
        SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);
        SDL_RenderDrawLine(renderer, x0, y, x1, y);

        if (seg_start >= window_start_seconds && seg_start <= window_end_seconds) {
            int px = content_left + (int)llround((seg_start - window_start_seconds) * pixels_per_second);
            SDL_Rect dot = {px - 3, y - 3, 6, 6};
            if (i == selected_index) {
                SDL_SetRenderDrawColor(renderer, 240, 250, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, point_color.r, point_color.g, point_color.b, point_color.a);
            }
            SDL_RenderFillRect(renderer, &dot);
            if (draw_labels) {
                char label[32];
                snprintf(label, sizeof(label), "%.1f", evt->bpm);
                int text_w = ui_measure_text_width(label, 1);
                int text_x = px + 6;
                int text_y = y - ui_font_line_height(1) - 2;
                if (text_x + text_w > overlay_rect->x + overlay_rect->w - 4) {
                    text_x = px - text_w - 6;
                }
                if (text_y < overlay_rect->y) {
                    text_y = overlay_rect->y;
                }
                ui_draw_text(renderer, text_x, text_y, label, label_color, 1);
            }
        }
    }
}

// Draws the automation mode overlay across the visible timeline lanes.
static void draw_timeline_automation_overlay(SDL_Renderer* renderer,
                                             const SDL_Rect* rect,
                                             int track_y,
                                             int track_height,
                                             int track_spacing,
                                             int track_count,
                                             int content_left,
                                             int content_width) {
    if (!renderer || !rect || track_height <= 0 || content_width <= 0) {
        return;
    }
    int lanes = track_count > 0 ? track_count : 1;
    int timeline_bottom = track_y + (lanes * (track_height + track_spacing)) - track_spacing;
    if (timeline_bottom <= track_y) {
        return;
    }
    SDL_Rect overlay_rect = {rect->x, track_y, rect->w, timeline_bottom - track_y};
    SDL_SetRenderDrawColor(renderer, 10, 12, 16, 140);
    SDL_RenderFillRect(renderer, &overlay_rect);
    SDL_SetRenderDrawColor(renderer, 120, 150, 175, 220);
    for (int lane = 0; lane < lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        int baseline_y = lane_top + track_height / 2;
        SDL_RenderDrawLine(renderer, content_left, baseline_y, content_left + content_width, baseline_y);
    }
}

static void draw_timeline_automation_segment(SDL_Renderer* renderer,
                                             int baseline_y,
                                             int range,
                                             float window_start_seconds,
                                             float window_end_seconds,
                                             int content_left,
                                             float pixels_per_second,
                                             double seg_start_seconds,
                                             double seg_end_seconds,
                                             float start_value,
                                             float end_value) {
    if (!renderer || range <= 0 || pixels_per_second <= 0.0f) {
        return;
    }
    if (seg_end_seconds <= seg_start_seconds) {
        return;
    }
    double visible_start = seg_start_seconds;
    double visible_end = seg_end_seconds;
    if (visible_start < window_start_seconds) {
        visible_start = window_start_seconds;
    }
    if (visible_end > window_end_seconds) {
        visible_end = window_end_seconds;
    }
    if (visible_end <= visible_start) {
        return;
    }
    double span = seg_end_seconds - seg_start_seconds;
    float t0 = span > 0.0 ? (float)((visible_start - seg_start_seconds) / span) : 0.0f;
    float t1 = span > 0.0 ? (float)((visible_end - seg_start_seconds) / span) : 0.0f;
    float v0 = start_value + (end_value - start_value) * t0;
    float v1 = start_value + (end_value - start_value) * t1;
    int x0 = content_left + (int)llroundf((float)(visible_start - window_start_seconds) * pixels_per_second);
    int x1 = content_left + (int)llroundf((float)(visible_end - window_start_seconds) * pixels_per_second);
    int y0 = baseline_y - (int)llroundf(v0 * (float)range);
    int y1 = baseline_y - (int)llroundf(v1 * (float)range);
    SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
}

// Draws automation points and linear segments for a clip.
static void draw_timeline_clip_automation(SDL_Renderer* renderer,
                                          const SDL_Rect* clip_rect,
                                          const EngineClip* clip,
                                          uint64_t clip_frames,
                                          EngineAutomationTarget target,
                                          const AutomationUIState* automation_ui,
                                          int track_index,
                                          int clip_index,
                                          int sample_rate,
                                          float window_start_seconds,
                                          float window_end_seconds,
                                          int content_left,
                                          int content_right,
                                          float pixels_per_second) {
    if (!renderer || !clip_rect || !clip || clip_rect->h <= 0 || clip_frames == 0 || sample_rate <= 0) {
        return;
    }
    const EngineAutomationLane* lane = NULL;
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        if (clip->automation_lanes[i].target == target) {
            lane = &clip->automation_lanes[i];
            break;
        }
    }
    int baseline_y = clip_rect->y + clip_rect->h / 2;
    int range = clip_rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    SDL_Color line_color = {170, 210, 230, 220};
    SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);

    double clip_start_seconds = (double)clip->timeline_start_frames / (double)sample_rate;
    double clip_end_seconds = clip_start_seconds + (double)clip_frames / (double)sample_rate;
    if (clip_end_seconds <= window_start_seconds || clip_start_seconds >= window_end_seconds) {
        return;
    }
    float prev_value = 0.0f;
    double prev_seconds = clip_start_seconds;
    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t frame = point->frame > clip_frames ? clip_frames : point->frame;
            float value = point->value;
            double point_seconds = clip_start_seconds + (double)frame / (double)sample_rate;
            draw_timeline_automation_segment(renderer,
                                             baseline_y,
                                             range,
                                             window_start_seconds,
                                             window_end_seconds,
                                             content_left,
                                             pixels_per_second,
                                             prev_seconds,
                                             point_seconds,
                                             prev_value,
                                             value);
            prev_seconds = point_seconds;
            prev_value = value;
        }
    }
    draw_timeline_automation_segment(renderer,
                                     baseline_y,
                                     range,
                                     window_start_seconds,
                                     window_end_seconds,
                                     content_left,
                                     pixels_per_second,
                                     prev_seconds,
                                     clip_end_seconds,
                                     prev_value,
                                     0.0f);

    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t frame = point->frame > clip_frames ? clip_frames : point->frame;
            double point_seconds = clip_start_seconds + (double)frame / (double)sample_rate;
            if (point_seconds < window_start_seconds || point_seconds > window_end_seconds) {
                continue;
            }
            int x = content_left + (int)llround((float)(point_seconds - window_start_seconds) * pixels_per_second);
            if (x < content_left - 6 || x > content_right + 6) {
                continue;
            }
            int y = baseline_y - (int)llround((double)point->value * (double)range);
            int r = 3;
            SDL_Rect dot = {x - r, y - r, r * 2, r * 2};
            bool selected = automation_ui &&
                            automation_ui->track_index == track_index &&
                            automation_ui->clip_index == clip_index &&
                            automation_ui->point_index == i &&
                            automation_ui->target == target;
            if (selected) {
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 120, 150, 170, 255);
            }
            SDL_RenderFillRect(renderer, &dot);
        }
    }
}

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, AppState* state) {
    TimelineTheme theme = {0};
    if (!renderer || !rect || !state || !state->engine) {
        return;
    }
    resolve_timeline_theme(&theme);

    SDL_SetRenderDrawColor(renderer, theme.bg.r, theme.bg.g, theme.bg.b, theme.bg.a);
    SDL_RenderFillRect(renderer, rect);

    float visible_seconds = clamp_float(state->timeline_visible_seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_float(state->timeline_vertical_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    float min_window_start = 0.0f;
    float max_window_start = 0.0f;
    timeline_get_scroll_bounds(state, visible_seconds, &min_window_start, &max_window_start);
    float window_start = clamp_float(state->timeline_window_start_seconds, min_window_start, max_window_start);
    float window_end = window_start + visible_seconds;

    const int controls_height = timeline_view_controls_height_for_width(rect->w);
    const int ruler_height = timeline_view_ruler_height();
    const int track_top_offset = controls_height + ruler_height;
    const int track_y = rect->y + track_top_offset;
    int track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (track_height < 32) {
        track_height = 32;
    }
    const int track_spacing = 12;
    const int header_width = timeline_view_track_header_width();
    const int content_left = rect->x + header_width + TIMELINE_BORDER_MARGIN;
    const int content_right = rect->x + rect->w - TIMELINE_BORDER_MARGIN;
    const int content_width = content_right - content_left;
    if (content_width <= 0) {
        return;
    }

    const Engine* engine = state->engine;
    const EngineTrack* tracks = engine_get_tracks(engine);
    const int track_count = engine_get_track_count(engine);
    const int sample_rate = engine_get_config(engine)->sample_rate;

    SDL_Rect header_rect = {rect->x, rect->y, rect->w, controls_height};
    SDL_SetRenderDrawColor(renderer, theme.header_fill.r, theme.header_fill.g, theme.header_fill.b, theme.header_fill.a);
    SDL_RenderFillRect(renderer, &header_rect);
    SDL_SetRenderDrawColor(renderer, theme.header_border.r, theme.header_border.g, theme.header_border.b, theme.header_border.a);
    SDL_RenderDrawLine(renderer, rect->x, rect->y + controls_height - 1, rect->x + rect->w, rect->y + controls_height - 1);

    TimelineControlsUI* controls = (TimelineControlsUI*)&state->timeline_controls;
    timeline_controls_compute_layout(rect->x, rect->y, rect->w, controls);

    bool remove_enabled = track_count > 0;
    draw_timeline_button(renderer, &controls->add_rect, "+", controls->add_hovered, true, &theme);
    draw_timeline_button(renderer, &controls->remove_rect, "-", controls->remove_hovered, remove_enabled, &theme);
    draw_timeline_button(renderer, &controls->loop_toggle_rect, "LOOP", controls->loop_toggle_hovered, true, &theme);
    draw_timeline_button(renderer, &controls->snap_toggle_rect, "SNAP", controls->snap_toggle_hovered, true, &theme);
    draw_timeline_button(renderer, &controls->automation_toggle_rect, "AUTO", controls->automation_toggle_hovered, true, &theme);
    const char* target_label = state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN ? "PAN" : "VOL";
    draw_timeline_button(renderer, &controls->automation_target_rect, target_label, controls->automation_target_hovered, true, &theme);
    draw_timeline_button(renderer, &controls->tempo_toggle_rect, "TEMPO", controls->tempo_toggle_hovered, true, &theme);
    draw_timeline_button(renderer, &controls->automation_label_toggle_rect, "VAL", controls->automation_label_toggle_hovered, true, &theme);
    if (state->loop_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_loop.r, theme.toggle_active_loop.g, theme.toggle_active_loop.b, theme.toggle_active_loop.a);
        SDL_RenderDrawRect(renderer, &controls->loop_toggle_rect);
    }
    if (state->timeline_snap_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_snap.r, theme.toggle_active_snap.g, theme.toggle_active_snap.b, theme.toggle_active_snap.a);
        SDL_RenderDrawRect(renderer, &controls->snap_toggle_rect);
    }
    if (state->timeline_automation_mode) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_auto.r, theme.toggle_active_auto.g, theme.toggle_active_auto.b, theme.toggle_active_auto.a);
        SDL_RenderDrawRect(renderer, &controls->automation_toggle_rect);
    }
    if (state->timeline_tempo_overlay_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_tempo.r, theme.toggle_active_tempo.g, theme.toggle_active_tempo.b, theme.toggle_active_tempo.a);
        SDL_RenderDrawRect(renderer, &controls->tempo_toggle_rect);
    }
    if (state->timeline_automation_labels_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_label.r, theme.toggle_active_label.g, theme.toggle_active_label.b, theme.toggle_active_label.a);
        SDL_RenderDrawRect(renderer, &controls->automation_label_toggle_rect);
    }

    controls->loop_start_rect = (SDL_Rect){0,0,0,0};
    controls->loop_end_rect = (SDL_Rect){0,0,0,0};

    const float pixels_per_second = (float)content_width / visible_seconds;
    int grid_lanes = track_count > 0 ? track_count : 1;

    bool have_loop_controls = state->loop_enabled && sample_rate > 0 && state->loop_end_frame > state->loop_start_frame;
    bool have_loop_region = false;
    SDL_Rect loop_region_rect = {0, 0, 0, 0};
    if (have_loop_controls) {
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        float loop_start_local = loop_start_sec - window_start;
        float loop_end_local = loop_end_sec - window_start;
        if (loop_start_local < 0.0f) loop_start_local = 0.0f;
        if (loop_end_local < 0.0f) loop_end_local = 0.0f;
        if (loop_start_local > visible_seconds) loop_start_local = visible_seconds;
        if (loop_end_local > visible_seconds) loop_end_local = visible_seconds;
        int loop_start_x = content_left + (int)roundf(loop_start_local * pixels_per_second);
        int loop_end_x = content_left + (int)roundf(loop_end_local * pixels_per_second);
        if (loop_start_x < content_left) loop_start_x = content_left;
        if (loop_start_x > content_left + content_width) loop_start_x = content_left + content_width;
        if (loop_end_x < content_left) loop_end_x = content_left;
        if (loop_end_x > content_left + content_width) loop_end_x = content_left + content_width;
        if (loop_end_x < loop_start_x) {
            int tmp = loop_end_x;
            loop_end_x = loop_start_x;
            loop_start_x = tmp;
        }
        int top_zone_y = rect->y + controls_height + 2;
        int top_zone_h = ruler_height - 4;
        if (top_zone_h < 10) top_zone_h = 10;
        int handle_width = 10;
        int handle_height = top_zone_h - 4;
        if (handle_height < 8) handle_height = 8;
        int handle_y = top_zone_y + (top_zone_h - handle_height) / 2;
        controls->loop_start_rect = (SDL_Rect){loop_start_x - handle_width / 2, handle_y, handle_width, handle_height};
        controls->loop_end_rect = (SDL_Rect){loop_end_x - handle_width / 2, handle_y, handle_width, handle_height};

        if (loop_end_x > loop_start_x) {
            have_loop_region = true;
            int loop_top = rect->y + controls_height;
            int loop_bottom = track_y + grid_lanes * (track_height + track_spacing) - track_spacing;
            int loop_height = loop_bottom - loop_top;
            if (loop_height < 0) loop_height = 0;
            loop_region_rect = (SDL_Rect){
                loop_start_x,
                loop_top,
                loop_end_x - loop_start_x,
                loop_height
            };
        }
    }

    if (have_loop_region) {
        SDL_Color loop_fill = theme.loop_fill;
        SDL_Color loop_border = theme.loop_border;
        SDL_SetRenderDrawColor(renderer, loop_fill.r, loop_fill.g, loop_fill.b, loop_fill.a);
        SDL_RenderFillRect(renderer, &loop_region_rect);
        SDL_SetRenderDrawColor(renderer, loop_border.r, loop_border.g, loop_border.b, loop_border.a);
        SDL_RenderDrawRect(renderer, &loop_region_rect);
    }

    state->tempo_map.sample_rate = (double)sample_rate;
    BeatGridCache beat_cache;
    TimeGridCache time_cache;
    beat_grid_cache_init(&beat_cache);
    time_grid_cache_init(&time_cache);
    bool beat_mode_ready = state->timeline_view_in_beats
        && state->tempo_map.event_count > 0
        && state->tempo_map.sample_rate > 0.0;
    if (beat_mode_ready) {
        beat_grid_cache_build(&beat_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines,
                              &state->tempo_map,
                              &state->time_signature_map);
    } else {
        time_grid_cache_build(&time_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines);
    }
    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        if (beat_cache.active) {
            beat_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &beat_cache);
        } else if (time_cache.active) {
            time_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &time_cache);
        } else {
            draw_timeline_grid(renderer,
                               content_left,
                               content_width,
                               lane_top,
                               track_height,
                               pixels_per_second,
                               visible_seconds,
                               state->timeline_show_all_grid_lines,
                               window_start,
                               state->timeline_view_in_beats,
                               &state->tempo_map,
                               &state->time_signature_map);
        }
    }
    beat_grid_cache_free(&beat_cache);
    time_grid_cache_free(&time_cache);

    SDL_Color header_bg = theme.lane_header_fill;
    SDL_Color header_border = theme.lane_header_border;
    SDL_Color header_text = theme.text;
    int active_track = (state->selected_track_index >= 0 && state->selected_track_index < track_count)
                           ? state->selected_track_index
                           : -1;
    const TrackNameEditor* editor = &state->track_name_editor;
    SDL_Point mouse_point = {state->mouse_x, state->mouse_y};

    if (tracks && track_count > 0) {
        for (int t = 0; t < track_count; ++t) {
            const EngineTrack* track = &tracks[t];
            int lane_top = track_y + t * (track_height + track_spacing);

            TimelineTrackHeaderLayout header_layout = {0};
            timeline_view_compute_track_header_layout(rect,
                                                      lane_top,
                                                      track_height,
                                                      header_width,
                                                      &header_layout);
            SDL_Rect header_rect = header_layout.header_rect;
            SDL_Color header_fill = header_bg;
            SDL_Color label_color = header_text;
            if (active_track == t) {
                header_fill = theme.lane_header_fill_active;
            }
            if (track && track->muted) {
                header_fill.r = (Uint8)((int)header_fill.r * 3 / 5);
                header_fill.g = (Uint8)((int)header_fill.g * 3 / 5);
                header_fill.b = (Uint8)((int)header_fill.b * 3 / 5);
                label_color = theme.text_muted;
            }
            SDL_SetRenderDrawColor(renderer, header_fill.r, header_fill.g, header_fill.b, header_fill.a);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, header_border.r, header_border.g, header_border.b, header_border.a);
            SDL_RenderDrawRect(renderer, &header_rect);
            char default_label[32];
            const char* label = NULL;
            if (editor && editor->editing && editor->track_index == t) {
                label = editor->buffer;
            } else if (track && track->name[0]) {
                label = track->name;
            }
            if (!label || label[0] == '\0') {
                snprintf(default_label, sizeof(default_label), "Track %d", t + 1);
                label = default_label;
            }

            SDL_Rect mute_rect = header_layout.mute_rect;
            SDL_Rect solo_rect = header_layout.solo_rect;

            bool mute_hover = SDL_PointInRect(&mouse_point, &mute_rect);
            bool solo_hover = SDL_PointInRect(&mouse_point, &solo_rect);
            SDL_Color mute_active = mix_color(theme.lane_header_fill, theme.toggle_active_loop, 0.18f);
            SDL_Color solo_active = mix_color(theme.lane_header_fill, theme.toggle_active_snap, 0.18f);
            mute_active.a = 242;
            solo_active.a = 242;
            draw_toggle_button(renderer, &mute_rect, "M", track ? track->muted : false, mute_hover, mute_active, &theme);
            draw_toggle_button(renderer, &solo_rect, "S", track ? track->solo : false, solo_hover, solo_active, &theme);

            int text_x = header_layout.text_x;
            int text_max_x = header_layout.text_max_x;
            int available_px = text_max_x - text_x;
            char text_buf[ENGINE_CLIP_NAME_MAX];
            const char* display = label;
            if (available_px > 0) {
                fit_label_ellipsis(label, available_px, 1.0f, text_buf, sizeof(text_buf));
                display = text_buf;
            }
            int label_text_y = header_layout.text_y;
            ui_draw_text(renderer, text_x, label_text_y, display, label_color, 1);
            if (editor && editor->editing && editor->track_index == t) {
                float scale = 1.0f;
                int caret_limit = text_max_x;
                if (caret_limit < text_x) caret_limit = text_x;
                char temp[ENGINE_CLIP_NAME_MAX];
                int len = (int)strlen(editor->buffer);
                int caret_x = text_x;
                int target_index = editor->cursor;
                if (target_index < 0) target_index = 0;
                if (target_index > len) target_index = len;
                snprintf(temp, sizeof(temp), "%.*s", target_index, editor->buffer);
                caret_x = text_x + ui_measure_text_width(temp, scale);
                if (caret_x > caret_limit) {
                    caret_x = caret_limit;
                }
                int caret_h = ui_font_line_height(scale);
                SDL_Rect caret_rect = {
                    caret_x,
                    label_text_y,
                    2,
                    caret_h
                };
                SDL_SetRenderDrawColor(renderer, theme.text.r, theme.text.g, theme.text.b, theme.text.a);
                SDL_RenderFillRect(renderer, &caret_rect);
            }

            if (!track || track->clip_count <= 0) {
                continue;
            }

            for (int i = 0; i < track->clip_count; ++i) {
                const EngineClip* clip = &track->clips[i];
                if (!clip) {
                    continue;
                }

                const uint64_t start_frame = clip->timeline_start_frames;
                uint64_t frame_count = clip->duration_frames;
                uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
                uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
                if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                    frame_count = clip_available;
                }
                const double clip_sec = (double)frame_count / (double)sample_rate;
                const double start_sec = (double)start_frame / (double)sample_rate;

                double clip_end_sec = start_sec + clip_sec;
                double visible_start_sec = start_sec;
                if (visible_start_sec < (double)window_start) {
                    visible_start_sec = (double)window_start;
                }
                double visible_end_sec = clip_end_sec;
                double window_end_sec = (double)window_end;
                if (visible_end_sec > window_end_sec) {
                    visible_end_sec = window_end_sec;
                }
                if (visible_end_sec <= visible_start_sec) {
                    continue;
                }

                double visible_offset = visible_start_sec - (double)window_start;
                int clip_x = content_left + (int)round(visible_offset * pixels_per_second);
                int clip_w = (int)round((visible_end_sec - visible_start_sec) * pixels_per_second);
                if (clip_w < 4) {
                    clip_w = 4;
                }

                SDL_Rect clip_rect = {0, 0, 0, 0};
                timeline_view_compute_lane_clip_rect(lane_top,
                                                     track_height,
                                                     clip_x,
                                                     clip_w,
                                                     &clip_rect);

                bool is_selected = timeline_clip_is_selected(state, t, i);
                SDL_SetRenderDrawColor(renderer, theme.clip_fill.r, theme.clip_fill.g, theme.clip_fill.b, theme.clip_fill.a);
                SDL_RenderFillRect(renderer, &clip_rect);

                if (is_selected) {
                    SDL_SetRenderDrawColor(renderer,
                                           theme.clip_border_selected.r,
                                           theme.clip_border_selected.g,
                                           theme.clip_border_selected.b,
                                           theme.clip_border_selected.a);
                } else {
                    SDL_SetRenderDrawColor(renderer, theme.clip_border.r, theme.clip_border.g, theme.clip_border.b, theme.clip_border.a);
                }
                SDL_RenderDrawRect(renderer, &clip_rect);

                const char* media_path = engine_clip_get_media_path(clip);
                if (clip->media && clip->media->frame_count > 0 &&
                    media_path && media_path[0] != '\0') {
                    int wave_label_pad = 4;
                    float label_scale = 1.3f;
                    int label_h = ui_font_line_height(label_scale);
                    SDL_Rect waveform_rect = clip_rect;
                    waveform_rect.y += label_h + wave_label_pad;
                    waveform_rect.h -= label_h + wave_label_pad + 2;
                    if (waveform_rect.h < 8) {
                        waveform_rect = clip_rect;
                    }
                    if (waveform_rect.w > 0 && waveform_rect.h > 0) {
                        double visible_duration = visible_end_sec - visible_start_sec;
                        if (visible_duration > 0.0) {
                            uint64_t local_visible_start = clip->offset_frames;
                            double local_offset_sec = visible_start_sec - start_sec;
                            if (local_offset_sec > 0.0) {
                                local_visible_start += (uint64_t)llround(local_offset_sec * (double)sample_rate);
                            }
                            uint64_t max_frame = clip->offset_frames + frame_count;
                            if (local_visible_start > max_frame) {
                                local_visible_start = max_frame;
                            }
                            uint64_t visible_frames = (uint64_t)llround(visible_duration * (double)sample_rate);
                            if (visible_frames == 0) {
                                visible_frames = 1;
                            }
                            if (local_visible_start + visible_frames > max_frame) {
                                visible_frames = max_frame > local_visible_start ? max_frame - local_visible_start : 0;
                            }
                            if (visible_frames > 0) {
                                SDL_Color wave_color = theme.waveform;
                                bool rendered = false;
                                if (state->inspector.waveform.use_kit_viz_waveform) {
                                    DawKitVizWaveformRequest request = {
                                        .renderer = renderer,
                                        .cache = &state->waveform_cache,
                                        .clip = clip->media,
                                        .source_path = media_path,
                                        .target_rect = &waveform_rect,
                                        .view_start_frame = local_visible_start,
                                        .view_frame_count = visible_frames,
                                        .color = wave_color,
                                    };
                                    rendered = daw_kit_viz_render_waveform(&request);
                                }
                                if (!rendered) {
                                    waveform_render_view(renderer,
                                                         &state->waveform_cache,
                                                         clip->media,
                                                         media_path,
                                                         &waveform_rect,
                                                         local_visible_start,
                                                         visible_frames,
                                                         wave_color);
                                }
                            }
                        }
                    }
                }

                if (sample_rate > 0) {
                    int fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * pixels_per_second);
                    int fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * pixels_per_second);
                    int clip_clip_left_px = (int)round((visible_start_sec - start_sec) * pixels_per_second);
                    int clip_offset_px = clip_clip_left_px;
                    if (clip_offset_px < 0) clip_offset_px = 0;
                    double clip_total_px = (clip_end_sec - start_sec) * pixels_per_second;
                    EngineFadeCurve fade_in_curve = clip->fade_in_curve;
                    EngineFadeCurve fade_out_curve = clip->fade_out_curve;

                    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
                    int fade_in_draw = 0;
                    if (fade_in_px > 0 && clip_offset_px < fade_in_px) {
                        fade_in_draw = fade_in_px - clip_offset_px;
                        if (fade_in_draw > clip_rect.w) fade_in_draw = clip_rect.w;
                    }
                    if (fade_in_draw > 0 && fade_in_px > 0) {
                        SDL_SetRenderDrawColor(renderer, theme.text.r, theme.text.g, theme.text.b, 28);
                        for (int fx = 0; fx < fade_in_draw; ++fx) {
                            float tf = (float)(clip_offset_px + fx) / (float)fade_in_px;
                            float gain = ui_fade_curve_eval(fade_in_curve, tf);
                            float overlay = 1.0f - gain;
                            int h = (int)(overlay * clip_rect.h);
                            SDL_RenderDrawLine(renderer,
                                               clip_rect.x + fx,
                                               clip_rect.y,
                                               clip_rect.x + fx,
                                               clip_rect.y + h);
                        }
                    }

                    int fade_out_draw = 0;
                    double fade_out_start_px = clip_total_px - (double)fade_out_px;
                    if (fade_out_px > 0 && clip_total_px > 0.0) {
                        int visible_right = clip_offset_px + clip_rect.w;
                        if ((double)visible_right > fade_out_start_px) {
                            int start_x = (int)floor(fade_out_start_px - (double)clip_offset_px);
                            if (start_x < 0) start_x = 0;
                            if (start_x < clip_rect.w) {
                                fade_out_draw = clip_rect.w - start_x;
                            }
                        }
                    }
                    if (fade_out_draw > 0 && fade_out_px > 0) {
                        SDL_SetRenderDrawColor(renderer, theme.text.r, theme.text.g, theme.text.b, 28);
                        int start_x = clip_rect.w - fade_out_draw;
                        for (int fx = 0; fx < fade_out_draw; ++fx) {
                            int local_px = clip_offset_px + start_x + fx;
                            float tf = (float)((double)local_px - fade_out_start_px) / (float)fade_out_px;
                            float gain = ui_fade_curve_eval(fade_out_curve, tf);
                            int h = (int)(gain * clip_rect.h);
                            int px = clip_rect.x + start_x + fx;
                            SDL_RenderDrawLine(renderer,
                                               px,
                                               clip_rect.y,
                                               px,
                                               clip_rect.y + h);
                        }
                    }
                    ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
                }

                SDL_Color text_color = theme.clip_text;
                char name_buf[ENGINE_CLIP_NAME_MAX];
                const char* name = timeline_clip_display_name(state, clip, name_buf, sizeof(name_buf));
                const TimelineTextMetrics* metrics = timeline_text_metrics();
                float scale_f = 1.0f;
                timeline_draw_text_in_rect_clipped(renderer,
                                                   &clip_rect,
                                                   name,
                                                   text_color,
                                                   metrics->clip_label_pad_x,
                                                   metrics->label_pad_y,
                                                   scale_f);

                if (is_selected) {
                    SDL_SetRenderDrawColor(renderer,
                                           theme.clip_border_selected.r,
                                           theme.clip_border_selected.g,
                                           theme.clip_border_selected.b,
                                           theme.clip_border_selected.a);
                    SDL_Rect left_handle = {
                        clip_rect.x - 2,
                        clip_rect.y,
                        CLIP_HANDLE_WIDTH,
                        clip_rect.h,
                    };
                    SDL_Rect right_handle = {
                        clip_rect.x + clip_rect.w - CLIP_HANDLE_WIDTH + 2,
                        clip_rect.y,
                        CLIP_HANDLE_WIDTH,
                        clip_rect.h,
                    };
                    SDL_RenderFillRect(renderer, &left_handle);
                    SDL_RenderFillRect(renderer, &right_handle);
                }
            }
        }
    }

    if (state->timeline_tempo_overlay_enabled) {
        SDL_Rect tempo_rect = {0, 0, 0, 0};
        if (timeline_compute_tempo_overlay_rect(rect,
                                                track_y,
                                                track_height,
                                                content_left,
                                                content_width,
                                                &tempo_rect)) {
            draw_timeline_tempo_overlay(renderer,
                                        &tempo_rect,
                                        &state->tempo_map,
                                        state->tempo_overlay_ui.event_index,
                                        window_start,
                                        window_end,
                                        content_left,
                                        (float)pixels_per_second,
                                        state->timeline_automation_labels_enabled);
        }
    }

    if (state->timeline_automation_mode) {
        draw_timeline_automation_overlay(renderer,
                                         rect,
                                         track_y,
                                         track_height,
                                         track_spacing,
                                         track_count,
                                         content_left,
                                         content_width);
        if (tracks && track_count > 0 && sample_rate > 0) {
            for (int t = 0; t < track_count; ++t) {
                const EngineTrack* track = &tracks[t];
                int lane_top = track_y + t * (track_height + track_spacing);
                if (!track || track->clip_count <= 0) {
                    continue;
                }
                for (int i = 0; i < track->clip_count; ++i) {
                    const EngineClip* clip = &track->clips[i];
                    if (!clip) {
                        continue;
                    }
                    uint64_t frame_count = clip->duration_frames;
                    uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
                    uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
                    if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                        frame_count = clip_available;
                    }
                    if (frame_count == 0) {
                        continue;
                    }
                    const double clip_sec = (double)frame_count / (double)sample_rate;
                    const double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                    int clip_x = content_left + (int)round((start_sec - (double)window_start) * pixels_per_second);
                    int clip_w = (int)round(clip_sec * pixels_per_second);
                    if (clip_w < 4) {
                        clip_w = 4;
                    }
                    SDL_Rect clip_rect = {0, 0, 0, 0};
                    timeline_view_compute_lane_clip_rect(lane_top,
                                                         track_height,
                                                         clip_x,
                                                         clip_w,
                                                         &clip_rect);
                    draw_timeline_clip_automation(renderer,
                                                  &clip_rect,
                                                  clip,
                                                  frame_count,
                                                  state->automation_ui.target,
                                                  &state->automation_ui,
                                                  t,
                                                  i,
                                                  sample_rate,
                                                  window_start,
                                                  window_end,
                                                  content_left,
                                                  content_left + content_width,
                                                  (float)pixels_per_second);
                }
            }
        }
    }

    if (have_loop_controls) {
        SDL_Color start_color = controls->adjusting_loop_start ? theme.text : theme.loop_handle_start;
        SDL_Color end_color = controls->adjusting_loop_end ? theme.text : theme.loop_handle_end;
        SDL_Color start_border = (controls->loop_start_hovered || controls->adjusting_loop_start)
                                     ? theme.clip_border_selected
                                     : theme.loop_handle_border;
        SDL_Color end_border = (controls->loop_end_hovered || controls->adjusting_loop_end)
                                   ? theme.clip_border_selected
                                   : theme.loop_handle_border;
        SDL_Rect start_draw = controls->loop_start_rect;
        SDL_Rect end_draw = controls->loop_end_rect;
        int shrink_w = 4;
        int shrink_h = 4;
        if (start_draw.w > shrink_w) {
            start_draw.x += shrink_w / 2;
            start_draw.w -= shrink_w;
        }
        if (start_draw.h > shrink_h) {
            start_draw.y += shrink_h / 2;
            start_draw.h -= shrink_h;
        }
        if (end_draw.w > shrink_w) {
            end_draw.x += shrink_w / 2;
            end_draw.w -= shrink_w;
        }
        if (end_draw.h > shrink_h) {
            end_draw.y += shrink_h / 2;
            end_draw.h -= shrink_h;
        }
        SDL_SetRenderDrawColor(renderer, start_color.r, start_color.g, start_color.b, start_color.a);
        SDL_RenderFillRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, start_border.r, start_border.g, start_border.b, start_border.a);
        SDL_RenderDrawRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, end_color.r, end_color.g, end_color.b, end_color.a);
        SDL_RenderFillRect(renderer, &end_draw);
        SDL_SetRenderDrawColor(renderer, end_border.r, end_border.g, end_border.b, end_border.a);
        SDL_RenderDrawRect(renderer, &end_draw);

        // Loop labels (time or beat) above handles.
        char start_label[32];
        char end_label[32];
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        format_timeline_label(loop_start_sec,
                              state->timeline_view_in_beats,
                              &state->tempo_map,
                              &state->time_signature_map,
                              start_label,
                              sizeof(start_label));
        format_timeline_label(loop_end_sec,
                              state->timeline_view_in_beats,
                              &state->tempo_map,
                              &state->time_signature_map,
                              end_label,
                              sizeof(end_label));
        SDL_Color loop_label_color = theme.loop_label;
        int label_scale = 1;
        int start_w = ui_measure_text_width(start_label, label_scale);
        int end_w = ui_measure_text_width(end_label, label_scale);
        int label_y = controls->loop_start_rect.y - ui_font_line_height(label_scale) - 2;
        if (label_y < rect->y) label_y = rect->y;
        int start_x = controls->loop_start_rect.x + (controls->loop_start_rect.w - start_w) / 2;
        int end_x = controls->loop_end_rect.x + (controls->loop_end_rect.w - end_w) / 2;
        ui_draw_text(renderer, start_x, label_y, start_label, loop_label_color, label_scale);
        ui_draw_text(renderer, end_x, label_y, end_label, loop_label_color, label_scale);
    }

    uint64_t playhead_frame = engine_get_transport_frame(engine);
    if (state->bounce_active) {
        uint64_t start = state->bounce_start_frame;
        uint64_t end = state->bounce_end_frame > start ? state->bounce_end_frame : start;
        playhead_frame = start + state->bounce_progress_frames;
        if (playhead_frame > end) {
            playhead_frame = end;
        }
    }
    const double transport_sec = (double)playhead_frame / (double)sample_rate;
    float playhead_offset = (float)((transport_sec - window_start) / visible_seconds) * (float)content_width;
    playhead_offset = clamp_float(playhead_offset, 0.0f, (float)content_width);
    int playhead_x = content_left + (int)roundf(playhead_offset);
    int timeline_bottom = track_y + (track_count > 0
                                      ? (track_count * (track_height + track_spacing)) - track_spacing
                                      : track_height);
    SDL_SetRenderDrawColor(renderer, theme.playhead.r, theme.playhead.g, theme.playhead.b, theme.playhead.a);
    SDL_RenderDrawLine(renderer, playhead_x, track_y - 8, playhead_x, timeline_bottom + 8);

    if (state->timeline_drag.active) {
        const TimelineDragState* drag = &state->timeline_drag;
        int dest_track = drag->destination_track_index;
        if (dest_track < 0) {
            dest_track = drag->track_index;
        }
        if (dest_track >= track_count) {
            dest_track = track_count > 0 ? track_count - 1 : 0;
        }
        if (dest_track >= 0 && dest_track < track_count && dest_track != drag->track_index) {
            double start_sec = drag->current_start_seconds;
            double duration_sec = drag->current_duration_seconds;
            if (duration_sec <= 0.0) {
                duration_sec = 1.0 / (double)sample_rate;
            }
            double ghost_offset = start_sec - (double)window_start;
            int ghost_x = content_left + (int)round(ghost_offset * pixels_per_second);
            int ghost_w = (int)round(duration_sec * pixels_per_second);
            if (ghost_w < 4) {
                ghost_w = 4;
            }
            int lane_top = track_y + dest_track * (track_height + track_spacing);
            SDL_Rect ghost_rect = {0, 0, 0, 0};
            timeline_view_compute_lane_clip_rect(lane_top,
                                                 track_height,
                                                 ghost_x,
                                                 ghost_w,
                                                 &ghost_rect);
            SDL_SetRenderDrawColor(renderer, 170, 200, 255, MOVE_GHOST_ALPHA);
            SDL_RenderFillRect(renderer, &ghost_rect);
            SDL_SetRenderDrawColor(renderer, 210, 230, 255, 200);
            SDL_RenderDrawRect(renderer, &ghost_rect);

            const EngineTrack* tracks = engine_get_tracks(state->engine);
            int count = engine_get_track_count(state->engine);
            if (tracks && drag->track_index >= 0 && drag->track_index < count) {
                const EngineTrack* src_track = &tracks[drag->track_index];
                if (src_track && drag->clip_index >= 0 && drag->clip_index < src_track->clip_count) {
                    const EngineClip* clip = &src_track->clips[drag->clip_index];
                    if (clip && clip->name[0]) {
                        const TimelineTextMetrics* metrics = timeline_text_metrics();
                        SDL_Color text_color = {220, 230, 255, 255};
                        timeline_draw_text_in_rect_clipped(renderer,
                                                           &ghost_rect,
                                                           clip->name,
                                                           text_color,
                                                           metrics->overlay_label_pad_x,
                                                           metrics->label_pad_y,
                                                           1.0f);
                    }
                }
            }

            if (drag->multi_move && drag->multi_clip_count > 1) {
                const TimelineTextMetrics* metrics = timeline_text_metrics();
                char label[64];
                snprintf(label, sizeof(label), "Moving %d clips", drag->multi_clip_count);
                int text_h = metrics->text_h_1x;
                int label_x = ghost_rect.x + metrics->overlay_edge_pad;
                int label_y = ghost_rect.y - text_h - metrics->overlay_badge_gap;
                int min_x = content_left + metrics->overlay_edge_pad;
                int max_x = content_left + content_width - metrics->overlay_edge_pad;
                int max_w;
                if (label_x < min_x) {
                    label_x = min_x;
                }
                if (label_y < track_y - (metrics->overlay_badge_gap * 2)) {
                    label_y = ghost_rect.y + ghost_rect.h + metrics->overlay_badge_gap;
                }
                max_w = max_x - label_x;
                SDL_Color badge = {235, 240, 255, 255};
                if (max_w > 0) {
                    ui_draw_text_clipped(renderer, label_x, label_y, label, badge, 1.0f, max_w);
                }
            }
        }
    }

    if (state->timeline_drop_active) {
        float start_sec = state->timeline_drop_seconds_snapped >= 0.0f
                              ? state->timeline_drop_seconds_snapped
                              : state->timeline_drop_seconds;
        float duration_sec = state->timeline_drop_preview_duration > 0.0f
                                 ? state->timeline_drop_preview_duration
                                 : 1.0f;
        float end_sec = start_sec + duration_sec;

        float visible_start = start_sec < window_start ? window_start : start_sec;
        float visible_end = end_sec > window_end ? window_end : end_sec;
        if (visible_end <= visible_start) {
            visible_end = visible_start + 0.01f;
        }

        int ghost_x = content_left + (int)roundf((visible_start - window_start) * pixels_per_second);
        int ghost_w = (int)roundf((visible_end - visible_start) * pixels_per_second);
        if (ghost_w < 6) {
            ghost_w = 6;
        }
        if (ghost_x < content_left) {
            ghost_w -= (content_left - ghost_x);
            ghost_x = content_left;
        }
        if (ghost_x + ghost_w > content_left + content_width) {
            ghost_w = content_left + content_width - ghost_x;
        }
        if (ghost_w < 1) {
            ghost_w = 1;
        }
        int drop_track = state->timeline_drop_track_index;
        if (drop_track < 0) {
            drop_track = 0;
        }
        if (track_count == 0) {
            drop_track = 0;
        }
        int lane_top = track_y + drop_track * (track_height + track_spacing);
        bool is_preview_track = (track_count == 0) || (drop_track >= track_count);
        if (is_preview_track) {
            TimelineTrackHeaderLayout preview_header = {0};
            timeline_view_compute_track_header_layout(rect,
                                                      lane_top,
                                                      track_height,
                                                      header_width,
                                                      &preview_header);
            SDL_Rect header_rect = preview_header.header_rect;
            SDL_SetRenderDrawColor(renderer, 46, 54, 72, 220);
            SDL_RenderFillRect(renderer, &header_rect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 200);
            SDL_RenderDrawRect(renderer, &header_rect);
            SDL_Color header_text = {210, 220, 235, 230};
            int preview_text_w = preview_header.text_max_x - preview_header.text_x;
            if (preview_text_w > 0) {
                ui_draw_text_clipped(renderer,
                                     preview_header.text_x,
                                     preview_header.text_y,
                                     "New Track",
                                     header_text,
                                     1.0f,
                                     preview_text_w);
            }

            SDL_Rect lane_rect = {
                content_left,
                lane_top,
                content_width,
                track_height
            };
            SDL_SetRenderDrawColor(renderer, 52, 62, 86, 120);
            SDL_RenderFillRect(renderer, &lane_rect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 140);
            SDL_RenderDrawRect(renderer, &lane_rect);

            draw_timeline_grid(renderer,
                               content_left,
                               content_width,
                               lane_top,
                               track_height,
                               pixels_per_second,
                               visible_seconds,
                               state->timeline_show_all_grid_lines,
                               window_start,
                               state->timeline_view_in_beats,
                               &state->tempo_map,
                               &state->time_signature_map);
        }
        SDL_Rect ghost_rect = {0, 0, 0, 0};
        timeline_view_compute_lane_clip_rect(lane_top,
                                             track_height,
                                             ghost_x,
                                             ghost_w,
                                             &ghost_rect);
        SDL_SetRenderDrawColor(renderer, 120, 160, 220, 120);
        SDL_RenderFillRect(renderer, &ghost_rect);
        SDL_SetRenderDrawColor(renderer, 180, 210, 255, 180);
        SDL_RenderDrawRect(renderer, &ghost_rect);

        if (state->timeline_drop_label[0] != '\0') {
            const TimelineTextMetrics* metrics = timeline_text_metrics();
            SDL_Color ghost_text = {220, 230, 255, 255};
            timeline_draw_text_in_rect_clipped(renderer,
                                               &ghost_rect,
                                               state->timeline_drop_label,
                                               ghost_text,
                                               metrics->overlay_label_pad_x,
                                               metrics->label_pad_y,
                                               1.0f);
        }
    }

    if (state->timeline_marquee_active && state->timeline_marquee_rect.w != 0 && state->timeline_marquee_rect.h != 0) {
        SDL_Rect m = state->timeline_marquee_rect;
        ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 120, 170, 240, 60);
        SDL_RenderFillRect(renderer, &m);
        SDL_SetRenderDrawColor(renderer, 120, 180, 255, 180);
        SDL_RenderDrawRect(renderer, &m);
        ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
    }
}
