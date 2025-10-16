#include "ui/transport.h"
#include "app_state.h"
#include "ui/timeline_view.h"

#include "ui/font5x7.h"

#include <string.h>

void transport_ui_init(TransportUI* ui) {
    if (!ui) {
        return;
    }
    ui->panel_rect = (SDL_Rect){0, 0, 0, 0};
    ui->play_rect = (SDL_Rect){0, 0, 0, 0};
    ui->stop_rect = (SDL_Rect){0, 0, 0, 0};
    ui->play_hovered = false;
    ui->stop_hovered = false;
    ui->grid_rect = (SDL_Rect){0, 0, 0, 0};
    ui->horiz_track_rect = (SDL_Rect){0,0,0,0};
    ui->horiz_handle_rect = (SDL_Rect){0,0,0,0};
    ui->vert_track_rect = (SDL_Rect){0,0,0,0};
    ui->vert_handle_rect = (SDL_Rect){0,0,0,0};
    ui->grid_hovered = false;
    ui->horiz_hovered = false;
    ui->vert_hovered = false;
    ui->adjusting_horizontal = false;
    ui->adjusting_vertical = false;
}

void transport_ui_layout(TransportUI* ui, const SDL_Rect* container) {
    if (!ui || !container) {
        return;
    }
    ui->panel_rect = *container;

    const int button_width = 64;
    const int button_height = 26;
    const int padding = 12;

    int x = container->x + padding;
    int y = container->y + (container->h - button_height) / 2;

    ui->play_rect = (SDL_Rect){x, y, button_width, button_height};
    ui->stop_rect = (SDL_Rect){x + button_width + padding, y, button_width, button_height};

    const int group_spacing = padding * 2;
    int grid_width = 96;
    int slider_width = 132;
    int slider_height = 8;
    int slider_y = container->y + (container->h - slider_height) / 2 - 8;

    int grid_x = ui->stop_rect.x + ui->stop_rect.w + group_spacing;
    ui->grid_rect = (SDL_Rect){grid_x, container->y + (container->h - 24) / 2, grid_width, 24};

    int slider_x = grid_x + grid_width + group_spacing;

    if (slider_x + slider_width > container->x + container->w - padding) {
        slider_width = container->x + container->w - padding - slider_x;
        if (slider_width < 80) {
            slider_width = 80;
        }
    }

    ui->horiz_track_rect = (SDL_Rect){slider_x, slider_y, slider_width, slider_height};
    ui->horiz_handle_rect = (SDL_Rect){slider_x, slider_y - 4, 12, slider_height + 8};

    int vert_slider_y = slider_y + 20;
    ui->vert_track_rect = (SDL_Rect){slider_x, vert_slider_y, slider_width, slider_height};
    ui->vert_handle_rect = (SDL_Rect){slider_x, vert_slider_y - 4, 12, slider_height + 8};
}

void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y) {
    if (!ui) {
        return;
    }
    SDL_Point p = {mouse_x, mouse_y};
    ui->play_hovered = SDL_PointInRect(&p, &ui->play_rect);
    ui->stop_hovered = SDL_PointInRect(&p, &ui->stop_rect);
    ui->grid_hovered = SDL_PointInRect(&p, &ui->grid_rect);
    ui->horiz_hovered = SDL_PointInRect(&p, &ui->horiz_track_rect);
    ui->vert_hovered = SDL_PointInRect(&p, &ui->vert_track_rect);
}

void transport_ui_sync(TransportUI* ui, const AppState* state) {
    if (!ui || !state) {
        return;
    }
    if (ui->horiz_track_rect.w <= 0 || ui->vert_track_rect.w <= 0) {
        return;
    }

    float horiz_t = 0.0f;
    if (TIMELINE_MAX_VISIBLE_SECONDS > TIMELINE_MIN_VISIBLE_SECONDS) {
        horiz_t = (state->timeline_visible_seconds - TIMELINE_MIN_VISIBLE_SECONDS) /
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
    int text_width = (int)strlen(label) * 6 * scale;
    int text_x = rect->x + (rect->w - text_width) / 2;
    int text_height = 7 * scale;
    int text_y = rect->y + (rect->h - text_height) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, const AppState* state, bool is_playing) {
    if (!renderer || !ui) {
        return;
    }
    SDL_Color label_zoom = {200, 200, 210, 255};
    SDL_Color label = {210, 210, 220, 255};
    ui_draw_text(renderer, ui->panel_rect.x + 12, ui->panel_rect.y + 8, "TRANSPORT", label, 2);

    SDL_Color underline = {90, 90, 100, 255};
    int underline_y = ui->panel_rect.y + ui->panel_rect.h - 1;
    SDL_SetRenderDrawColor(renderer, underline.r, underline.g, underline.b, underline.a);
    SDL_RenderDrawLine(renderer, ui->panel_rect.x, underline_y, ui->panel_rect.x + ui->panel_rect.w, underline_y);

    SDL_Color button_base = {60, 60, 70, 255};
    render_button(renderer, &ui->play_rect, ui->play_hovered, is_playing, "PLAY", button_base);
    render_button(renderer, &ui->stop_rect, ui->stop_hovered, !is_playing, "STOP", button_base);

    SDL_Color track_bg = {36, 36, 44, 255};
    SDL_Color track_border = {90, 90, 110, 255};

    bool grid_active = state ? state->timeline_show_all_grid_lines : false;
    render_button(renderer, &ui->grid_rect, ui->grid_hovered, grid_active, grid_active ? "GRID:ALL" : "GRID:AUTO", button_base);

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
