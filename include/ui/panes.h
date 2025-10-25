#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct Pane Pane;

typedef struct {
    Pane* panes;
    int count;
    Pane* hovered;
} PaneManager;

struct Pane {
    SDL_Rect rect;
    SDL_Color border_color;
    SDL_Color fill_color;
    const char* title;
    bool drawTitle;
    bool visible;
    bool highlighted;
};

void pane_manager_init(PaneManager* mgr, Pane* panes, int count);
void pane_manager_update_hover(PaneManager* mgr, int mouse_x, int mouse_y);
