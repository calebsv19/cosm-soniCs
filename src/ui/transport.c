#include "ui/transport.h"

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
}

void transport_ui_update_hover(TransportUI* ui, int mouse_x, int mouse_y) {
    if (!ui) {
        return;
    }
    SDL_Point p = {mouse_x, mouse_y};
    ui->play_hovered = SDL_PointInRect(&p, &ui->play_rect);
    ui->stop_hovered = SDL_PointInRect(&p, &ui->stop_rect);
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

void transport_ui_render(SDL_Renderer* renderer, const TransportUI* ui, bool is_playing) {
    if (!renderer || !ui) {
        return;
    }
    SDL_Color label = {210, 210, 220, 255};
    ui_draw_text(renderer, ui->panel_rect.x + 12, ui->panel_rect.y + 8, "TRANSPORT", label, 2);

    SDL_Color underline = {90, 90, 100, 255};
    int underline_y = ui->panel_rect.y + ui->panel_rect.h - 1;
    SDL_SetRenderDrawColor(renderer, underline.r, underline.g, underline.b, underline.a);
    SDL_RenderDrawLine(renderer, ui->panel_rect.x, underline_y, ui->panel_rect.x + ui->panel_rect.w, underline_y);

    SDL_Color button_base = {60, 60, 70, 255};
    render_button(renderer, &ui->play_rect, ui->play_hovered, is_playing, "PLAY", button_base);
    render_button(renderer, &ui->stop_rect, ui->stop_hovered, !is_playing, "STOP", button_base);
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
