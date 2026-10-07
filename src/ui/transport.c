#include "ui/control_chrome.h"
#include "ui/text_edit.h"
#include "ui/transport.h"
#include "app/audio_recording.h"
#include "app_state.h"
#include "ui/timeline_view.h"
#include "time/tempo.h"

#include "ui/font.h"
#include "ui/daw_ui_button.h"
#include "ui/transport_controls.h"
#include "ui/shared_theme_font_adapter.h"

#include <string.h>
#include <math.h>

static void fill_fallback_theme_palette(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    *palette = (DawThemePalette){
        .menu_fill = {26, 26, 34, 255},
        .timeline_fill = {32, 32, 40, 255},
        .inspector_fill = {28, 28, 36, 255},
        .library_fill = {24, 24, 32, 255},
        .pane_border = {90, 90, 110, 255},
        .pane_highlight_fill = {42, 46, 58, 255},
        .pane_highlight_border = {120, 160, 220, 255},
        .title_text = {220, 220, 230, 255},
        .text_primary = {220, 220, 230, 255},
        .text_muted = {200, 200, 210, 255},
        .control_fill = {60, 60, 70, 255},
        .control_hover_fill = {90, 90, 110, 255},
        .control_active_fill = {120, 140, 180, 255},
        .control_border = {120, 120, 128, 255},
        .slider_track = {36, 36, 44, 255},
        .slider_handle = {180, 210, 255, 255},
        .slider_handle_hover = {210, 230, 255, 255},
        .timeline_border = {90, 90, 110, 255},
        .grid_minor = {65, 65, 85, 255},
        .grid_sub = {66, 70, 100, 200},
        .grid_major = {80, 82, 115, 255},
        .grid_downbeat = {90, 100, 130, 255},
        .selection_fill = {110, 140, 190, 180},
        .accent_primary = {120, 160, 220, 255},
        .accent_warning = {230, 190, 110, 255},
        .accent_error = {220, 110, 110, 255}
    };
}

static void resolve_transport_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        fill_fallback_theme_palette(palette);
    }
}

void transport_ui_init(TransportUI* ui) {
    if (!ui) {
        return;
    }
    ui->panel_rect = (SDL_Rect){0, 0, 0, 0};
    ui->load_rect = (SDL_Rect){0, 0, 0, 0};
    ui->save_rect = (SDL_Rect){0, 0, 0, 0};
    ui->play_rect = (SDL_Rect){0, 0, 0, 0};
    ui->stop_rect = (SDL_Rect){0, 0, 0, 0};
    ui->bpm_rect = (SDL_Rect){0, 0, 0, 0};
    ui->ts_rect = (SDL_Rect){0, 0, 0, 0};
    ui->ts_num_rect = (SDL_Rect){0, 0, 0, 0};
    ui->ts_den_rect = (SDL_Rect){0, 0, 0, 0};
    ui->beat_toggle_rect = (SDL_Rect){0, 0, 0, 0};
    ui->play_hovered = false;
    ui->stop_hovered = false;
    ui->load_hovered = false;
    ui->save_hovered = false;
    ui->grid_rect = (SDL_Rect){0, 0, 0, 0};
    ui->time_label_rect = (SDL_Rect){0, 0, 0, 0};
    ui->seek_track_rect = (SDL_Rect){0, 0, 0, 0};
    ui->seek_handle_rect = (SDL_Rect){0, 0, 0, 0};
    ui->window_track_rect = (SDL_Rect){0, 0, 0, 0};
    ui->window_handle_rect = (SDL_Rect){0, 0, 0, 0};
    ui->horiz_track_rect = (SDL_Rect){0,0,0,0};
    ui->horiz_handle_rect = (SDL_Rect){0,0,0,0};
    ui->vert_track_rect = (SDL_Rect){0,0,0,0};
    ui->vert_handle_rect = (SDL_Rect){0,0,0,0};
    ui->grid_hovered = false;
    ui->bpm_hovered = false;
    ui->ts_hovered = false;
    ui->beat_toggle_hovered = false;
    ui->seek_hovered = false;
    ui->window_hovered = false;
    ui->horiz_hovered = false;
    ui->vert_hovered = false;
    ui->adjusting_horizontal = false;
    ui->adjusting_vertical = false;
    ui->adjusting_seek = false;
    ui->adjusting_window = false;
}

void transport_ui_layout(TransportUI* ui, const SDL_Rect* container) {
    if (!ui || !container) {
        return;
    }
    ui->panel_rect = *container;

    const float button_scale = 1.0f;
    const int button_pad_x = 10;
    const int button_pad_y = 4;
    const int text_h = ui_font_line_height(button_scale);
    int button_width = ui_measure_text_width("LOAD", button_scale);
    int measured = ui_measure_text_width("SAVE", button_scale);
    if (measured > button_width) button_width = measured;
    measured = ui_measure_text_width("PLAY", button_scale);
    if (measured > button_width) button_width = measured;
    measured = ui_measure_text_width("STOP", button_scale);
    if (measured > button_width) button_width = measured;
    button_width += button_pad_x * 2;
    if (button_width < 56) {
        button_width = 56;
    }
    int button_height = text_h + button_pad_y * 2;
    if (button_height < 20) {
        button_height = 20;
    }
    const int padding = 10;
    const int button_v_gap = 3;
    int label_width = ui_measure_text_width("000:00.000", button_scale) + 20;
    if (label_width < 108) {
        label_width = 108;
    }
    int bpm_height = text_h + 6;
    if (bpm_height < 18) {
        bpm_height = 18;
    }
    const int bpm_gap = 3;
    const int seek_height = 7;
    const int handle_width = 10;

    int x = container->x + padding;
    int button_stack_height = button_height * 2 + button_v_gap;
    int buttons_y = container->y + (container->h - button_stack_height) / 2;

    ui->load_rect = (SDL_Rect){x, buttons_y, button_width, button_height};
    ui->play_rect = (SDL_Rect){x, buttons_y + button_height + button_v_gap, button_width, button_height};

    ui->save_rect = (SDL_Rect){x + button_width + padding, buttons_y, button_width, button_height};
    ui->stop_rect = (SDL_Rect){x + button_width + padding, buttons_y + button_height + button_v_gap, button_width, button_height};

    const int group_spacing = padding * 2;
    int grid_width = ui_measure_text_width("GRID:AUTO", button_scale) + button_pad_x * 2;
    measured = ui_measure_text_width("GRID:ALL", button_scale) + button_pad_x * 2;
    if (measured > grid_width) {
        grid_width = measured;
    }
    if (grid_width < 80) {
        grid_width = 80;
    }
    int slider_width = 132;
    int slider_height = 7;
    int slider_y = container->y + (container->h - slider_height) / 2 - 6;
    int slider_row_gap = 18;

    x = ui->stop_rect.x + ui->stop_rect.w + padding;
    int stack_h = bpm_height + bpm_gap + button_height;
    int stack_y = container->y + (container->h - stack_h) / 2;
    int bpm_y = stack_y;
    int label_y = bpm_y + bpm_height + bpm_gap;
    ui->time_label_rect = (SDL_Rect){x, label_y, label_width, button_height};
    int bpm_ts_gap = 4;
    int bpm_width = (label_width - bpm_ts_gap) / 2;
    int ts_width = label_width - bpm_width - bpm_ts_gap;
    ui->bpm_rect = (SDL_Rect){x, bpm_y, bpm_width, bpm_height};
    ui->ts_rect = (SDL_Rect){x + bpm_width + bpm_ts_gap, bpm_y, ts_width, bpm_height};
    int ts_gap = 2;
    int ts_half = (ui->ts_rect.w - ts_gap) / 2;
    ui->ts_num_rect = (SDL_Rect){ui->ts_rect.x, ui->ts_rect.y, ts_half, ui->ts_rect.h};
    ui->ts_den_rect = (SDL_Rect){ui->ts_rect.x + ts_half + ts_gap, ui->ts_rect.y, ts_half, ui->ts_rect.h};
    int toggle_size = button_height;
    int toggle_y = container->y + (container->h - toggle_size) / 2;
    ui->beat_toggle_rect = (SDL_Rect){x + label_width + 6, toggle_y, 22, toggle_size};
    x += label_width + padding + ui->beat_toggle_rect.w + 6;


    int right_edge = container->x + container->w - padding;
    int grid_x = right_edge - grid_width;
    ui->grid_rect = (SDL_Rect){grid_x, container->y + (container->h - button_height) / 2, grid_width, button_height};

    int fit_button_width = ui_measure_text_width("W", button_scale) + 8;
    int fit_button_height = text_h + 4;
    if (fit_button_width < 18) {
        fit_button_width = 18;
    }
    if (fit_button_height < 14) {
        fit_button_height = 14;
    }
    {
        int max_fit_height = (container->h - 4) / 2;
        if (max_fit_height < 1) {
            max_fit_height = 1;
        }
        if (fit_button_height > max_fit_height) {
            fit_button_height = max_fit_height;
        }
    }
    int fit_spacing = padding / 2;

    // Leave a little breathing room between sliders and the grid button.
    int slider_limit = grid_x - fit_spacing - fit_button_width - 12;
    if (slider_limit < x + group_spacing + 80) {
        slider_limit = x + group_spacing + 80;
    }

    int total_space = slider_limit - x;
    if (total_space < 160) {
        total_space = 160;
    }

    int min_slider = 80;
    int min_seek = 100;
    if (total_space < min_slider + min_seek + group_spacing) {
        min_seek = total_space - min_slider - group_spacing;
        if (min_seek < 40) {
            min_seek = total_space / 2;
        }
    }

    int seek_width = (int)(total_space * 0.6f);
    if (seek_width < min_seek) seek_width = min_seek;
    if (seek_width > total_space - min_slider - group_spacing) {
        seek_width = total_space - min_slider - group_spacing;
    }
    if (seek_width < 40) seek_width = 40;

    slider_width = total_space - seek_width - group_spacing;
    if (slider_width < min_slider) {
        slider_width = min_slider;
        seek_width = total_space - slider_width - group_spacing;
        if (seek_width < 40) seek_width = 40;
    }

    int seek_y = slider_y;
    int window_y = seek_y + slider_row_gap;
    ui->seek_track_rect = (SDL_Rect){x, seek_y, seek_width, seek_height};
    ui->seek_handle_rect = (SDL_Rect){x, seek_y - 4, handle_width, seek_height + 8};
    ui->window_track_rect = (SDL_Rect){x, window_y, seek_width, seek_height};
    ui->window_handle_rect = (SDL_Rect){x, window_y - 4, handle_width, seek_height + 8};

    int slider_x = x + seek_width + group_spacing;
    if (slider_x + slider_width > slider_limit) {
        slider_x = slider_limit - slider_width;
    }
    if (slider_x < x + group_spacing) {
        slider_x = x + group_spacing;
        slider_width = slider_limit - slider_x;
    }

    ui->horiz_track_rect = (SDL_Rect){slider_x, slider_y, slider_width, slider_height};
    ui->horiz_handle_rect = (SDL_Rect){slider_x, slider_y - 4, handle_width, slider_height + 8};

    int button_stack_x = slider_limit + fit_spacing;
    if (button_stack_x + fit_button_width > grid_x) {
        button_stack_x = grid_x - fit_button_width;
    }
    {
        int fit_gap = text_h / 4;
        if (fit_gap < 2) {
            fit_gap = 2;
        }
        int fit_stack_h = fit_button_height * 2 + fit_gap;
        int fit_stack_y = container->y + (container->h - fit_stack_h) / 2;
        if (fit_stack_y < container->y) {
            fit_stack_y = container->y;
        }
        if (fit_stack_y + fit_stack_h > container->y + container->h) {
            fit_stack_y = container->y + container->h - fit_stack_h;
        }
        ui->fit_width_rect = (SDL_Rect){button_stack_x, fit_stack_y, fit_button_width, fit_button_height};
        ui->fit_height_rect = (SDL_Rect){button_stack_x,
                                         fit_stack_y + fit_button_height + fit_gap,
                                         fit_button_width,
                                         fit_button_height};
    }

    int vert_slider_y = slider_y + slider_row_gap;
    ui->vert_track_rect = (SDL_Rect){slider_x, vert_slider_y, slider_width, slider_height};
    ui->vert_handle_rect = (SDL_Rect){slider_x, vert_slider_y - 4, handle_width, slider_height + 8};
}

void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y) {
    if (!ui) {
        return;
    }
    SDL_Point p = {mouse_x, mouse_y};
    ui->load_hovered = SDL_PointInRect(&p, &ui->load_rect);
    ui->save_hovered = SDL_PointInRect(&p, &ui->save_rect);
    ui->play_hovered = SDL_PointInRect(&p, &ui->play_rect);
    ui->stop_hovered = SDL_PointInRect(&p, &ui->stop_rect);
    ui->grid_hovered = SDL_PointInRect(&p, &ui->grid_rect);
    ui->bpm_hovered = SDL_PointInRect(&p, &ui->bpm_rect);
    ui->ts_hovered = SDL_PointInRect(&p, &ui->ts_rect)
        || SDL_PointInRect(&p, &ui->ts_num_rect)
        || SDL_PointInRect(&p, &ui->ts_den_rect);
    ui->beat_toggle_hovered = SDL_PointInRect(&p, &ui->beat_toggle_rect);
    ui->seek_hovered = daw_slider_hit(ui->seek_track_rect,mouse_x,mouse_y) || SDL_PointInRect(&p, &ui->seek_handle_rect);
    ui->window_hovered = daw_slider_hit(ui->window_track_rect,mouse_x,mouse_y) || SDL_PointInRect(&p, &ui->window_handle_rect);
    ui->horiz_hovered = daw_slider_hit(ui->horiz_track_rect,mouse_x,mouse_y);
    ui->vert_hovered = daw_slider_hit(ui->vert_track_rect,mouse_x,mouse_y);
    ui->fit_width_hovered = SDL_PointInRect(&p, &ui->fit_width_rect);
    ui->fit_height_hovered = SDL_PointInRect(&p, &ui->fit_height_rect);
}

static uint64_t compute_total_frames(const AppState* state) {
    if (!state || !state->engine) {
        return 0;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    if (!tracks || track_count <= 0) {
        return 0;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    return max_frames;
}

void transport_ui_sync(TransportUI* ui, const AppState* state) {
    if (!ui || !state) {
        return;
    }
    daw_transport_controls_sync(ui,state);
    if (ui->horiz_track_rect.w <= 0 || ui->vert_track_rect.w <= 0 || ui->seek_track_rect.w <= 0 || ui->window_track_rect.w <= 0) {
        return;
    }

    float visible_seconds = state->timeline_visible_seconds;
    if (visible_seconds < TIMELINE_MIN_VISIBLE_SECONDS) visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS;
    if (visible_seconds > TIMELINE_MAX_VISIBLE_SECONDS) visible_seconds = TIMELINE_MAX_VISIBLE_SECONDS;

    float horiz_t = 0.0f;
    if (TIMELINE_MAX_VISIBLE_SECONDS > TIMELINE_MIN_VISIBLE_SECONDS) {
        horiz_t = (visible_seconds - TIMELINE_MIN_VISIBLE_SECONDS) /
                  (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
    }
    if (horiz_t < 0.0f) horiz_t = 0.0f;
    if (horiz_t > 1.0f) horiz_t = 1.0f;

    float vert_t = 0.0f;
    if (TIMELINE_MAX_VERTICAL_SCALE > TIMELINE_MIN_VERTICAL_SCALE) {
        vert_t = (state->timeline_vertical_scale - TIMELINE_MIN_VERTICAL_SCALE) /
                 (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
    }
    if (vert_t < 0.0f) vert_t = 0.0f;
    if (vert_t > 1.0f) vert_t = 1.0f;

    int handle_w = 12;
    ui->horiz_handle_rect.w = handle_w;
    ui->horiz_handle_rect.h = ui->horiz_track_rect.h + 8;
    ui->horiz_handle_rect.y = ui->horiz_track_rect.y - 4;
    ui->horiz_handle_rect.x = ui->horiz_track_rect.x + (int)(horiz_t * ui->horiz_track_rect.w) - handle_w / 2;

    ui->vert_handle_rect.w = handle_w;
    ui->vert_handle_rect.h = ui->vert_track_rect.h + 8;
    ui->vert_handle_rect.y = ui->vert_track_rect.y - 4;
    ui->vert_handle_rect.x = ui->vert_track_rect.x + (int)(vert_t * ui->vert_track_rect.w) - handle_w / 2;

    const Engine* engine = state->engine;
    const EngineRuntimeConfig* cfg = engine_get_config(engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    uint64_t total_frames = compute_total_frames(state);
    if (total_frames == 0 && sample_rate > 0) {
        total_frames = (uint64_t)(visible_seconds * (float)sample_rate);
    }
    if (total_frames < 1) {
        total_frames = 1;
    }
    uint64_t transport_frame = engine_get_presentation_frame(engine);
    if (transport_frame > total_frames) {
        transport_frame = total_frames;
    }
    float seek_t = (float)transport_frame / (float)total_frames;
    if (seek_t < 0.0f) seek_t = 0.0f;
    if (seek_t > 1.0f) seek_t = 1.0f;
    ui->seek_handle_rect.w = handle_w;
    ui->seek_handle_rect.h = ui->seek_track_rect.h + 8;
    ui->seek_handle_rect.y = ui->seek_track_rect.y - 4;
    ui->seek_handle_rect.x = ui->seek_track_rect.x + (int)(seek_t * ui->seek_track_rect.w) - handle_w / 2;

    float total_seconds = (sample_rate > 0) ? (float)total_frames / (float)sample_rate : 0.0f;
    float max_window_start = total_seconds > visible_seconds ? total_seconds - visible_seconds : 0.0f;
    if (max_window_start < 0.0f) max_window_start = 0.0f;
    float window_start = state->timeline_window_start_seconds;
    if (window_start < 0.0f) window_start = 0.0f;
    if (window_start > max_window_start) window_start = max_window_start;
    float window_t = max_window_start > 0.0f ? window_start / max_window_start : 0.0f;
    ui->window_handle_rect.w = handle_w;
    ui->window_handle_rect.h = ui->window_track_rect.h + 8;
    ui->window_handle_rect.y = ui->window_track_rect.y - 4;
    ui->window_handle_rect.x = ui->window_track_rect.x + (int)(window_t * ui->window_track_rect.w) - handle_w / 2;
}

static void draw_time_with_decimal(SDL_Renderer* renderer, const SDL_Rect* rect, const char* text, SDL_Color color, int scale, float dot_ratio, int dot_index) {
    if (!renderer || !rect || !text) {
        return;
    }
    if (dot_index < 1) {
        dot_index = 1;
    }
    const char* dot = text;
    for (int i = 0; i < dot_index; ++i) {
        dot = strchr(dot, '.');
        if (!dot) {
            break;
        }
        if (i < dot_index - 1) {
            dot += 1;
        }
    }
    if (!dot) {
        int tw = ui_measure_text_width(text, scale);
        int th = ui_font_line_height(scale);
        int tx = rect->x + (rect->w - tw) / 2;
        int ty = rect->y + (rect->h - th) / 2;
        ui_draw_text(renderer, tx, ty, text, color, scale);
        return;
    }

    char left[32];
    char right[32];
    size_t left_len = (size_t)(dot - text);
    if (left_len >= sizeof(left)) {
        left_len = sizeof(left) - 1;
    }
    memcpy(left, text, left_len);
    left[left_len] = '\0';
    snprintf(right, sizeof(right), "%s", dot + 1);

    int left_w = ui_measure_text_width(left, scale);
    int dot_w = ui_measure_text_width(".", scale);
    int th = ui_font_line_height(scale);
    int baseline_y = rect->y + (rect->h - th) / 2;

    int dot_x = rect->x + (int)lroundf((float)rect->w * dot_ratio) - dot_w / 2;
    int left_x = dot_x - left_w;
    int right_x = dot_x + dot_w;

    ui_draw_text(renderer, left_x, baseline_y, left, color, scale);
    ui_draw_text(renderer, dot_x, baseline_y, ".", color, scale);
    ui_draw_text(renderer, right_x, baseline_y, right, color, scale);
}

void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, const AppState* state, bool is_playing) {
    DawThemePalette theme = {0};
    if (!renderer || !ui) {
        return;
    }
    resolve_transport_theme(&theme);
    SDL_Color label_zoom = theme.text_muted;

    SDL_Color underline = theme.pane_border;
    int underline_y = ui->panel_rect.y + ui->panel_rect.h - 1;
    SDL_SetRenderDrawColor(renderer, underline.r, underline.g, underline.b, underline.a);
    SDL_RenderDrawLine(renderer, ui->panel_rect.x, underline_y, ui->panel_rect.x + ui->panel_rect.w, underline_y);

    daw_transport_control_draw(renderer, ui, DAW_TRANSPORT_LOAD, "LOAD", false, ui->load_hovered, &theme);
    daw_transport_control_draw(renderer, ui, DAW_TRANSPORT_SAVE, "SAVE", false, ui->save_hovered, &theme);
    bool recording = state && daw_audio_recording_is_active(&state->audio_recording);
    EnginePlaybackSnapshot playback = engine_transport_get_playback_snapshot(state ? state->engine : NULL);
    if (state && state->engine) is_playing = playback.applied_playing;
    const char* play_label = recording ? "REC" : playback.pending && playback.requested_playing ? "PLAY…" : "PLAY";
    const char* stop_label = playback.pending && !playback.requested_playing ? "STOP…" : "STOP";
    daw_transport_control_draw(renderer, ui, DAW_TRANSPORT_PLAY, play_label, is_playing || recording, ui->play_hovered, &theme);
    daw_transport_control_draw(renderer, ui, DAW_TRANSPORT_STOP, stop_label, !is_playing && !recording, ui->stop_hovered, &theme);

    SDL_Color track_border = theme.timeline_border;

    bool grid_active = state ? state->timeline_show_all_grid_lines : false;
    daw_transport_control_draw(renderer, ui, DAW_TRANSPORT_GRID, grid_active ? "GRID:ALL" : "GRID:AUTO", grid_active, ui->grid_hovered, &theme);

    if (state && state->engine) {
        SDL_Color time_bg = theme.timeline_fill;
        daw_control_frame(renderer,&ui->time_label_rect,time_bg,track_border);
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sample_rate = cfg ? cfg->sample_rate : 0;
        uint64_t frame = engine_get_presentation_frame(state->engine);
        double seconds = (sample_rate > 0) ? (double)frame / (double)sample_rate : 0.0;

        char time_text[48];
        SDL_Color time_color = state->timeline_view_in_beats ? theme.text_muted : theme.text_primary;
        if (state->timeline_view_in_beats && state->tempo_map.event_count > 0) {
            double beats = tempo_map_seconds_to_beats(&state->tempo_map, seconds);
            int bar = 1;
            int beat_idx = 1;
            double sub = 0.0;
            time_signature_map_beat_to_bar_beat(&state->time_signature_map,
                                                beats,
                                                &bar,
                                                &beat_idx,
                                                &sub,
                                                NULL,
                                                NULL);
            int sub_ms = (int)llround(sub * 1000.0);
            if (bar < 1) bar = 1;
            if (beat_idx < 1) beat_idx = 1;
            snprintf(time_text, sizeof(time_text), "%03d.%02d.%03d", bar, beat_idx, sub_ms);
        } else {
            int total_ms = (int)llround(seconds * 1000.0);
            int minutes = total_ms / 60000;
            int seconds_part = (total_ms / 1000) % 60;
            int millis = total_ms % 1000;
            snprintf(time_text, sizeof(time_text), "%02d:%02d.%03d", minutes, seconds_part, millis);
        }
        int time_scale = 1;
        float dot_ratio = state->timeline_view_in_beats ? (5.0f / 8.0f) : (4.0f / 7.0f);
        int dot_index = state->timeline_view_in_beats ? 2 : 1;
        draw_time_with_decimal(renderer, &ui->time_label_rect, time_text, time_color, time_scale, dot_ratio, dot_index);

        // BPM field and TS stub
        SDL_Rect bpm_rect = ui->bpm_rect;
        SDL_Color bpm_bg = theme.slider_track;
        SDL_Color bpm_border = theme.control_border;
        SDL_Color bpm_text_color = theme.text_primary;
        if (state->tempo_ui.focus == TEMPO_FOCUS_BPM) {
            bpm_bg = theme.control_hover_fill;
        }
        if (state->tempo_ui.editing && state->tempo_ui.focus == TEMPO_FOCUS_BPM) {
            bpm_bg = theme.control_active_fill;
        }
        daw_control_frame(renderer,&bpm_rect,bpm_bg,bpm_border);
        char bpm_text[32];
        double tempo_bpm = state->tempo.bpm;
        const TempoEvent* active_tempo = tempo_map_event_at_beat(&state->tempo_map,
                                                                 tempo_map_seconds_to_beats(&state->tempo_map, seconds));
        if (active_tempo) {
            tempo_bpm = active_tempo->bpm;
        }
        if (state->tempo_ui.editing && state->tempo_ui.buffer[0]) {
            snprintf(bpm_text, sizeof(bpm_text), "%s", state->tempo_ui.buffer);
        } else {
            snprintf(bpm_text, sizeof(bpm_text), "%.0f", tempo_bpm);
        }
        int bpm_scale = 1;
        int bpm_tw = ui_measure_text_width(bpm_text, bpm_scale);
        int bpm_th = ui_font_line_height(bpm_scale);
        int bpm_tx = bpm_rect.x + (bpm_rect.w - bpm_tw) / 2;
        int bpm_ty = bpm_rect.y + (bpm_rect.h - bpm_th) / 2;
        if (state->tempo_ui.editing && state->tempo_ui.focus == TEMPO_FOCUS_BPM) {
            SDL_Rect view = {bpm_rect.x + 4,bpm_rect.y + 2,bpm_rect.w - 8,bpm_rect.h - 4};
            daw_text_edit_draw(renderer, &state->tempo_ui.text_edit, (char*)state->tempo_ui.buffer,
                sizeof(state->tempo_ui.buffer), state->tempo_ui.cursor, view, bpm_text_color,
                theme.control_active_fill, bpm_scale);
        } else ui_draw_text(renderer, bpm_tx, bpm_ty, bpm_text, bpm_text_color, bpm_scale);

        int ts_num = state->tempo.ts_num;
        int ts_den = state->tempo.ts_den;
        const TimeSignatureEvent* active_signature =
            time_signature_map_event_at_beat(&state->time_signature_map,
                                             tempo_map_seconds_to_beats(&state->tempo_map, seconds));
        if (active_signature) {
            ts_num = active_signature->ts_num;
            ts_den = active_signature->ts_den;
        }

        SDL_Color ts_bg = theme.slider_track;
        SDL_Color ts_focus_bg = theme.control_hover_fill;
        SDL_Color ts_edit_bg = theme.control_active_fill;
        bool focus_ts = state->tempo_ui.focus == TEMPO_FOCUS_TS;
        bool editing_ts = state->tempo_ui.editing && focus_ts;

        SDL_Rect ts_num_rect = ui->ts_num_rect;
        SDL_Rect ts_den_rect = ui->ts_den_rect;
        SDL_Color num_bg = ts_bg;
        SDL_Color den_bg = ts_bg;
        if (focus_ts && state->tempo_ui.ts_part == TEMPO_TS_PART_NUM) {
            num_bg = editing_ts ? ts_edit_bg : ts_focus_bg;
        }
        if (focus_ts && state->tempo_ui.ts_part == TEMPO_TS_PART_DEN) {
            den_bg = editing_ts ? ts_edit_bg : ts_focus_bg;
        }
        daw_control_frame(renderer,&ts_num_rect,num_bg,bpm_border);
        daw_control_frame(renderer,&ts_den_rect,den_bg,bpm_border);

        char ts_num_text[8];
        char ts_den_text[8];
        if (editing_ts && state->tempo_ui.ts_part == TEMPO_TS_PART_NUM && state->tempo_ui.ts_buffer[0]) {
            snprintf(ts_num_text, sizeof(ts_num_text), "%s", state->tempo_ui.ts_buffer);
        } else {
            snprintf(ts_num_text, sizeof(ts_num_text), "%d", ts_num);
        }
        if (editing_ts && state->tempo_ui.ts_part == TEMPO_TS_PART_DEN && state->tempo_ui.ts_buffer[0]) {
            snprintf(ts_den_text, sizeof(ts_den_text), "%s", state->tempo_ui.ts_buffer);
        } else {
            snprintf(ts_den_text, sizeof(ts_den_text), "%d", ts_den);
        }
        int ts_scale = 1;
        int num_tw = ui_measure_text_width(ts_num_text, ts_scale);
        int den_tw = ui_measure_text_width(ts_den_text, ts_scale);
        int ts_th = ui_font_line_height(ts_scale);
        int num_tx = ts_num_rect.x + (ts_num_rect.w - num_tw) / 2;
        int den_tx = ts_den_rect.x + (ts_den_rect.w - den_tw) / 2;
        int ts_ty = ts_num_rect.y + (ts_num_rect.h - ts_th) / 2;
        SDL_Rect signature_rects[2] = {ts_num_rect,ts_den_rect};
        const char* signature_text[] = {ts_num_text,ts_den_text};
        int signature_x[] = {num_tx,den_tx};
        for (int i = 0; i < 2; ++i) {
            if (editing_ts && state->tempo_ui.ts_part == (i ? TEMPO_TS_PART_DEN : TEMPO_TS_PART_NUM)) {
                SDL_Rect rect = signature_rects[i];
                SDL_Rect view = {rect.x + 3,rect.y + 2,rect.w - 6,rect.h - 4};
                daw_text_edit_draw(renderer, &state->tempo_ui.text_edit, (char*)state->tempo_ui.ts_buffer,
                    sizeof(state->tempo_ui.ts_buffer), state->tempo_ui.ts_cursor, view,
                    bpm_text_color, theme.control_active_fill, ts_scale);
            } else ui_draw_text(renderer, signature_x[i], ts_ty, signature_text[i], bpm_text_color, ts_scale);
        }

        daw_transport_control_draw(renderer,ui,DAW_TRANSPORT_BEATS,"B",
            state->timeline_view_in_beats,ui->beat_toggle_hovered,&theme);
    }

    const SDL_Rect tracks[]={ui->seek_track_rect,ui->window_track_rect,ui->horiz_track_rect,ui->vert_track_rect};
    const SDL_Rect handles[]={ui->seek_handle_rect,ui->window_handle_rect,ui->horiz_handle_rect,ui->vert_handle_rect};
    const bool emphasized[]={ui->seek_hovered || ui->adjusting_seek,ui->window_hovered || ui->adjusting_window,ui->horiz_hovered,ui->vert_hovered};
    for(unsigned i=0;i<4;++i) {
        daw_slider_draw(renderer,tracks[i],handles[i].x+handles[i].w/2,theme.control_border,
            emphasized[i] ? theme.slider_handle_hover : theme.slider_handle);
    }

    daw_transport_control_draw(renderer,ui,DAW_TRANSPORT_FIT_WIDTH,"W",false,ui->fit_width_hovered,&theme);
    daw_transport_control_draw(renderer,ui,DAW_TRANSPORT_FIT_HEIGHT,"H",false,ui->fit_height_hovered,&theme);

    {
        int label_h = ui_font_line_height(1.0f);
        int label_gap = 2;
        ui_draw_text(renderer,
                     ui->seek_track_rect.x,
                     ui->seek_track_rect.y - label_h - label_gap,
                     "Playhead",
                     label_zoom,
                     1.0f);
        ui_draw_text(renderer,
                     ui->window_track_rect.x,
                     ui->window_track_rect.y - label_h - label_gap,
                     "Window",
                     label_zoom,
                     1.0f);
        ui_draw_text(renderer,
                     ui->horiz_track_rect.x,
                     ui->horiz_track_rect.y - label_h - label_gap,
                     "Timeline",
                     label_zoom,
                     1.0f);
        ui_draw_text(renderer,
                     ui->vert_track_rect.x,
                     ui->vert_track_rect.y - label_h - label_gap,
                     "Track Size",
                     label_zoom,
                     1.0f);
    }
}

bool transport_ui_click_play(const TransportUI* ui, int mouse_x, int mouse_y) {
    if (!ui) {
        return false;
    }
    SDL_Point p = {mouse_x, mouse_y};
    return SDL_PointInRect(&p, &ui->play_rect);
}

bool transport_ui_click_stop(const TransportUI* ui, int mouse_x, int mouse_y) {
    if (!ui) {
        return false;
    }
    SDL_Point p = {mouse_x, mouse_y};
    return SDL_PointInRect(&p, &ui->stop_rect);
}
