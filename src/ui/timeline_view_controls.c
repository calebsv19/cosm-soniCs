#include "ui/timeline_view_controls.h"

#include "ui/font.h"
#include "ui/timeline_view.h"

#include <string.h>

typedef struct TimelineTextMetrics {
    bool valid;
    int text_h_1x;
    int width_plus;
    int width_minus;
    int width_midi_region;
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
    metrics.width_midi_region = ui_measure_text_width("+ MIDI", 1.0f);
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
    if (strcmp(label, "+ MIDI") == 0) return metrics->width_midi_region;
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

static int timeline_button_width(const char* label, int min_width) {
    int width = timeline_label_width_cached(label) + 14;
    if (width < min_width) {
        width = min_width;
    }
    return width;
}

int timeline_view_controls_compute_layout(int timeline_x,
                                          int timeline_y,
                                          int timeline_width,
                                          TimelineControlsUI* out_controls) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    enum { k_button_count = 9 };
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
        &local_rects[4], &local_rects[5], &local_rects[6], &local_rects[7],
        &local_rects[8]
    };
    if (out_controls) {
        SDL_Rect zero = {0, 0, 0, 0};
        out_controls->add_rect = zero;
        out_controls->remove_rect = zero;
        out_controls->midi_region_rect = zero;
        out_controls->loop_toggle_rect = zero;
        out_controls->snap_toggle_rect = zero;
        out_controls->automation_toggle_rect = zero;
        out_controls->automation_target_rect = zero;
        out_controls->tempo_toggle_rect = zero;
        out_controls->automation_label_toggle_rect = zero;
        button_rects[0] = &out_controls->add_rect;
        button_rects[1] = &out_controls->remove_rect;
        button_rects[2] = &out_controls->midi_region_rect;
        button_rects[3] = &out_controls->loop_toggle_rect;
        button_rects[4] = &out_controls->snap_toggle_rect;
        button_rects[5] = &out_controls->automation_toggle_rect;
        button_rects[6] = &out_controls->automation_target_rect;
        button_rects[7] = &out_controls->tempo_toggle_rect;
        button_rects[8] = &out_controls->automation_label_toggle_rect;
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
    widths[2] = timeline_button_width("+ MIDI", 54);
    widths[3] = timeline_button_width("LOOP", 36);
    widths[4] = timeline_button_width("SNAP", 40);
    widths[5] = timeline_button_width("AUTO", 44);
    widths[6] = timeline_button_width("PAN", 40);
    {
        int vol_w = timeline_button_width("VOL", 40);
        if (vol_w > widths[6]) {
            widths[6] = vol_w;
        }
    }
    widths[7] = timeline_button_width("TEMPO", 52);
    widths[8] = timeline_button_width("VAL", 40);

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
    return timeline_view_controls_compute_layout(0, 0, 0, NULL);
}

int timeline_view_controls_height_for_width(int timeline_width) {
    return timeline_view_controls_compute_layout(0, 0, timeline_width, NULL);
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

void timeline_view_draw_button(SDL_Renderer* renderer,
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

void timeline_view_get_text_metrics_snapshot(TimelineViewTextMetricsSnapshot* out_metrics) {
    const TimelineTextMetrics* metrics = timeline_text_metrics();
    if (!out_metrics) {
        return;
    }
    out_metrics->text_h_1x = metrics->text_h_1x;
    out_metrics->clip_label_pad_x = metrics->clip_label_pad_x;
    out_metrics->overlay_label_pad_x = metrics->overlay_label_pad_x;
    out_metrics->label_pad_y = metrics->label_pad_y;
    out_metrics->overlay_badge_gap = metrics->overlay_badge_gap;
    out_metrics->overlay_edge_pad = metrics->overlay_edge_pad;
}
