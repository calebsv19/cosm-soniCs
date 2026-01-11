#include "ui/layout.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/layout_config.h"
#include "ui/library_browser.h"
#include "ui/transport.h"
#include "ui/clip_inspector.h"
#include "ui/effects_panel.h"
#include "session/project_manager.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void render_single_pane(SDL_Renderer* renderer, const Pane* pane) {
    if (!pane->visible) {
        return;
    }
    SDL_Color fill = pane->fill_color;
    SDL_Color border = pane->border_color;
    if (pane->highlighted) {
        int boost = 18;
        if (pane->title &&
            (strcmp(pane->title, "LIBRARY") == 0 || strcmp(pane->title, "CLIP INSPECTOR") == 0)) {
            boost = 6;
            border.r = 90;
            border.g = 120;
            border.b = 170;
        }
        int r = fill.r + boost;
        int g = fill.g + boost;
        int b = fill.b + boost;
        fill.r = (Uint8)(r > 255 ? 255 : r);
        fill.g = (Uint8)(g > 255 ? 255 : g);
        fill.b = (Uint8)(b > 255 ? 255 : b);
        if (boost == 18) {
            border.r = 120;
            border.g = 160;
            border.b = 220;
        }
    }

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &pane->rect);

    if (pane->drawTitle){
	    SDL_Color title_color = {220, 220, 230, 255};
	    ui_draw_text(renderer, pane->rect.x + 12, pane->rect.y + 12, pane->title, title_color, 1);
    }

    if (pane->highlighted) {
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &pane->rect);
    }
}

static void render_layout_grid(SDL_Renderer* renderer, const AppState* state) {
    SDL_Color border_color = {200, 200, 210, 255};
    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);

    int min_x = state->window_width;
    int min_y = state->window_height;
    int max_x = 0;
    int max_y = 0;

    for (int i = 0; i < state->pane_count; ++i) {
        const Pane* pane = &state->panes[i];
        if (!pane->visible) {
            continue;
        }
        if (pane->rect.x < min_x) min_x = pane->rect.x;
        if (pane->rect.y < min_y) min_y = pane->rect.y;
        int pane_max_x = pane->rect.x + pane->rect.w;
        int pane_max_y = pane->rect.y + pane->rect.h;
        if (pane_max_x > max_x) max_x = pane_max_x;
        if (pane_max_y > max_y) max_y = pane_max_y;
    }

    if (max_x <= min_x || max_y <= min_y) {
        return;
    }

    SDL_Rect outer = {min_x, min_y, max_x - min_x, max_y - min_y};
    SDL_RenderDrawRect(renderer, &outer);

    if (state->pane_count >= 1) {
        const Pane* transport = &state->panes[0];
        int y = transport->rect.y + transport->rect.h;
        SDL_RenderDrawLine(renderer, transport->rect.x, y, transport->rect.x + transport->rect.w, y);
    }

    if (state->pane_count >= 4) {
        const Pane* library = &state->panes[3];
        const Pane* timeline = &state->panes[1];
        if (library->visible && timeline->visible) {
            int x = timeline->rect.x;
            SDL_RenderDrawLine(renderer, x, library->rect.y, x, library->rect.y + library->rect.h);
        }
    }

    if (state->pane_count >= 3) {
        const Pane* timeline = &state->panes[1];
        const Pane* mixer = &state->panes[2];
        if (timeline->visible && mixer->visible) {
            int y = mixer->rect.y;
            SDL_RenderDrawLine(renderer, timeline->rect.x, y, timeline->rect.x + timeline->rect.w, y);
        }
    }
}


void ui_init_panes(AppState* state) {
    if (!state) {
        return;
    }
    state->pane_count = 4;
    state->panes[0] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = {26, 26, 34, 255},
        .title = "MENU",
	.drawTitle = false,
        .visible = true,
        .highlighted = false,
    };
    state->panes[1] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = {32, 32, 40, 255},
        .title = "TIMELINE",
	.drawTitle = false,
        .visible = true,
        .highlighted = false,
    };
    state->panes[2] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = {28, 28, 36, 255},
        .title = "CLIP INSPECTOR",
        .drawTitle = false,
	.visible = true,
        .highlighted = false,
    };
    state->panes[3] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = {24, 24, 32, 255},
        .title = "LIBRARY",
        .drawTitle = true,
	.visible = true,
        .highlighted = false,
    };
    transport_ui_init(&state->transport_ui);

    UILayoutRuntime* runtime = &state->layout_runtime;
    const UILayoutConfig* cfg = ui_layout_config_get();
    runtime->transport_ratio = cfg->transport_height_ratio;
    runtime->library_ratio = cfg->library_width_ratio;
    runtime->mixer_ratio = cfg->mixer_height_ratio;
    runtime->zone_count = 0;
    runtime->drag.active = false;
    runtime->drag.target = UI_RESIZE_NONE;
}

void ui_layout_panes(AppState* state, int width, int height) {
    if (!state) {
        return;
    }
    const UILayoutConfig* cfg = ui_layout_config_get();
    UILayoutRuntime* runtime = &state->layout_runtime;

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    state->window_width = width;
    state->window_height = height;

    Pane* transport = &state->panes[0];
    Pane* timeline = &state->panes[1];
    Pane* mixer = &state->panes[2];
    Pane* library = &state->panes[3];

    float transport_ratio = runtime->transport_ratio > 0.0f ? runtime->transport_ratio : cfg->transport_height_ratio;
    float library_ratio = runtime->library_ratio > 0.0f ? runtime->library_ratio : cfg->library_width_ratio;
    float mixer_ratio = runtime->mixer_ratio > 0.0f ? runtime->mixer_ratio : cfg->mixer_height_ratio;

    int transport_height = (int)((float)height * transport_ratio + 0.5f);
    if (transport_height < cfg->min_transport_height) {
        transport_height = cfg->min_transport_height;
    }
    int min_content_height = cfg->min_timeline_height + cfg->min_mixer_height;
    if (transport_height > height - min_content_height) {
        transport_height = height - min_content_height;
        if (transport_height < 0) {
            transport_height = 0;
        }
    }

    int content_height = height - transport_height;
    if (content_height < min_content_height) {
        content_height = min_content_height;
        if (content_height > height) {
            content_height = height;
        }
        transport_height = height - content_height;
        if (transport_height < 0) {
            transport_height = 0;
        }
    }

    int library_width = (int)((float)width * library_ratio + 0.5f);
    if (library_width < cfg->min_library_width) {
        library_width = cfg->min_library_width;
    }
    if (library_width > width - cfg->min_library_width) {
        library_width = width - cfg->min_library_width;
    }
    if (library_width < 0) {
        library_width = 0;
    }

    int content_width = width - library_width;
    if (content_width < 0) {
        content_width = 0;
    }

    int mixer_height = (int)((float)content_height * mixer_ratio + 0.5f);
    if (mixer_height < cfg->min_mixer_height) {
        mixer_height = cfg->min_mixer_height;
    }
    if (mixer_height > content_height - cfg->min_timeline_height) {
        mixer_height = content_height - cfg->min_timeline_height;
        if (mixer_height < 0) {
            mixer_height = 0;
        }
    }

    int timeline_height = content_height - mixer_height;
    if (timeline_height < cfg->min_timeline_height) {
        timeline_height = cfg->min_timeline_height;
        mixer_height = content_height - timeline_height;
        if (mixer_height < 0) {
            mixer_height = 0;
        }
    }

    transport->rect = (SDL_Rect){0, 0, width, transport_height};
    library->rect = (SDL_Rect){0, transport_height, library_width, content_height};
    timeline->rect = (SDL_Rect){library_width, transport_height, content_width, timeline_height};
    mixer->rect = (SDL_Rect){library_width, transport_height + timeline_height, content_width, mixer_height};

    transport_ui_layout(&state->transport_ui, &transport->rect);

    int content_height_actual = height - transport_height;
    if (content_height_actual < 0) {
        content_height_actual = 0;
    }

    runtime->transport_ratio = height > 0 ? (float)transport_height / (float)height : 0.0f;
    runtime->library_ratio = width > 0 ? (float)library_width / (float)width : 0.0f;
    runtime->mixer_ratio = content_height_actual > 0 ? (float)mixer_height / (float)content_height_actual : 0.0f;

    ui_layout_update_zones(state);
}

void ui_ensure_layout(AppState* state, SDL_Window* window, SDL_Renderer* renderer) {
    if (!state) {
        return;
    }
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    (void)renderer;
#endif
    int width = 0;
    int height = 0;
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    if (!window) {
        return;
    }
    SDL_GetWindowSize(window, &width, &height);
#else
    if (!renderer) {
        return;
    }
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        SDL_Log("SDL_GetRendererOutputSize failed: %s", SDL_GetError());
        return;
    }
#endif
    if (width != state->window_width || height != state->window_height) {
        ui_layout_panes(state, width, height);
    }
}

void ui_render_panes(SDL_Renderer* renderer, const AppState* state) {
    if (!renderer || !state) {
        return;
    }
    for (int i = 0; i < state->pane_count; ++i) {
        render_single_pane(renderer, &state->panes[i]);
    }
    const Pane* library = ui_layout_get_pane(state, 3);
    if (library) {
        library_browser_render(&state->library, renderer, &library->rect, 20);
    }
    render_layout_grid(renderer, state);
}

void ui_render_overlays(SDL_Renderer* renderer, const AppState* state) {
    if (!renderer || !state) {
        return;
    }
    ClipInspectorLayout inspector_layout;
    clip_inspector_compute_layout(state, &inspector_layout);
    if (state->inspector.visible) {
        clip_inspector_render(renderer, state, &inspector_layout);
    } else {
        EffectsPanelLayout effects_layout;
        effects_panel_compute_layout(state, &effects_layout);
        effects_panel_render(renderer, state, &effects_layout);
    }

    // Status log display removed per request.

    if (state->project_prompt.active) {
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
        SDL_SetRenderDrawColor(renderer, 22, 22, 30, 220);
        SDL_RenderFillRect(renderer, &modal);
        SDL_SetRenderDrawColor(renderer, 120, 140, 180, 255);
        SDL_RenderDrawRect(renderer, &modal);

        const char* title = "Save Project As";
        SDL_Color text_col = {230, 230, 240, 255};
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
        SDL_SetRenderDrawColor(renderer, 32, 36, 48, 255);
        SDL_RenderFillRect(renderer, &input_box);
        SDL_SetRenderDrawColor(renderer, 140, 150, 170, 255);
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
        SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
        SDL_RenderFillRect(renderer, &caret);
    }

    if (state->project_load.active) {
        int width = state->window_width > 0 ? state->window_width : 800;
        int height = state->window_height > 0 ? state->window_height : 600;
        SDL_Rect modal = {
            (width - 720) / 2,
            (height - 420) / 2,
            720,
            420
        };
        SDL_SetRenderDrawColor(renderer, 22, 22, 30, 240);
        SDL_RenderFillRect(renderer, &modal);
        SDL_SetRenderDrawColor(renderer, 120, 140, 180, 255);
        SDL_RenderDrawRect(renderer, &modal);

        SDL_Color text_col = {230, 230, 240, 255};
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

        SDL_SetRenderDrawColor(renderer, 30, 34, 44, 255);
        SDL_RenderFillRect(renderer, &list_rect);
        SDL_SetRenderDrawColor(renderer, 90, 110, 140, 255);
        SDL_RenderDrawRect(renderer, &list_rect);

        SDL_SetRenderDrawColor(renderer, 30, 34, 44, 255);
        SDL_RenderFillRect(renderer, &info_rect);
        SDL_SetRenderDrawColor(renderer, 90, 110, 140, 255);
        SDL_RenderDrawRect(renderer, &info_rect);

        // List entries
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
            SDL_Color row_col = selected ? (SDL_Color){60, 90, 140, 255} : (SDL_Color){40, 44, 54, 255};
            SDL_Rect row = {list_rect.x, y, list_rect.w, item_h};
            SDL_SetRenderDrawColor(renderer, row_col.r, row_col.g, row_col.b, row_col.a);
            SDL_RenderFillRect(renderer, &row);
            SDL_SetRenderDrawColor(renderer, 20, 24, 30, 255);
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
            SDL_SetRenderDrawColor(renderer, 120, 140, 180, 220);
            SDL_RenderFillRect(renderer, &bar);
        }

        // Info panel for selection
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

        // Buttons
        SDL_SetRenderDrawColor(renderer, 60, 80, 110, 255);
        SDL_RenderFillRect(renderer, &load_button);
        SDL_SetRenderDrawColor(renderer, 90, 110, 140, 255);
        SDL_RenderDrawRect(renderer, &load_button);
        ui_draw_text(renderer, load_button.x + 12, load_button.y + 10, "Load", text_col, 1);

        SDL_SetRenderDrawColor(renderer, 60, 60, 70, 255);
        SDL_RenderFillRect(renderer, &cancel_button);
        SDL_SetRenderDrawColor(renderer, 90, 110, 140, 255);
        SDL_RenderDrawRect(renderer, &cancel_button);
        ui_draw_text(renderer, cancel_button.x + 8, cancel_button.y + 10, "Cancel", text_col, 1);
    }
}

void ui_render_controls(SDL_Renderer* renderer, AppState* state) {
    if (!renderer || !state) {
        return;
    }
    bool playing = false;
    if (state->engine) {
        playing = engine_transport_is_playing(state->engine);
    }
    transport_ui_sync(&state->transport_ui, state);
    transport_ui_render(renderer, &state->transport_ui, state, playing);

    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (timeline) {
        ui_set_clip_rect(renderer, &timeline->rect);
        timeline_view_render(renderer, &timeline->rect, state);
        ui_set_clip_rect(renderer, NULL);
    }
}

void ui_layout_update_zones(AppState* state) {
    if (!state) {
        return;
    }
    UILayoutRuntime* runtime = &state->layout_runtime;
    runtime->zone_count = 0;

    const int thickness = 32;
    const int corner_size = 64;

    if (state->pane_count < 4) {
        return;
    }

    const SDL_Rect transport = state->panes[0].rect;
    const SDL_Rect timeline = state->panes[1].rect;
    const SDL_Rect mixer = state->panes[2].rect;
    const SDL_Rect library = state->panes[3].rect;

    int idx = 0;

    // Horizontal zone between transport and content
    if (transport.h > 0) {
        int y = transport.y + transport.h;
        int top = y - thickness / 2;
        if (top < transport.y) {
            top = transport.y;
        }
        if (top + thickness > state->window_height) {
            top = state->window_height - thickness;
        }
        if (top < 0) top = 0;
        runtime->zones[idx++] = (UIResizeZone){
            .rect = {0, top, state->window_width, thickness},
            .target = UI_RESIZE_TRANSPORT
        };
    }

    int library_edge = library.x + library.w;
    int content_top = transport.y + transport.h;
    int content_height = state->window_height - content_top;
    if (content_height < 0) content_height = 0;

    // Vertical zone between library and timeline/mixer
    if (library.w > 0 && content_height > 0) {
        int left = library_edge - thickness / 2;
        if (left < 0) left = 0;
        if (left + thickness > state->window_width) {
            left = state->window_width - thickness;
        }
        runtime->zones[idx++] = (UIResizeZone){
            .rect = {left, content_top, thickness, content_height},
            .target = UI_RESIZE_LIBRARY
        };
    }

    // Horizontal zone between timeline and mixer
    if (mixer.h > 0 && timeline.w > 0) {
        int boundary_y = mixer.y;
        int top = boundary_y - thickness / 2;
        if (top < content_top) top = content_top;
        if (top + thickness > state->window_height) {
            top = state->window_height - thickness;
        }
        runtime->zones[idx++] = (UIResizeZone){
            .rect = {library_edge, top, state->window_width - library_edge, thickness},
            .target = UI_RESIZE_TIMELINE_MIXER
        };
    }

    // Corner at transport/library intersection
    if (library.w > 0 && transport.h > 0) {
        int cx = library_edge - corner_size / 2;
        int cy = content_top - corner_size / 2;
        runtime->zones[idx++] = (UIResizeZone){
            .rect = {cx, cy, corner_size, corner_size},
            .target = UI_RESIZE_CORNER_TOP
        };
    }

    // Corner at library/mixer intersection
    if (library.w > 0 && mixer.h > 0) {
        int cx = library_edge - corner_size / 2;
        int cy = mixer.y - corner_size / 2;
        runtime->zones[idx++] = (UIResizeZone){
            .rect = {cx, cy, corner_size, corner_size},
            .target = UI_RESIZE_CORNER_BOTTOM
        };
    }

    runtime->zone_count = idx;
}

static int clamp_int(int value, int min, int max) {
    if (max < min) {
        max = min;
    }
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

const Pane* ui_layout_get_pane(const AppState* state, int index) {
    if (!state || index < 0 || index >= state->pane_count) {
        return NULL;
    }
    return &state->panes[index];
}

bool ui_layout_handle_pointer(AppState* state, Uint32 prev_buttons, Uint32 curr_buttons, int mouse_x, int mouse_y) {
    if (!state) {
        return false;
    }

    UILayoutRuntime* runtime = &state->layout_runtime;
    const UILayoutConfig* cfg = ui_layout_config_get();

    bool prev_down = (prev_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool curr_down = (curr_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    if (!prev_down && curr_down && !runtime->drag.active) {
        UIResizeTarget target = UI_RESIZE_NONE;
        SDL_Point p = {mouse_x, mouse_y};
        // Prefer corner zones first.
        for (int i = 0; i < runtime->zone_count; ++i) {
            if (runtime->zones[i].target == UI_RESIZE_CORNER_TOP || runtime->zones[i].target == UI_RESIZE_CORNER_BOTTOM) {
                if (SDL_PointInRect(&p, &runtime->zones[i].rect)) {
                    target = runtime->zones[i].target;
                    break;
                }
            }
        }
        if (target == UI_RESIZE_NONE) {
            for (int i = 0; i < runtime->zone_count; ++i) {
                if (runtime->zones[i].target != UI_RESIZE_CORNER_TOP && runtime->zones[i].target != UI_RESIZE_CORNER_BOTTOM) {
                    if (SDL_PointInRect(&p, &runtime->zones[i].rect)) {
                        target = runtime->zones[i].target;
                        break;
                    }
                }
            }
        }
        if (target != UI_RESIZE_NONE) {
            runtime->drag.active = true;
            runtime->drag.target = target;
            runtime->drag.start_mouse_x = mouse_x;
            runtime->drag.start_mouse_y = mouse_y;
            runtime->drag.start_transport_ratio = runtime->transport_ratio;
            runtime->drag.start_library_ratio = runtime->library_ratio;
            runtime->drag.start_mixer_ratio = runtime->mixer_ratio;
            runtime->drag.start_transport_px = state->panes[0].rect.h;
            runtime->drag.start_library_px = state->panes[3].rect.w;
            runtime->drag.start_mixer_px = state->panes[2].rect.h;
            runtime->drag.start_window_width = state->window_width;
            runtime->drag.start_window_height = state->window_height;
            runtime->drag.start_content_height = state->window_height - state->panes[0].rect.h;
        }
    }

    bool layout_changed = false;

    if (runtime->drag.active && curr_down) {
        int dx = mouse_x - runtime->drag.start_mouse_x;
        int dy = mouse_y - runtime->drag.start_mouse_y;

        int win_w = runtime->drag.start_window_width > 0 ? runtime->drag.start_window_width : state->window_width;
        int win_h = runtime->drag.start_window_height > 0 ? runtime->drag.start_window_height : state->window_height;
        if (win_w <= 0) win_w = 1;
        if (win_h <= 0) win_h = 1;

        int min_transport_px = cfg->min_transport_height;
        int max_transport_px = win_h - (cfg->min_timeline_height + cfg->min_mixer_height);
        if (max_transport_px < min_transport_px) {
            max_transport_px = min_transport_px;
        }

        int min_library_px = cfg->min_library_width;
        int max_library_px = win_w - cfg->min_library_width;
        if (max_library_px < min_library_px) {
            max_library_px = min_library_px;
        }

        int content_height = runtime->drag.start_content_height;
        if (content_height <= 0) {
            content_height = win_h - runtime->drag.start_transport_px;
            if (content_height < 1) content_height = 1;
        }

        int min_mixer_px = cfg->min_mixer_height;
        int max_mixer_px = content_height - cfg->min_timeline_height;
        if (max_mixer_px < min_mixer_px) {
            max_mixer_px = min_mixer_px;
        }

        float new_transport_ratio = runtime->transport_ratio;
        float new_library_ratio = runtime->library_ratio;
        float new_mixer_ratio = runtime->mixer_ratio;

        switch (runtime->drag.target) {
            case UI_RESIZE_TRANSPORT: {
                int new_px = clamp_int(runtime->drag.start_transport_px + dy, min_transport_px, max_transport_px);
                new_transport_ratio = (float)new_px / (float)win_h;
                break;
            }
            case UI_RESIZE_LIBRARY: {
                int new_px = clamp_int(runtime->drag.start_library_px + dx, min_library_px, max_library_px);
                new_library_ratio = (float)new_px / (float)win_w;
                break;
            }
            case UI_RESIZE_TIMELINE_MIXER: {
                int new_px = clamp_int(runtime->drag.start_mixer_px - dy, min_mixer_px, max_mixer_px);
                new_mixer_ratio = (float)new_px / (float)content_height;
                break;
            }
            case UI_RESIZE_CORNER_TOP: {
                int new_transport_px = clamp_int(runtime->drag.start_transport_px + dy, min_transport_px, max_transport_px);
                int new_library_px = clamp_int(runtime->drag.start_library_px + dx, min_library_px, max_library_px);
                new_transport_ratio = (float)new_transport_px / (float)win_h;
                new_library_ratio = (float)new_library_px / (float)win_w;
                break;
            }
            case UI_RESIZE_CORNER_BOTTOM: {
                int new_library_px = clamp_int(runtime->drag.start_library_px + dx, min_library_px, max_library_px);
                int new_mixer_px = clamp_int(runtime->drag.start_mixer_px - dy, min_mixer_px, max_mixer_px);
                new_library_ratio = (float)new_library_px / (float)win_w;
                new_mixer_ratio = (float)new_mixer_px / (float)content_height;
                break;
            }
            case UI_RESIZE_NONE:
            default:
                break;
        }

        if (new_transport_ratio < 0.0f) new_transport_ratio = 0.0f;
        if (new_transport_ratio > 1.0f) new_transport_ratio = 1.0f;
        if (new_library_ratio < 0.0f) new_library_ratio = 0.0f;
        if (new_library_ratio > 1.0f) new_library_ratio = 1.0f;
        if (new_mixer_ratio < 0.0f) new_mixer_ratio = 0.0f;
        if (new_mixer_ratio > 1.0f) new_mixer_ratio = 1.0f;

        if (fabsf(new_transport_ratio - runtime->transport_ratio) > 0.0001f ||
            fabsf(new_library_ratio - runtime->library_ratio) > 0.0001f ||
            fabsf(new_mixer_ratio - runtime->mixer_ratio) > 0.0001f) {
            runtime->transport_ratio = new_transport_ratio;
            runtime->library_ratio = new_library_ratio;
            runtime->mixer_ratio = new_mixer_ratio;
            ui_layout_panes(state, state->window_width, state->window_height);
            layout_changed = true;
        }
    }

    if (runtime->drag.active && !curr_down) {
        runtime->drag.active = false;
        runtime->drag.target = UI_RESIZE_NONE;
    }

    return layout_changed;
}

void ui_layout_handle_hover(AppState* state, int mouse_x, int mouse_y) {
    if (!state) {
        return;
    }
    const Pane* library = ui_layout_get_pane(state, 3);
    if (!library) {
        state->library.hovered_index = -1;
        return;
    }
    int line_height = 20;
    int hit = library_browser_hit_test(&state->library, &library->rect, mouse_x, mouse_y, line_height);
    state->library.hovered_index = hit;
}
