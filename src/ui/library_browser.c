#include "ui/library_browser.h"

#include "audio/media_clip.h"
#include "audio/media_registry.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define strcasecmp _stricmp
#endif

static const float k_library_text_scale = 1.0f;
static const float k_library_detail_text_scale = 0.9f;
static const int k_library_row_extra_height = 8;
static const int k_library_content_top_padding = 8;
static const int k_library_header_gap = 6;
static const int k_library_button_height = 20;
static const int k_library_button_width = 74;

static void resolve_library_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .text_primary = {200, 200, 210, 255},
            .selection_fill = {110, 140, 190, 200},
            .control_hover_fill = {70, 95, 160, 160},
            .text_muted = {180, 184, 198, 255}
        };
    }
}

static void library_safe_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static const char* library_basename_from_path(const char* path) {
    const char* slash = NULL;
    if (!path) {
        return "";
    }
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char* backslash = strrchr(path, '\\');
        if (!slash || (backslash && backslash > slash)) {
            slash = backslash;
        }
    }
#endif
    return slash ? slash + 1 : path;
}

static bool library_rect_contains(const SDL_Rect* rect, int x, int y) {
    return rect && x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

static SDL_Rect library_source_button_rect(const SDL_Rect* rect) {
    int top = rect->y + (rect->h - k_library_button_height) / 2;
    int right = rect->x + rect->w - 10;
    SDL_Rect out = {
        right - (k_library_button_width * 2 + 6),
        top,
        k_library_button_width,
        k_library_button_height
    };
    return out;
}

static SDL_Rect library_project_button_rect(const SDL_Rect* rect) {
    int top = rect->y + (rect->h - k_library_button_height) / 2;
    int right = rect->x + rect->w - 10;
    SDL_Rect out = {
        right - k_library_button_width,
        top,
        k_library_button_width,
        k_library_button_height
    };
    return out;
}

static int library_content_header_height(void) {
    int line_h = ui_font_line_height(k_library_text_scale);
    int detail_h = ui_font_line_height(k_library_detail_text_scale);
    return line_h + k_library_header_gap + detail_h + k_library_header_gap;
}

static int library_source_row_height(void) {
    int line_h = ui_font_line_height(k_library_text_scale);
    if (line_h < 8) {
        line_h = 8;
    }
    return line_h + k_library_row_extra_height;
}

static int library_project_row_height(void) {
    int top_h = ui_font_line_height(k_library_text_scale);
    int bottom_h = ui_font_line_height(k_library_detail_text_scale);
    if (top_h < 8) {
        top_h = 8;
    }
    if (bottom_h < 7) {
        bottom_h = 7;
    }
    return top_h + bottom_h + 10;
}

static int library_active_row_height(const LibraryBrowser* browser) {
    if (browser && browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        return library_project_row_height();
    }
    return library_source_row_height();
}

void library_browser_init(LibraryBrowser* browser, const char* directory) {
    if (!browser) {
        return;
    }
    browser->count = 0;
    if (directory) {
        strncpy(browser->directory, directory, sizeof(browser->directory) - 1);
        browser->directory[sizeof(browser->directory) - 1] = '\0';
    } else {
        browser->directory[0] = '\0';
    }
    browser->hovered_index = -1;
    browser->selected_index = -1;
    browser->project_count = 0;
    browser->hovered_project_index = -1;
    browser->selected_project_index = -1;
    browser->panel_mode = LIBRARY_PANEL_MODE_SOURCE;
    library_safe_copy(browser->status_line, sizeof(browser->status_line), "No files loaded");
    browser->editing = false;
    browser->edit_index = -1;
    browser->edit_buffer[0] = '\0';
    browser->edit_cursor = 0;
}

void library_browser_set_mode(LibraryBrowser* browser, LibraryPanelMode mode) {
    if (!browser) {
        return;
    }
    if (mode != LIBRARY_PANEL_MODE_SOURCE && mode != LIBRARY_PANEL_MODE_IN_PROJECT) {
        return;
    }
    browser->panel_mode = mode;
    browser->hovered_index = -1;
    browser->hovered_project_index = -1;
}

LibraryPanelMode library_browser_mode(const LibraryBrowser* browser) {
    if (!browser) {
        return LIBRARY_PANEL_MODE_SOURCE;
    }
    return browser->panel_mode;
}

void library_browser_render_header_controls(const LibraryBrowser* browser,
                                            SDL_Renderer* renderer,
                                            const SDL_Rect* header_rect) {
    DawThemePalette theme = {0};
    SDL_Color text_color;
    SDL_Color highlight_color;
    SDL_Color selected_color;
    SDL_Rect source_button;
    SDL_Rect project_button;
    SDL_Rect mode_button;
    if (!browser || !renderer || !header_rect || header_rect->w <= 0 || header_rect->h <= 0) {
        return;
    }
    resolve_library_theme(&theme);
    text_color = theme.text_primary;
    highlight_color = theme.control_hover_fill;
    selected_color = theme.selection_fill;
    source_button = library_source_button_rect(header_rect);
    project_button = library_project_button_rect(header_rect);
    mode_button = browser->panel_mode == LIBRARY_PANEL_MODE_SOURCE ? source_button : project_button;
    SDL_SetRenderDrawColor(renderer, highlight_color.r, highlight_color.g, highlight_color.b, 100);
    SDL_RenderFillRect(renderer, &mode_button);
    SDL_SetRenderDrawColor(renderer, selected_color.r, selected_color.g, selected_color.b, selected_color.a);
    SDL_RenderDrawRect(renderer, &source_button);
    SDL_RenderDrawRect(renderer, &project_button);
    ui_draw_text_clipped(renderer,
                         source_button.x + 8,
                         source_button.y + 3,
                         "SOURCE",
                         text_color,
                         1.0f,
                         source_button.w - 12);
    ui_draw_text_clipped(renderer,
                         project_button.x + 8,
                         project_button.y + 3,
                         "IN PROJECT",
                         text_color,
                         1.0f,
                         project_button.w - 12);
}

bool library_browser_hit_test_mode_button(const LibraryBrowser* browser,
                                          const SDL_Rect* rect,
                                          int x,
                                          int y,
                                          LibraryPanelMode* out_mode) {
    SDL_Rect source_rect;
    SDL_Rect project_rect;
    (void)browser;
    if (!rect || rect->w <= 0 || rect->h <= 0) {
        return false;
    }
    source_rect = library_source_button_rect(rect);
    project_rect = library_project_button_rect(rect);
    if (library_rect_contains(&source_rect, x, y)) {
        if (out_mode) {
            *out_mode = LIBRARY_PANEL_MODE_SOURCE;
        }
        return true;
    }
    if (library_rect_contains(&project_rect, x, y)) {
        if (out_mode) {
            *out_mode = LIBRARY_PANEL_MODE_IN_PROJECT;
        }
        return true;
    }
    return false;
}

static void ensure_directory(const char* path) {
    if (!path || !*path) {
        return;
    }
    char temp[260];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    for (char* p = temp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char old = *p;
            *p = '\0';
#if defined(_WIN32)
            _mkdir(temp);
#else
            mkdir(temp, 0755);
#endif
            *p = old;
        }
    }
#if defined(_WIN32)
    _mkdir(temp);
#else
    mkdir(temp, 0755);
#endif
}

static float library_resolve_duration_seconds(const char* directory, const char* filename) {
    if (!directory || !filename) {
        return 0.0f;
    }
    char full_path[LIBRARY_PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", directory, filename);

    AudioMediaClip clip;
    memset(&clip, 0, sizeof(clip));
    if (!audio_media_clip_load(full_path, 0, &clip)) {
        return 0.0f;
    }
    float seconds = 0.0f;
    if (clip.sample_rate > 0 && clip.frame_count > 0) {
        seconds = (float)clip.frame_count / (float)clip.sample_rate;
    }
    audio_media_clip_free(&clip);
    return seconds;
}

void library_browser_scan(LibraryBrowser* browser, MediaRegistry* registry) {
    if (!browser || browser->directory[0] == '\0') {
        return;
    }

    ensure_directory(browser->directory);

    DIR* dir = opendir(browser->directory);
    if (!dir) {
        browser->count = 0;
        library_safe_copy(browser->status_line, sizeof(browser->status_line), "Input root unavailable");
        return;
    }

    browser->count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const char* dot = strrchr(entry->d_name, '.');
        if (!dot) {
            continue;
        }
        if (strcasecmp(dot, ".wav") != 0 && strcasecmp(dot, ".mp3") != 0) {
            continue;
        }
        if (browser->count >= LIBRARY_MAX_ITEMS) {
            break;
        }
        LibraryItem* item = &browser->items[browser->count];
        strncpy(item->name, entry->d_name, LIBRARY_NAME_MAX - 1);
        item->name[LIBRARY_NAME_MAX - 1] = '\0';
        item->duration_seconds = library_resolve_duration_seconds(browser->directory, entry->d_name);
        item->media_id[0] = '\0';
        if (registry) {
            char full_path[LIBRARY_PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", browser->directory, entry->d_name);
            MediaRegistryEntry reg_entry = {0};
            if (media_registry_ensure_for_path(registry, full_path, entry->d_name, &reg_entry)) {
                strncpy(item->media_id, reg_entry.id, sizeof(item->media_id) - 1);
                item->media_id[sizeof(item->media_id) - 1] = '\0';
                if (reg_entry.duration_seconds <= 0.0f) {
                    reg_entry.duration_seconds = item->duration_seconds;
                    media_registry_update_path(registry, reg_entry.id, reg_entry.path, reg_entry.name);
                }
            }
        }
        browser->count++;
    }
    closedir(dir);

    if (registry && registry->dirty) {
        media_registry_save(registry);
    }

    if (browser->count > 0) {
        snprintf(browser->status_line, sizeof(browser->status_line), "Found %d source files", browser->count);
    } else {
        library_safe_copy(browser->status_line, sizeof(browser->status_line), "No .wav/.mp3 files in input root");
    }

    browser->hovered_index = -1;
    if (browser->selected_index >= browser->count) {
        browser->selected_index = -1;
    }
    if (browser->editing && (browser->edit_index < 0 || browser->edit_index >= browser->count)) {
        browser->editing = false;
        browser->edit_index = -1;
        browser->edit_buffer[0] = '\0';
        browser->edit_cursor = 0;
    }
}

void library_browser_refresh_project_usage(LibraryBrowser* browser, const Engine* engine) {
    if (!browser || !engine) {
        if (browser) {
            browser->project_count = 0;
            if (browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
                library_safe_copy(browser->status_line, sizeof(browser->status_line), "No project loaded");
            }
        }
        return;
    }

    browser->project_count = 0;
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    for (int t = 0; tracks && t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        for (int c = 0; c < track->clip_count; ++c) {
            const EngineClip* clip = &track->clips[c];
            const char* media_path = engine_clip_get_media_path(clip);
            const char* media_id = engine_clip_get_media_id(clip);
            int found = -1;
            if (!media_path || media_path[0] == '\0') {
                continue;
            }
            for (int i = 0; i < browser->project_count; ++i) {
                if (strcmp(browser->project_items[i].path, media_path) == 0) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                browser->project_items[found].use_count++;
                continue;
            }
            if (browser->project_count >= LIBRARY_MAX_ITEMS) {
                continue;
            }
            LibraryProjectItem* item = &browser->project_items[browser->project_count++];
            library_safe_copy(item->path, sizeof(item->path), media_path);
            library_safe_copy(item->name, sizeof(item->name), library_basename_from_path(media_path));
            library_safe_copy(item->media_id, sizeof(item->media_id), media_id ? media_id : "");
            item->use_count = 1;
        }
    }

    if (browser->selected_project_index >= browser->project_count) {
        browser->selected_project_index = -1;
    }
    if (browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        if (browser->project_count > 0) {
            snprintf(browser->status_line, sizeof(browser->status_line), "Project uses %d media file(s)", browser->project_count);
        } else {
            library_safe_copy(browser->status_line, sizeof(browser->status_line), "No media files in current project");
        }
    }
}

int library_browser_active_count(const LibraryBrowser* browser) {
    if (!browser) {
        return 0;
    }
    return browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT ? browser->project_count : browser->count;
}

bool library_browser_select_active_index(LibraryBrowser* browser, int index) {
    int count = 0;
    if (!browser) {
        return false;
    }
    count = library_browser_active_count(browser);
    if (index < 0 || index >= count) {
        return false;
    }
    if (browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        browser->selected_project_index = index;
    } else {
        browser->selected_index = index;
    }
    return true;
}

int library_browser_row_height(void) {
    return library_source_row_height();
}

void library_browser_render(const LibraryBrowser* browser, SDL_Renderer* renderer, const SDL_Rect* rect) {
    DawThemePalette theme = {0};
    SDL_Rect prev_clip = {0};
    SDL_bool had_clip = SDL_FALSE;
    if (!browser || !renderer || !rect) {
        return;
    }
    if (rect->w <= 0 || rect->h <= 0) {
        return;
    }

    resolve_library_theme(&theme);
    SDL_Color text_color = theme.text_primary;
    SDL_Color highlight_color = theme.control_hover_fill;
    SDL_Color selected_color = theme.selection_fill;
    SDL_Color muted_color = theme.text_muted;

    had_clip = ui_clip_is_enabled(renderer);
    ui_get_clip_rect(renderer, &prev_clip);
    ui_set_clip_rect(renderer, rect);

    int line_y = rect->y + k_library_content_top_padding;
    int line_w = rect->w - 20;
    if (line_w < 1) {
        line_w = 1;
    }
    ui_draw_text_clipped(renderer,
                         rect->x + 10,
                         line_y,
                         browser->directory[0] ? browser->directory : "(no input root)",
                         text_color,
                         k_library_text_scale,
                         line_w);
    line_y += ui_font_line_height(k_library_text_scale) + k_library_header_gap;
    ui_draw_text_clipped(renderer,
                         rect->x + 10,
                         line_y,
                         browser->status_line[0] ? browser->status_line : "No status",
                         muted_color,
                         k_library_detail_text_scale,
                         line_w);

    int row_h = library_active_row_height(browser);
    int y = rect->y + k_library_content_top_padding + library_content_header_height();
    int list_count = library_browser_active_count(browser);

    for (int i = 0; i < list_count; ++i) {
        if (y + row_h > rect->y + rect->h) {
            break;
        }
        SDL_Rect row = {rect->x + 8, y, rect->w - 16, row_h};
        if (row.w <= 0 || row.h <= 0) {
            break;
        }

        bool selected = false;
        bool hovered = false;
        if (browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
            selected = (i == browser->selected_project_index);
            hovered = (i == browser->hovered_project_index);
        } else {
            selected = (i == browser->selected_index);
            hovered = (i == browser->hovered_index);
        }

        if (selected) {
            SDL_SetRenderDrawColor(renderer, selected_color.r, selected_color.g, selected_color.b, selected_color.a);
            SDL_RenderFillRect(renderer, &row);
        } else if (hovered) {
            SDL_SetRenderDrawColor(renderer, highlight_color.r, highlight_color.g, highlight_color.b, highlight_color.a);
            SDL_RenderFillRect(renderer, &row);
        }

        if (browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
            const LibraryProjectItem* item = &browser->project_items[i];
            char top_line[200];
            snprintf(top_line, sizeof(top_line), "%s  x%d", item->name, item->use_count);
            int top_y = row.y + 3;
            int detail_y = top_y + ui_font_line_height(k_library_text_scale) + 1;
            int text_w = row.w - 8;
            if (text_w < 1) {
                text_w = 1;
            }
            ui_draw_text_clipped(renderer,
                                 row.x + 4,
                                 top_y,
                                 top_line,
                                 text_color,
                                 k_library_text_scale,
                                 text_w);
            ui_draw_text_clipped(renderer,
                                 row.x + 4,
                                 detail_y,
                                 item->path,
                                 muted_color,
                                 k_library_detail_text_scale,
                                 text_w);
        } else {
            const char* display_name = browser->items[i].name;
            char label[192];
            bool is_editing = browser->editing && browser->edit_index == i;
            if (is_editing) {
                display_name = browser->edit_buffer;
            }
            if (!is_editing && browser->items[i].duration_seconds > 0.0f) {
                snprintf(label, sizeof(label), "%s (%.1fs)", display_name, browser->items[i].duration_seconds);
            } else {
                snprintf(label, sizeof(label), "%s", display_name);
            }
            int text_x = row.x + 4;
            int text_y = row.y + (row_h - ui_font_line_height(k_library_text_scale)) / 2;
            int text_max_w = row.w - 8;
            if (text_max_w < 1) {
                text_max_w = 1;
            }
            ui_draw_text_clipped(renderer, text_x, text_y, label, text_color, k_library_text_scale, text_max_w);

            if (is_editing) {
                char temp[LIBRARY_NAME_MAX];
                int len = (int)strlen(browser->edit_buffer);
                int cursor = browser->edit_cursor;
                if (cursor < 0) cursor = 0;
                if (cursor > len) cursor = len;
                snprintf(temp, sizeof(temp), "%.*s", cursor, browser->edit_buffer);
                int cursor_x = text_x + ui_measure_text_width(temp, k_library_text_scale);
                int cursor_y = text_y;
                int cursor_h = ui_font_line_height(k_library_text_scale);
                SDL_SetRenderDrawColor(renderer, muted_color.r, muted_color.g, muted_color.b, muted_color.a);
                SDL_RenderDrawLine(renderer, cursor_x, cursor_y, cursor_x, cursor_y + cursor_h);
            }
        }
        y += row_h;
    }

    if (list_count == 0) {
        const char* empty_label = browser->panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT
                                      ? "(no project media)"
                                      : "(no wav/mp3 files)";
        int empty_x = rect->x + 12;
        int empty_y = rect->y + k_library_content_top_padding + library_content_header_height();
        int empty_w = rect->w - 20;
        if (empty_w < 1) {
            empty_w = 1;
        }
        ui_draw_text_clipped(renderer,
                             empty_x,
                             empty_y,
                             empty_label,
                             text_color,
                             k_library_text_scale,
                             empty_w);
    }

    ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
}

int library_browser_hit_test(const LibraryBrowser* browser, const SDL_Rect* rect, int x, int y) {
    int row_h = 0;
    int y_start = 0;
    int count = 0;
    if (!browser || !rect) {
        return -1;
    }
    row_h = library_active_row_height(browser);
    if (row_h <= 0) {
        return -1;
    }
    y_start = rect->y + k_library_content_top_padding + library_content_header_height();
    if (x < rect->x || x >= rect->x + rect->w) {
        return -1;
    }
    if (y < y_start || y >= rect->y + rect->h) {
        return -1;
    }
    count = library_browser_active_count(browser);
    int index = (y - y_start) / row_h;
    if (index < 0 || index >= count) {
        return -1;
    }
    return index;
}
