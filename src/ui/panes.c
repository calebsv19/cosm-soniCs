#include "ui/panes.h"

void pane_manager_init(PaneManager* mgr, Pane* panes, int count) {
    if (!mgr) {
        return;
    }
    mgr->panes = panes;
    mgr->count = count;
    mgr->hovered = NULL;
    for (int i = 0; i < count; ++i) {
        panes[i].highlighted = false;
    }
}

void pane_manager_update_hover(PaneManager* mgr, int mouse_x, int mouse_y) {
    if (!mgr || !mgr->panes) {
        return;
    }
    Pane* new_hover = NULL;
    for (int i = 0; i < mgr->count; ++i) {
        Pane* pane = &mgr->panes[i];
        pane->highlighted = false;
        if (!pane->visible) {
            continue;
        }
        if (SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &pane->rect)) {
            new_hover = pane;
        }
    }
    if (new_hover) {
        new_hover->highlighted = true;
    }
    mgr->hovered = new_hover;
}
