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
        panes[i].dirty = true;
        panes[i].dirty_reasons = DAW_RENDER_INVALIDATION_LAYOUT;
        panes[i].last_render_frame_id = 0;
    }
}

void pane_manager_update_hover(PaneManager* mgr, int mouse_x, int mouse_y) {
    if (!mgr || !mgr->panes) {
        return;
    }
    Pane* new_hover = NULL;
    for (int i = 0; i < mgr->count; ++i) {
        Pane* pane = &mgr->panes[i];
        bool was_highlighted = pane->highlighted;
        pane->highlighted = false;
        if (!pane->visible) {
            if (was_highlighted) {
                pane_mark_dirty(pane, DAW_RENDER_INVALIDATION_INPUT);
            }
            continue;
        }
        if (SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &pane->rect)) {
            new_hover = pane;
        }
        if (was_highlighted != pane->highlighted) {
            pane_mark_dirty(pane, DAW_RENDER_INVALIDATION_INPUT);
        }
    }
    if (new_hover) {
        if (!new_hover->highlighted) {
            pane_mark_dirty(new_hover, DAW_RENDER_INVALIDATION_INPUT);
        }
        new_hover->highlighted = true;
    }
    mgr->hovered = new_hover;
}

Pane* pane_manager_hit_test(PaneManager* mgr, int mouse_x, int mouse_y) {
    if (!mgr || !mgr->panes) {
        return NULL;
    }
    SDL_Point p = {mouse_x, mouse_y};
    for (int i = mgr->count - 1; i >= 0; --i) {
        Pane* pane = &mgr->panes[i];
        if (!pane->visible) {
            continue;
        }
        if (SDL_PointInRect(&p, &pane->rect)) {
            return pane;
        }
    }
    return NULL;
}

int pane_manager_count_dirty(const PaneManager* mgr) {
    if (!mgr || !mgr->panes || mgr->count <= 0) {
        return 0;
    }
    int dirty_count = 0;
    for (int i = 0; i < mgr->count; ++i) {
        if (mgr->panes[i].dirty) {
            dirty_count++;
        }
    }
    return dirty_count;
}

void pane_mark_dirty(Pane* pane, uint32_t reason_bits) {
    if (!pane) {
        return;
    }
    pane->dirty = true;
    pane->dirty_reasons |= reason_bits;
}

void pane_clear_dirty(Pane* pane) {
    if (!pane) {
        return;
    }
    pane->dirty = false;
    pane->dirty_reasons = DAW_RENDER_INVALIDATION_NONE;
}
