#include "ui/library_browser.h"

#include "audio/media_clip.h"
#include "ui/font.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define strcasecmp _stricmp
#endif

#include <stdio.h>

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
    browser->editing = false;
    browser->edit_index = -1;
    browser->edit_buffer[0] = '\0';
    browser->edit_cursor = 0;
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
    char full_path[512];
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

void library_browser_scan(LibraryBrowser* browser) {
    if (!browser || browser->directory[0] == '\0') {
        return;
    }

    ensure_directory(browser->directory);

    DIR* dir = opendir(browser->directory);
    if (!dir) {
        browser->count = 0;
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
        strncpy(browser->items[browser->count].name, entry->d_name, LIBRARY_NAME_MAX - 1);
        browser->items[browser->count].name[LIBRARY_NAME_MAX - 1] = '\0';
        browser->items[browser->count].duration_seconds = library_resolve_duration_seconds(browser->directory, entry->d_name);
        browser->count++;
    }
    closedir(dir);

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

void library_browser_render(const LibraryBrowser* browser, SDL_Renderer* renderer, const SDL_Rect* rect, int line_height) {
    if (!browser || !renderer || !rect) {
        return;
    }
    SDL_Color text_color = {200, 200, 210, 255};
    SDL_Color highlight_color = {70, 95, 160, 160};
    SDL_Color selected_color = {110, 140, 190, 200};
    int y = rect->y + 32;
    for (int i = 0; i < browser->count; ++i) {
        if (y + line_height > rect->y + rect->h) {
            break;
        }
        if (i == browser->selected_index) {
            SDL_Rect row = {rect->x + 8, y - 4, rect->w - 16, line_height + 4};
            SDL_SetRenderDrawColor(renderer, selected_color.r, selected_color.g, selected_color.b, selected_color.a);
            SDL_RenderFillRect(renderer, &row);
        } else if (i == browser->hovered_index) {
            SDL_Rect row = {rect->x + 8, y - 4, rect->w - 16, line_height + 4};
            SDL_SetRenderDrawColor(renderer, highlight_color.r, highlight_color.g, highlight_color.b, highlight_color.a);
            SDL_RenderFillRect(renderer, &row);
        }
        const char* display_name = browser->items[i].name;
        char label[192];
        bool is_editing = browser->editing && browser->edit_index == i;
        if (is_editing) {
            display_name = browser->edit_buffer;
        }
        if (browser->items[i].duration_seconds > 0.0f) {
            snprintf(label, sizeof(label), "%s (%.1fs)", display_name, browser->items[i].duration_seconds);
        } else {
            snprintf(label, sizeof(label), "%s", display_name);
        }
        ui_draw_text(renderer, rect->x + 16, y, label, text_color, 1.5f);

        if (is_editing) {
            int char_w = 6 * 2;
            int cursor_x = rect->x + 16 + browser->edit_cursor * char_w;
            int cursor_y = y;
            SDL_SetRenderDrawColor(renderer, 240, 240, 250, 255);
            SDL_RenderDrawLine(renderer, cursor_x, cursor_y, cursor_x, cursor_y + 14);
        }
        y += line_height;
    }
    if (browser->count == 0) {
        ui_draw_text(renderer, rect->x + 16, rect->y + 32, "(no wav files)", text_color, 1.5f);
    }
}

int library_browser_hit_test(const LibraryBrowser* browser, const SDL_Rect* rect, int x, int y, int line_height) {
    if (!browser || !rect) {
        return -1;
    }
    int y_start = rect->y + 32;
    if (x < rect->x || x > rect->x + rect->w) {
        return -1;
    }
    if (y < y_start) {
        return -1;
    }
    int index = (y - y_start) / line_height;
    if (index < 0 || index >= browser->count) {
        return -1;
    }
    return index;
}
