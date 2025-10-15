#include "ui/library_browser.h"

#include "ui/font5x7.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define strcasecmp _stricmp
#endif

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
        size_t len = strlen(entry->d_name);
        if (len < 4) {
            continue;
        }
        const char* ext = entry->d_name + len - 4;
        if (strcasecmp(ext, ".wav") != 0) {
            continue;
        }
        if (browser->count >= LIBRARY_MAX_ITEMS) {
            break;
        }
        strncpy(browser->items[browser->count].name, entry->d_name, LIBRARY_NAME_MAX - 1);
        browser->items[browser->count].name[LIBRARY_NAME_MAX - 1] = '\0';
        browser->count++;
    }
    closedir(dir);

    browser->hovered_index = -1;
    if (browser->selected_index >= browser->count) {
        browser->selected_index = -1;
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
        ui_draw_text(renderer, rect->x + 16, y, browser->items[i].name, text_color, 2);
        y += line_height;
    }
    if (browser->count == 0) {
        ui_draw_text(renderer, rect->x + 16, rect->y + 32, "(no wav files)", text_color, 2);
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
