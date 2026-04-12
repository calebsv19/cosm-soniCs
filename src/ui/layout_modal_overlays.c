#include "ui/layout_modal_overlays.h"

#include "session/project_manager.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void resolve_modal_theme(SDL_Color* modal_fill,
                                SDL_Color* modal_border,
                                SDL_Color* input_fill,
                                SDL_Color* input_border,
                                SDL_Color* text_color,
                                SDL_Color* selection_fill,
                                SDL_Color* button_fill,
                                SDL_Color* button_fill_alt) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (modal_fill) {
            *modal_fill = theme.inspector_fill;
            modal_fill->a = 236;
        }
        if (modal_border) *modal_border = theme.pane_border;
        if (input_fill) *input_fill = theme.timeline_fill;
        if (input_border) *input_border = theme.control_border;
        if (text_color) *text_color = theme.text_primary;
        if (selection_fill) *selection_fill = theme.slider_handle;
        if (button_fill) *button_fill = theme.control_active_fill;
        if (button_fill_alt) *button_fill_alt = theme.control_fill;
        return;
    }
    if (modal_fill) *modal_fill = (SDL_Color){22, 22, 30, 236};
    if (modal_border) *modal_border = (SDL_Color){120, 140, 180, 255};
    if (input_fill) *input_fill = (SDL_Color){32, 36, 48, 255};
    if (input_border) *input_border = (SDL_Color){140, 150, 170, 255};
    if (text_color) *text_color = (SDL_Color){230, 230, 240, 255};
    if (selection_fill) *selection_fill = (SDL_Color){200, 220, 255, 255};
    if (button_fill) *button_fill = (SDL_Color){60, 80, 110, 255};
    if (button_fill_alt) *button_fill_alt = (SDL_Color){60, 60, 70, 255};
}

void ui_render_project_prompt_overlay(SDL_Renderer* renderer, AppState* state) {
    if (!renderer || !state || !state->project_prompt.active) {
        return;
    }

    SDL_Color modal_fill = {0};
    SDL_Color modal_border = {0};
    SDL_Color input_fill = {0};
    SDL_Color input_border = {0};
    SDL_Color text_col = {0};
    SDL_Color caret_fill = {0};
    resolve_modal_theme(&modal_fill, &modal_border, &input_fill, &input_border, &text_col, &caret_fill, NULL, NULL);
    int width = state->window_width > 0 ? state->window_width : 800;
    int height = state->window_height > 0 ? state->window_height : 600;
    int box_w = 480;
    int box_h = 180;
    SDL_Rect modal = {
        (width - box_w) / 2,
        (height - box_h) / 2,
        box_w,
        box_h
    };
    SDL_SetRenderDrawColor(renderer, modal_fill.r, modal_fill.g, modal_fill.b, 220);
    SDL_RenderFillRect(renderer, &modal);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &modal);

    const char* title = "Save Project As";
    int title_w = ui_measure_text_width(title, 2);
    ui_draw_text(renderer, modal.x + (modal.w - title_w) / 2, modal.y + 12, title, text_col, 2);

    const char* hint = "Type a name and press Enter. Esc to cancel.";
    ui_draw_text(renderer, modal.x + 16, modal.y + modal.h - 32, hint, text_col, 1);

    SDL_Rect input_box = {
        modal.x + 16,
        modal.y + 60,
        modal.w - 32,
        44
    };
    SDL_SetRenderDrawColor(renderer, input_fill.r, input_fill.g, input_fill.b, input_fill.a);
    SDL_RenderFillRect(renderer, &input_box);
    SDL_SetRenderDrawColor(renderer, input_border.r, input_border.g, input_border.b, input_border.a);
    SDL_RenderDrawRect(renderer, &input_box);

    int scale = 2;
    int text_x = input_box.x + 8;
    int text_y = input_box.y + (input_box.h - ui_font_line_height(scale)) / 2;
    ui_draw_text(renderer, text_x, text_y, state->project_prompt.buffer, text_col, scale);

    int cursor_len = (int)strlen(state->project_prompt.buffer);
    int cursor_index = state->project_prompt.cursor;
    if (cursor_index < 0) cursor_index = 0;
    if (cursor_index > cursor_len) cursor_index = cursor_len;
    char temp[SESSION_NAME_MAX];
    snprintf(temp, sizeof(temp), "%.*s", cursor_index, state->project_prompt.buffer);
    int caret_x = text_x + ui_measure_text_width(temp, scale);
    if (caret_x < text_x) caret_x = text_x;
    int caret_h = ui_font_line_height(scale);
    SDL_Rect caret = {caret_x, text_y, 2, caret_h};
    SDL_SetRenderDrawColor(renderer, caret_fill.r, caret_fill.g, caret_fill.b, caret_fill.a);
    SDL_RenderFillRect(renderer, &caret);
}

void ui_render_project_load_overlay(SDL_Renderer* renderer, AppState* state) {
    if (!renderer || !state || !state->project_load.active) {
        return;
    }

    SDL_Color modal_fill = {0};
    SDL_Color modal_border = {0};
    SDL_Color input_fill = {0};
    SDL_Color input_border = {0};
    SDL_Color text_col = {0};
    SDL_Color selection_fill = {0};
    SDL_Color button_fill = {0};
    SDL_Color button_fill_alt = {0};
    resolve_modal_theme(&modal_fill,
                        &modal_border,
                        &input_fill,
                        &input_border,
                        &text_col,
                        &selection_fill,
                        &button_fill,
                        &button_fill_alt);
    int width = state->window_width > 0 ? state->window_width : 800;
    int height = state->window_height > 0 ? state->window_height : 600;
    SDL_Rect modal = {
        (width - 720) / 2,
        (height - 420) / 2,
        720,
        420
    };
    SDL_SetRenderDrawColor(renderer, modal_fill.r, modal_fill.g, modal_fill.b, 240);
    SDL_RenderFillRect(renderer, &modal);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &modal);

    const char* title = "Load Project";
    ui_draw_text(renderer, modal.x + 16, modal.y + 12, title, text_col, 2);

    SDL_Rect list_rect = {
        modal.x + 16,
        modal.y + 56,
        modal.w / 2 - 32,
        modal.h - 96
    };
    SDL_Rect info_rect = {
        modal.x + modal.w / 2 + 8,
        modal.y + 56,
        modal.w / 2 - 24,
        modal.h - 126
    };
    SDL_Rect load_button = {
        info_rect.x,
        modal.y + modal.h - 52,
        120,
        36
    };
    SDL_Rect cancel_button = {
        load_button.x + load_button.w + 12,
        load_button.y,
        120,
        36
    };

    SDL_SetRenderDrawColor(renderer, input_fill.r, input_fill.g, input_fill.b, input_fill.a);
    SDL_RenderFillRect(renderer, &list_rect);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &list_rect);

    SDL_SetRenderDrawColor(renderer, input_fill.r, input_fill.g, input_fill.b, input_fill.a);
    SDL_RenderFillRect(renderer, &info_rect);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &info_rect);

    int item_h = 28;
    int max_scroll = state->project_load.count * item_h - list_rect.h;
    if (max_scroll < 0) max_scroll = 0;
    float scroll = state->project_load.scroll_offset;
    if (scroll < 0.0f) scroll = 0.0f;
    if (scroll > (float)max_scroll) scroll = (float)max_scroll;

    SDL_Rect clip_rect = list_rect;
    ui_set_clip_rect(renderer, &clip_rect);
    for (int i = 0; i < state->project_load.count; ++i) {
        int y = list_rect.y + (int)((float)i * item_h - scroll);
        if (y > list_rect.y + list_rect.h) {
            break;
        }
        if (y + item_h < list_rect.y) {
            continue;
        }
        bool selected = (i == state->project_load.selected_index);
        SDL_Color row_col = selected ? selection_fill : input_fill;
        SDL_Rect row = {list_rect.x, y, list_rect.w, item_h};
        SDL_SetRenderDrawColor(renderer, row_col.r, row_col.g, row_col.b, row_col.a);
        SDL_RenderFillRect(renderer, &row);
        SDL_SetRenderDrawColor(renderer, modal_fill.r, modal_fill.g, modal_fill.b, modal_fill.a);
        SDL_RenderDrawRect(renderer, &row);

        const char* name = state->project_load.entries[i].name[0] ? state->project_load.entries[i].name : "project";
        ui_draw_text(renderer, row.x + 8, row.y + 4, name, text_col, 1.2f);
    }
    ui_set_clip_rect(renderer, NULL);

    if (max_scroll > 0) {
        float t = scroll / (float)max_scroll;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int bar_h = (int)((float)list_rect.h * ((float)list_rect.h / ((float)list_rect.h + (float)max_scroll)));
        if (bar_h < 24) bar_h = 24;
        int bar_y = list_rect.y + (int)(t * (list_rect.h - bar_h));
        SDL_Rect bar = {list_rect.x + list_rect.w - 8, bar_y, 6, bar_h};
        SDL_SetRenderDrawColor(renderer, selection_fill.r, selection_fill.g, selection_fill.b, 220);
        SDL_RenderFillRect(renderer, &bar);
    }

    int sel = state->project_load.selected_index;
    if (sel >= 0 && sel < state->project_load.count) {
        const ProjectInfo* info = &state->project_load.entries[sel];
        char line[256];
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 8, info->name, text_col, 2);
        snprintf(line, sizeof(line), "Path: %s", info->path);
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 34, line, text_col, 1.2f);
        if (info->modified_ms > 0) {
            time_t sec = (time_t)(info->modified_ms / 1000LL);
            struct tm* tminfo = localtime(&sec);
            if (tminfo) {
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tminfo);
                snprintf(line, sizeof(line), "Modified: %s", buf);
                ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 58, line, text_col, 1.2f);
            }
        }
        snprintf(line, sizeof(line), "Tracks: %d", info->track_count);
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 82, line, text_col, 1.2f);
        snprintf(line, sizeof(line), "Clips: %d", info->clip_count);
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 106, line, text_col, 1.2f);
        snprintf(line, sizeof(line), "Duration: %.2fs", info->duration_seconds);
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 130, line, text_col, 1.2f);
        snprintf(line, sizeof(line), "File Size: %.1f KB", info->file_size > 0 ? (double)info->file_size / 1024.0 : 0.0);
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 154, line, text_col, 1.2f);
    } else {
        const char* none = "No projects found";
        ui_draw_text(renderer, info_rect.x + 8, info_rect.y + 8, none, text_col, 1.2f);
    }

    SDL_SetRenderDrawColor(renderer, button_fill.r, button_fill.g, button_fill.b, button_fill.a);
    SDL_RenderFillRect(renderer, &load_button);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &load_button);
    ui_draw_text(renderer, load_button.x + 12, load_button.y + 10, "Load", text_col, 1);

    SDL_SetRenderDrawColor(renderer, button_fill_alt.r, button_fill_alt.g, button_fill_alt.b, button_fill_alt.a);
    SDL_RenderFillRect(renderer, &cancel_button);
    SDL_SetRenderDrawColor(renderer, modal_border.r, modal_border.g, modal_border.b, modal_border.a);
    SDL_RenderDrawRect(renderer, &cancel_button);
    ui_draw_text(renderer, cancel_button.x + 8, cancel_button.y + 10, "Cancel", text_col, 1);
}
