#include "ui/transport.h"
#include "app_state.h"
#include "ui/timeline_view.h"

#include "ui/font.h"

#include <string.h>

void transport_ui_init(TransportUI* ui) {
    if (!ui) {
        return;
    }
    ui->panel_rect = (SDL_Rect){0, 0, 0, 0};
    ui->load_rect = (SDL_Rect){0, 0, 0, 0};
    ui->save_rect = (SDL_Rect){0, 0, 0, 0};
    ui->play_rect = (SDL_Rect){0, 0, 0, 0};
    ui->stop_rect = (SDL_Rect){0, 0, 0, 0};
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

    const int button_width = 64;
    const int button_height = 26;
    const int padding = 12;
    const int button_v_gap = 4;
    const int label_width = 96;
    const int seek_height = 8;
    const int handle_width = 12;

    int x = container->x + padding;
    int button_stack_height = button_height * 2 + button_v_gap;
    int buttons_y = container->y + (container->h - button_stack_height) / 2;

    ui->load_rect = (SDL_Rect){x, buttons_y, button_width, button_height};
    ui->play_rect = (SDL_Rect){x, buttons_y + button_height + button_v_gap, button_width, button_height};

    ui->save_rect = (SDL_Rect){x + button_width + padding, buttons_y, button_width, button_height};
    ui->stop_rect = (SDL_Rect){x + button_width + padding, buttons_y + button_height + button_v_gap, button_width, button_height};

    const int group_spacing = padding * 2;
    int grid_width = 88;
    int slider_width = 132;
    int slider_height = 8;
    int slider_y = container->y + (container->h - slider_height) / 2 - 8;
    int slider_row_gap = 20;

    x = ui->stop_rect.x + ui->stop_rect.w + padding;
    int label_y = container->y + (container->h - button_height) / 2;
    ui->time_label_rect = (SDL_Rect){x, label_y, label_width, button_height};
    x += label_width + padding;


    int right_edge = container->x + container->w - padding;
    int grid_x = right_edge - grid_width;
    ui->grid_rect = (SDL_Rect){grid_x, container->y + (container->h - 24) / 2, grid_width, 24};

    int fit_button_width = 20;
    int fit_button_height = 16;
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
    ui->fit_width_rect = (SDL_Rect){button_stack_x, slider_y - 10, fit_button_width, fit_button_height};
    ui->fit_height_rect = (SDL_Rect){button_stack_x, slider_y + 12, fit_button_width, fit_button_height};

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
    ui->seek_hovered = SDL_PointInRect(&p, &ui->seek_track_rect) || SDL_PointInRect(&p, &ui->seek_handle_rect);
    ui->window_hovered = SDL_PointInRect(&p, &ui->window_track_rect) || SDL_PointInRect(&p, &ui->window_handle_rect);
    ui->horiz_hovered = SDL_PointInRect(&p, &ui->horiz_track_rect);
    ui->vert_hovered = SDL_PointInRect(&p, &ui->vert_track_rect);
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
    if (ui->horiz_handle_rect.x < ui->horiz_track_rect.x)
        ui->horiz_handle_rect.x = ui->horiz_track_rect.x;
    if (ui->horiz_handle_rect.x + handle_w > ui->horiz_track_rect.x + ui->horiz_track_rect.w)
        ui->horiz_handle_rect.x = ui->horiz_track_rect.x + ui->horiz_track_rect.w - handle_w;

    ui->vert_handle_rect.w = handle_w;
    ui->vert_handle_rect.h = ui->vert_track_rect.h + 8;
    ui->vert_handle_rect.y = ui->vert_track_rect.y - 4;
    ui->vert_handle_rect.x = ui->vert_track_rect.x + (int)(vert_t * ui->vert_track_rect.w) - handle_w / 2;
    if (ui->vert_handle_rect.x < ui->vert_track_rect.x)
        ui->vert_handle_rect.x = ui->vert_track_rect.x;
    if (ui->vert_handle_rect.x + handle_w > ui->vert_track_rect.x + ui->vert_track_rect.w)
        ui->vert_handle_rect.x = ui->vert_track_rect.x + ui->vert_track_rect.w - handle_w;

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
    uint64_t transport_frame = engine_get_transport_frame(engine);
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
    if (ui->seek_handle_rect.x < ui->seek_track_rect.x)
        ui->seek_handle_rect.x = ui->seek_track_rect.x;
    if (ui->seek_handle_rect.x + handle_w > ui->seek_track_rect.x + ui->seek_track_rect.w)
        ui->seek_handle_rect.x = ui->seek_track_rect.x + ui->seek_track_rect.w - handle_w;

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
    if (ui->window_handle_rect.x < ui->window_track_rect.x)
        ui->window_handle_rect.x = ui->window_track_rect.x;
    if (ui->window_handle_rect.x + handle_w > ui->window_track_rect.x + ui->window_track_rect.w)
        ui->window_handle_rect.x = ui->window_track_rect.x + ui->window_track_rect.w - handle_w;
}

static void render_button(SDL_Renderer* renderer, const SDL_Rect* rect, bool hovered, bool active, const char* label, SDL_Color base_color) {
    SDL_Color fill = base_color;
    if (active) {
        fill.r = (Uint8)((fill.r + 80) > 255 ? 255 : fill.r + 80);
        fill.g = (Uint8)((fill.g + 100) > 255 ? 255 : fill.g + 100);
        fill.b = (Uint8)((fill.b + 140) > 255 ? 255 : fill.b + 140);
    } else if (hovered) {
        fill.r = (Uint8)((fill.r + 30) > 255 ? 255 : fill.r + 30);
        fill.g = (Uint8)((fill.g + 30) > 255 ? 255 : fill.g + 30);
        fill.b = (Uint8)((fill.b + 40) > 255 ? 255 : fill.b + 40);
    }

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Color border = {120, 120, 128, 255};
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Color text = {220, 220, 230, 255};
    const int scale = 2;
    int text_width = ui_measure_text_width(label, scale);
    int text_height = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_width) / 2;
    int text_y = rect->y + (rect->h - text_height) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, const AppState* state, bool is_playing) {
    if (!renderer || !ui) {
        return;
    }
    SDL_Color label_zoom = {200, 200, 210, 255};

    SDL_Color underline = {90, 90, 100, 255};
    int underline_y = ui->panel_rect.y + ui->panel_rect.h - 1;
    SDL_SetRenderDrawColor(renderer, underline.r, underline.g, underline.b, underline.a);
    SDL_RenderDrawLine(renderer, ui->panel_rect.x, underline_y, ui->panel_rect.x + ui->panel_rect.w, underline_y);

    SDL_Color button_base = {60, 60, 70, 255};
    render_button(renderer, &ui->load_rect, ui->load_hovered, false, "LOAD", button_base);
    render_button(renderer, &ui->save_rect, ui->save_hovered, false, "SAVE", button_base);
    render_button(renderer, &ui->play_rect, ui->play_hovered, is_playing, "PLAY", button_base);
    render_button(renderer, &ui->stop_rect, ui->stop_hovered, !is_playing, "STOP", button_base);

    SDL_Color track_bg = {36, 36, 44, 255};
    SDL_Color track_border = {90, 90, 110, 255};

    bool grid_active = state ? state->timeline_show_all_grid_lines : false;
    render_button(renderer, &ui->grid_rect, ui->grid_hovered, grid_active, grid_active ? "GRID:ALL" : "GRID:AUTO", button_base);

    if (state && state->engine) {
        SDL_Color time_bg = {32, 32, 40, 255};
        SDL_SetRenderDrawColor(renderer, time_bg.r, time_bg.g, time_bg.b, time_bg.a);
        SDL_RenderFillRect(renderer, &ui->time_label_rect);
        SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
        SDL_RenderDrawRect(renderer, &ui->time_label_rect);
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        int sample_rate = cfg ? cfg->sample_rate : 0;
        uint64_t frame = engine_get_transport_frame(state->engine);
        double seconds = (sample_rate > 0) ? (double)frame / (double)sample_rate : 0.0;
        int total_ms = (int)llround(seconds * 1000.0);
        int minutes = total_ms / 60000;
        int seconds_part = (total_ms / 1000) % 60;
        int millis = total_ms % 1000;
        char time_text[32];
        snprintf(time_text, sizeof(time_text), "%02d:%02d.%03d", minutes, seconds_part, millis);
        SDL_Color time_color = {225, 225, 235, 255};
        int tw = ui_measure_text_width(time_text, 2);
        int th = ui_font_line_height(2);
        int time_x = ui->time_label_rect.x + (ui->time_label_rect.w - tw) / 2;
        int time_y = ui->time_label_rect.y + (ui->time_label_rect.h - th) / 2;
        if (time_x < ui->time_label_rect.x) time_x = ui->time_label_rect.x;
        ui_draw_text(renderer, time_x, time_y, time_text, time_color, 2);
    }

    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, &ui->seek_track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &ui->seek_track_rect);
    SDL_Color seek_handle_col = {180, 210, 255, 255};
    if (ui->seek_hovered || ui->adjusting_seek) {
        seek_handle_col = (SDL_Color){210, 230, 255, 255};
    }
    SDL_SetRenderDrawColor(renderer, seek_handle_col.r, seek_handle_col.g, seek_handle_col.b, seek_handle_col.a);
    SDL_RenderFillRect(renderer, &ui->seek_handle_rect);
    SDL_RenderDrawRect(renderer, &ui->seek_handle_rect);

    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, &ui->window_track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &ui->window_track_rect);
    SDL_Color window_handle_col = {170, 200, 245, 255};
    if (ui->window_hovered || ui->adjusting_window) {
        window_handle_col = (SDL_Color){200, 220, 255, 255};
    }
    SDL_SetRenderDrawColor(renderer, window_handle_col.r, window_handle_col.g, window_handle_col.b, window_handle_col.a);
    SDL_RenderFillRect(renderer, &ui->window_handle_rect);
    SDL_RenderDrawRect(renderer, &ui->window_handle_rect);

    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, &ui->horiz_track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &ui->horiz_track_rect);

    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, &ui->vert_track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &ui->vert_track_rect);

    if (state) {
        SDL_SetRenderDrawColor(renderer, 180, 210, 255, 255);
        SDL_RenderFillRect(renderer, &ui->horiz_handle_rect);
        SDL_RenderDrawRect(renderer, &ui->horiz_handle_rect);

        SDL_RenderFillRect(renderer, &ui->vert_handle_rect);
        SDL_RenderDrawRect(renderer, &ui->vert_handle_rect);
    }

    SDL_Color fit_base = {36, 36, 44, 255};
    SDL_Color fit_border = {90, 90, 110, 255};
    SDL_Color fit_hover = {72, 92, 120, 255};

    SDL_Rect buttons[2] = {ui->fit_width_rect, ui->fit_height_rect};
    const char* labels[2] = {"W", "H"};
    bool hovers[2] = {ui->fit_width_hovered, ui->fit_height_hovered};
    for (int i = 0; i < 2; ++i) {
        SDL_Color fill = hovers[i] ? fit_hover : fit_base;
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &buttons[i]);
        SDL_SetRenderDrawColor(renderer, fit_border.r, fit_border.g, fit_border.b, fit_border.a);
        SDL_RenderDrawRect(renderer, &buttons[i]);
        int scale = 2;
        int tw = ui_measure_text_width(labels[i], scale);
        int th = ui_font_line_height(scale);
        int tx = buttons[i].x + (buttons[i].w - tw) / 2;
        int ty = buttons[i].y + (buttons[i].h - th) / 2;
        ui_draw_text(renderer, tx, ty, labels[i], (SDL_Color){220, 220, 230, 255}, scale);
    }

    ui_draw_text(renderer, ui->seek_track_rect.x, ui->seek_track_rect.y - 18, "Playhead", label_zoom, 2);
    ui_draw_text(renderer, ui->window_track_rect.x, ui->window_track_rect.y - 18, "Window", label_zoom, 2);
    ui_draw_text(renderer, ui->horiz_track_rect.x, ui->horiz_track_rect.y - 18, "Timeline", label_zoom, 2);
    ui_draw_text(renderer, ui->vert_track_rect.x, ui->vert_track_rect.y - 18, "Track Size", label_zoom, 2);
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
