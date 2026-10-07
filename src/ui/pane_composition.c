#include "ui/pane_composition.h"
#include "app_state.h"
#include "ui/layout.h"
#include "input/project_modal_input.h"

// Converts integral product geometry into shared host coordinates.
static CorePaneRect bounds(SDL_Rect rect) {
    return (CorePaneRect){rect.x,rect.y,rect.w,rect.h};
}
// Converts solved integral coordinates back without changing logical/drawable scale.
static SDL_Rect pixels(CorePaneRect rect) {
    return (SDL_Rect){(int)rect.x,(int)rect.y,(int)rect.width,(int)rect.height};
}
CoreResult daw_panes_compose(const AppState* state, KitPaneComposition* view) {
    if (!state || !view || state->pane_count < 0 || state->pane_count > 4)
        return (CoreResult){CORE_ERR_INVALID_ARG,"invalid DAW pane count"};
    KitPaneCompositionSpec specs[4]; unsigned count=0;
    for (int i=0;i<state->pane_count;++i) {
        const Pane* pane=&state->panes[i];
        if (!pane->visible) continue;
        specs[count++]=(KitPaneCompositionSpec){.id=(CorePaneId)i+1,
            .bounds=bounds(pane->rect),.header_height=ui_layout_pane_header_height(pane),.enabled=1};
    }
    return kit_pane_composition_build(view,specs,count,
        (CorePaneRect){0,0,state->window_width,state->window_height});
}
SDL_Rect daw_pane_content_rect(const Pane* pane) {
    if (!pane) return (SDL_Rect){0};
    KitPaneCompositionSpec spec={.id=1,.bounds=bounds(pane->rect),
        .header_height=ui_layout_pane_header_height(pane),.enabled=1};
    KitPaneComposition view;
    if (kit_pane_composition_build(&view,&spec,1,spec.bounds).code!=CORE_OK) return (SDL_Rect){0};
    return pixels(view.entries[0].content);
}
bool daw_pane_clip_begin(SDL_Renderer* renderer, const AppState* state, int index,
    KitPaneRegion region, DawPaneClip* saved) {
    if (!renderer || !saved || index<0 || index>=4) return false;
    KitPaneComposition view;
    if (daw_panes_compose(state,&view).code!=CORE_OK) return false;
    const KitPaneCompositionEntry* entry=kit_pane_composition_find(&view,(CorePaneId)index+1);
    if (!entry) return false;
    SDL_Rect clip=pixels(region==KIT_PANE_REGION_HEADER ? entry->visible_header :
        region==KIT_PANE_REGION_CONTENT ? entry->visible_content : entry->visible_shell);
    if (clip.w<=0 || clip.h<=0) return false;
    saved->enabled=SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer,&saved->previous);
    if (saved->enabled && !SDL_IntersectRect(&clip,&saved->previous,&clip)) return false;
    return SDL_RenderSetClipRect(renderer,&clip)==0;
}
void daw_pane_clip_end(SDL_Renderer* renderer, const DawPaneClip* saved) {
    if (renderer && saved) (void)SDL_RenderSetClipRect(renderer,saved->enabled ? &saved->previous : NULL);
}
void daw_pane_resize_cancel(AppState* state) {
    if (!state || !state->layout_runtime.drag.active) return;
    state->layout_runtime.drag.active=false;
    state->layout_runtime.drag.target=UI_RESIZE_NONE;
    state->layout_runtime.divider_wait_release=true;
}
void daw_pane_lifecycle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) return;
    bool invalid=project_modal_input_active(state) ||
        daw_workspace_authoring_host_active(&state->workspace_authoring);
    if (event->type==SDL_WINDOWEVENT) {
        Uint8 kind=event->window.event;
        invalid=invalid || kind==SDL_WINDOWEVENT_FOCUS_LOST || kind==SDL_WINDOWEVENT_HIDDEN ||
            kind==SDL_WINDOWEVENT_MINIMIZED || kind==SDL_WINDOWEVENT_SIZE_CHANGED || kind==SDL_WINDOWEVENT_RESIZED;
    }
    if (invalid) daw_pane_resize_cancel(state);
}
