#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "audio/media_registry.h"

#define LIBRARY_MAX_ITEMS 256
#define LIBRARY_NAME_MAX 128
#define LIBRARY_PATH_MAX 512
#define LIBRARY_STATUS_MAX 192

struct Engine;

typedef enum {
    LIBRARY_PANEL_MODE_SOURCE = 0,
    LIBRARY_PANEL_MODE_IN_PROJECT = 1
} LibraryPanelMode;

typedef struct {
    char name[LIBRARY_NAME_MAX];
    float duration_seconds;
    char media_id[MEDIA_ID_MAX];
} LibraryItem;

typedef struct {
    char name[LIBRARY_NAME_MAX];
    char path[LIBRARY_PATH_MAX];
    char media_id[MEDIA_ID_MAX];
    int use_count;
} LibraryProjectItem;

typedef struct {
    LibraryItem items[LIBRARY_MAX_ITEMS];
    int count;
    char directory[260];
    int hovered_index;
    int selected_index;
    LibraryProjectItem project_items[LIBRARY_MAX_ITEMS];
    int project_count;
    int hovered_project_index;
    int selected_project_index;
    LibraryPanelMode panel_mode;
    char status_line[LIBRARY_STATUS_MAX];
    bool editing;
    int edit_index;
    char edit_buffer[LIBRARY_NAME_MAX];
    int edit_cursor;
} LibraryBrowser;

void library_browser_init(LibraryBrowser* browser, const char* directory);
void library_browser_set_mode(LibraryBrowser* browser, LibraryPanelMode mode);
LibraryPanelMode library_browser_mode(const LibraryBrowser* browser);
void library_browser_render_header_controls(const LibraryBrowser* browser,
                                            SDL_Renderer* renderer,
                                            const SDL_Rect* header_rect);
bool library_browser_hit_test_mode_button(const LibraryBrowser* browser,
                                          const SDL_Rect* rect,
                                          int x,
                                          int y,
                                          LibraryPanelMode* out_mode);
void library_browser_scan(LibraryBrowser* browser, MediaRegistry* registry);
void library_browser_refresh_project_usage(LibraryBrowser* browser, const struct Engine* engine);
int  library_browser_active_count(const LibraryBrowser* browser);
bool library_browser_select_active_index(LibraryBrowser* browser, int index);
int  library_browser_row_height(void);
void library_browser_render(const LibraryBrowser* browser, SDL_Renderer* renderer, const SDL_Rect* rect);
int  library_browser_hit_test(const LibraryBrowser* browser, const SDL_Rect* rect, int x, int y);
