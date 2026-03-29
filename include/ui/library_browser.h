#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "audio/media_registry.h"

#define LIBRARY_MAX_ITEMS 256
#define LIBRARY_NAME_MAX 128

typedef struct {
    char name[LIBRARY_NAME_MAX];
    float duration_seconds;
    char media_id[MEDIA_ID_MAX];
} LibraryItem;

typedef struct {
    LibraryItem items[LIBRARY_MAX_ITEMS];
    int count;
    char directory[260];
    int hovered_index;
    int selected_index;
    bool editing;
    int edit_index;
    char edit_buffer[LIBRARY_NAME_MAX];
    int edit_cursor;
} LibraryBrowser;

void library_browser_init(LibraryBrowser* browser, const char* directory);
void library_browser_scan(LibraryBrowser* browser, MediaRegistry* registry);
int  library_browser_row_height(void);
void library_browser_render(const LibraryBrowser* browser, SDL_Renderer* renderer, const SDL_Rect* rect);
int  library_browser_hit_test(const LibraryBrowser* browser, const SDL_Rect* rect, int x, int y);
