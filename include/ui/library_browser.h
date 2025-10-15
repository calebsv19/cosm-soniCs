#pragma once

#include <SDL2/SDL.h>

#define LIBRARY_MAX_ITEMS 256
#define LIBRARY_NAME_MAX 128

typedef struct {
    char name[LIBRARY_NAME_MAX];
} LibraryItem;

typedef struct {
    LibraryItem items[LIBRARY_MAX_ITEMS];
    int count;
    char directory[260];
    int hovered_index;
    int selected_index;
} LibraryBrowser;

void library_browser_init(LibraryBrowser* browser, const char* directory);
void library_browser_scan(LibraryBrowser* browser);
void library_browser_render(const LibraryBrowser* browser, SDL_Renderer* renderer, const SDL_Rect* rect, int line_height);
int  library_browser_hit_test(const LibraryBrowser* browser, const SDL_Rect* rect, int x, int y, int line_height);
