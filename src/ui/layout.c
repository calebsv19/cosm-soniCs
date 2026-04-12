#include "ui/layout.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/layout_config.h"
#include "ui/layout_modal_overlays.h"
#include "ui/library_browser.h"
#include "ui/transport.h"
#include "ui/clip_inspector.h"
#include "ui/effects_panel.h"
#include "ui/shared_theme_font_adapter.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    UI_PANE_TITLE_PAD_X = 12,
    UI_PANE_TITLE_PAD_TOP = 12,
    UI_PANE_TITLE_PAD_BOTTOM = 8
};

int ui_layout_pane_header_height(const Pane* pane) {
    int line_h;
    if (!pane || !pane->drawTitle || !pane->title || !pane->title[0]) {
        return 0;
    }
    line_h = ui_font_line_height(1.0f);
    if (line_h < 0) {
        line_h = 0;
    }
    return UI_PANE_TITLE_PAD_TOP + line_h + UI_PANE_TITLE_PAD_BOTTOM;
}

SDL_Rect ui_layout_pane_content_rect(const Pane* pane) {
    SDL_Rect content = {0, 0, 0, 0};
    int header_h;
    if (!pane) {
        return content;
    }
    content = pane->rect;
    header_h = ui_layout_pane_header_height(pane);
    if (header_h > content.h) {
        header_h = content.h;
    }
    content.y += header_h;
    content.h -= header_h;
    return content;
}

static bool layout_rect_has_positive_size(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static bool layout_rect_contains_rect(const SDL_Rect* outer, const SDL_Rect* inner) {
    if (!outer || !inner) {
        return false;
    }
    return inner->x >= outer->x &&
           inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w &&
           inner->y + inner->h <= outer->y + outer->h;
}

static bool layout_rects_overlap_strict(const SDL_Rect* a, const SDL_Rect* b) {
    if (!layout_rect_has_positive_size(a) || !layout_rect_has_positive_size(b)) {
        return false;
    }
    return a->x < b->x + b->w &&
           a->x + a->w > b->x &&
           a->y < b->y + b->h &&
           a->y + a->h > b->y;
}

bool ui_layout_debug_validate(const AppState* state) {
    if (!state) {
        return false;
    }

    bool ok = true;
    SDL_Rect window_rect = {0, 0, state->window_width, state->window_height};
    const Pane* transport_pane = ui_layout_get_pane(state, 0);
    const Pane* library_pane = ui_layout_get_pane(state, 3);

    for (int i = 0; i < state->pane_count; ++i) {
        const Pane* pane = &state->panes[i];
        if (!pane->visible) {
            continue;
        }
        if (!layout_rect_has_positive_size(&pane->rect)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: pane %d has invalid size", i);
            ok = false;
            continue;
        }
        if (!layout_rect_contains_rect(&window_rect, &pane->rect)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: pane %d outside window bounds", i);
            ok = false;
        }
    }

    if (transport_pane) {
        const TransportUI* ui = &state->transport_ui;
        const SDL_Rect* control_rects[] = {
            &ui->load_rect,
            &ui->save_rect,
            &ui->play_rect,
            &ui->stop_rect,
            &ui->grid_rect,
            &ui->time_label_rect,
            &ui->bpm_rect,
            &ui->ts_rect,
            &ui->seek_track_rect,
            &ui->window_track_rect,
            &ui->horiz_track_rect,
            &ui->vert_track_rect,
            &ui->fit_width_rect,
            &ui->fit_height_rect
        };
        for (size_t i = 0; i < sizeof(control_rects) / sizeof(control_rects[0]); ++i) {
            const SDL_Rect* rect = control_rects[i];
            if (!layout_rect_has_positive_size(rect)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: transport control %zu invalid", i);
                ok = false;
                continue;
            }
            if (!layout_rect_contains_rect(&transport_pane->rect, rect)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: transport control %zu outside pane", i);
                ok = false;
            }
        }
        if (layout_rects_overlap_strict(&ui->fit_width_rect, &ui->fit_height_rect)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: transport fit buttons overlap");
            ok = false;
        }
    }

    if (library_pane) {
        SDL_Rect content_rect = ui_layout_pane_content_rect(library_pane);
        if (!layout_rect_contains_rect(&library_pane->rect, &content_rect)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_debug_validate: library content outside pane");
            ok = false;
        }
    }

    return ok;
}

static void render_single_pane(SDL_Renderer* renderer, const Pane* pane) {
    DawThemePalette theme_palette = {0};
    const bool use_shared_theme = daw_shared_theme_resolve_palette(&theme_palette);
    if (!pane->visible) {
        return;
    }
    SDL_Color fill = pane->fill_color;
    SDL_Color border = use_shared_theme ? theme_palette.pane_border : pane->border_color;
    if (pane->highlighted) {
        if (use_shared_theme) {
            border = theme_palette.pane_highlight_border;
        } else {
            if (pane->title &&
                (strcmp(pane->title, "LIBRARY") == 0 || strcmp(pane->title, "CLIP INSPECTOR") == 0)) {
                border.r = 90;
                border.g = 120;
                border.b = 170;
            } else {
                border.r = 120;
                border.g = 160;
                border.b = 220;
            }
        }
    }

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &pane->rect);

    if (pane->drawTitle && pane->title && pane->title[0]) {
        SDL_Color title_color = daw_shared_theme_title_color();
        SDL_Rect content_rect = ui_layout_pane_content_rect(pane);
        ui_draw_text(renderer,
                     pane->rect.x + UI_PANE_TITLE_PAD_X,
                     pane->rect.y + UI_PANE_TITLE_PAD_TOP,
                     pane->title,
                     title_color,
                     1.0f);
        if (content_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderDrawLine(renderer,
                               pane->rect.x,
                               content_rect.y,
                               pane->rect.x + pane->rect.w,
                               content_rect.y);
        }
    }

    if (pane->highlighted) {
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &pane->rect);
    }
}

static void render_layout_grid(SDL_Renderer* renderer, const AppState* state) {
    DawThemePalette theme_palette = {0};
    SDL_Color border_color = daw_shared_theme_resolve_palette(&theme_palette)
                                 ? theme_palette.pane_border
                                 : (SDL_Color){200, 200, 210, 255};
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

static void render_content_separators(SDL_Renderer* renderer, const AppState* state) {
    if (!renderer || !state || state->pane_count < 3) {
        return;
    }

    const Pane* timeline = &state->panes[1];
    const Pane* mixer = &state->panes[2];
    if (!timeline->visible || !mixer->visible) {
        return;
    }

    DawThemePalette theme_palette = {0};
    SDL_Color border_color = daw_shared_theme_resolve_palette(&theme_palette)
                                 ? theme_palette.pane_border
                                 : (SDL_Color){200, 200, 210, 255};
    int y = mixer->rect.y;
    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderDrawLine(renderer, timeline->rect.x, y, timeline->rect.x + timeline->rect.w, y);
}


void ui_init_panes(AppState* state) {
    if (!state) {
        return;
    }
    state->pane_count = 4;
    state->panes[0] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = (SDL_Color){26, 26, 34, 255},
        .title = "MENU",
		.drawTitle = false,
        .visible = true,
        .highlighted = false,
    };
    state->panes[1] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = (SDL_Color){32, 32, 40, 255},
        .title = "TIMELINE",
		.drawTitle = false,
        .visible = true,
        .highlighted = false,
    };
    state->panes[2] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = (SDL_Color){28, 28, 36, 255},
        .title = "CLIP INSPECTOR",
        .drawTitle = false,
		.visible = true,
        .highlighted = false,
    };
    state->panes[3] = (Pane){
        .rect = {0, 0, 0, 0},
        .border_color = {200, 200, 210, 255},
        .fill_color = (SDL_Color){24, 24, 32, 255},
        .title = "LIBRARY",
        .drawTitle = true,
		.visible = true,
        .highlighted = false,
    };
    ui_apply_shared_theme(state);
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

void ui_apply_shared_theme(AppState* state) {
    DawThemePalette theme_palette = {0};
    const bool use_shared_theme = state && daw_shared_theme_resolve_palette(&theme_palette);
    if (!state || state->pane_count < 4) {
        return;
    }
    state->panes[0].fill_color = use_shared_theme ? theme_palette.menu_fill : (SDL_Color){26, 26, 34, 255};
    state->panes[1].fill_color = use_shared_theme ? theme_palette.timeline_fill : (SDL_Color){32, 32, 40, 255};
    state->panes[2].fill_color = use_shared_theme ? theme_palette.inspector_fill : (SDL_Color){28, 28, 36, 255};
    state->panes[3].fill_color = use_shared_theme ? theme_palette.library_fill : (SDL_Color){24, 24, 32, 255};
    state->panes[0].border_color = use_shared_theme ? theme_palette.pane_border : (SDL_Color){200, 200, 210, 255};
    state->panes[1].border_color = use_shared_theme ? theme_palette.pane_border : (SDL_Color){200, 200, 210, 255};
    state->panes[2].border_color = use_shared_theme ? theme_palette.pane_border : (SDL_Color){200, 200, 210, 255};
    state->panes[3].border_color = use_shared_theme ? theme_palette.pane_border : (SDL_Color){200, 200, 210, 255};
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
#if !defined(NDEBUG)
    if (!ui_layout_debug_validate(state)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui_layout_panes: layout debug validation failed");
    }
#endif
}

bool ui_ensure_layout(AppState* state, SDL_Window* window, SDL_Renderer* renderer) {
    if (!state) {
        return false;
    }
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    (void)renderer;
#endif
    int width = 0;
    int height = 0;
#ifdef VK_RENDERER_ENABLE_SDL_COMPAT
    if (!window) {
        return false;
    }
    SDL_GetWindowSize(window, &width, &height);
#else
    if (!renderer) {
        return false;
    }
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        SDL_Log("SDL_GetRendererOutputSize failed: %s", SDL_GetError());
        return false;
    }
#endif
    if (width != state->window_width || height != state->window_height) {
        ui_layout_panes(state, width, height);
        return true;
    }
    return false;
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
        int header_h = ui_layout_pane_header_height(library);
        if (header_h > 0) {
            SDL_Rect header_rect = library->rect;
            if (header_h < header_rect.h) {
                header_rect.h = header_h;
            }
            library_browser_render_header_controls(&state->library, renderer, &header_rect);
        }
        SDL_Rect content_rect = ui_layout_pane_content_rect(library);
        library_browser_render(&state->library, renderer, &content_rect);
    }
    render_layout_grid(renderer, state);
}

void ui_render_overlays(SDL_Renderer* renderer, AppState* state) {
    if (!renderer || !state) {
        return;
    }
    ui_set_clip_rect(renderer, NULL);
    ClipInspectorLayout inspector_layout;
    clip_inspector_compute_layout(state, &inspector_layout);
    if (state->inspector.visible) {
        const Pane* mixer = ui_layout_get_pane(state, 2);
        SDL_Rect prev_clip;
        SDL_bool had_clip = ui_clip_is_enabled(renderer);
        if (mixer) {
            ui_get_clip_rect(renderer, &prev_clip);
            ui_set_clip_rect(renderer, &mixer->rect);
        }
        clip_inspector_render(renderer, state, &inspector_layout);
        if (mixer) {
            ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
        }
    } else {
        EffectsPanelLayout effects_layout;
        effects_panel_compute_layout(state, &effects_layout);
        const Pane* mixer = ui_layout_get_pane(state, 2);
        SDL_Rect prev_clip;
        SDL_bool had_clip = ui_clip_is_enabled(renderer);
        if (mixer) {
            ui_get_clip_rect(renderer, &prev_clip);
            ui_set_clip_rect(renderer, &mixer->rect);
        }
        effects_panel_render(renderer, state, &effects_layout);
        if (mixer) {
            ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
        }
    }

    // Status log display removed per request.

    ui_render_project_prompt_overlay(renderer, state);
    ui_render_project_load_overlay(renderer, state);

    render_content_separators(renderer, state);
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
    SDL_Rect content_rect = ui_layout_pane_content_rect(library);
    int hit = library_browser_hit_test(&state->library, &content_rect, mouse_x, mouse_y);
    if (state->library.panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        state->library.hovered_project_index = hit;
        state->library.hovered_index = -1;
    } else {
        state->library.hovered_index = hit;
        state->library.hovered_project_index = -1;
    }
}
