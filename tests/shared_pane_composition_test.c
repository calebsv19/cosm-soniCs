#include "app_state.h"
#include "ui/layout.h"
#include "ui/pane_composition.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "vk_renderer.h"
#include "input/project_modal_input.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Checks product geometry, stable identity, hidden panes and half-open shared boundaries.
static void composition(AppState* state) {
    const int sizes[][2]={{640,480},{800,600},{1600,1000},{2400,1400}};
    for (unsigned i=0;i<4;++i) {
        ui_layout_panes(state,sizes[i][0],sizes[i][1]);
        KitPaneComposition view; assert(daw_panes_compose(state,&view).code==CORE_OK && view.count==4);
        for (int j=0;j<4;++j) {
            const KitPaneCompositionEntry* entry=kit_pane_composition_find(&view,j+1); assert(entry);
            SDL_Rect content=ui_layout_pane_content_rect(&state->panes[j]);
            assert(entry->content.x==content.x && entry->content.y==content.y &&
                entry->content.width==content.w && entry->content.height==content.h);
            assert(entry->visible_shell.x>=0 && entry->visible_shell.y>=0);
            assert(entry->visible_shell.x+entry->visible_shell.width<=sizes[i][0]);
            assert(entry->visible_shell.y+entry->visible_shell.height<=sizes[i][1]);
        }
        assert(kit_pane_composition_hit(&view,state->panes[1].rect.x,state->panes[1].rect.y+40).id==2);
        state->panes[2].visible=false; assert(daw_panes_compose(state,&view).code==CORE_OK);
        assert(!kit_pane_composition_find(&view,3)); state->panes[2].visible=true;
    }
}
// Starts a genuine divider gesture through sampled application routing.
static void begin(AppState* state) {
    ui_layout_panes(state,1600,1000);
    int x=state->panes[1].rect.x+state->panes[1].rect.w/2, y=state->panes[2].rect.y;
    input_manager_update_layout_pointer(state,0,SDL_BUTTON_LMASK,x,y);
    assert(state->layout_runtime.drag.active && state->layout_runtime.drag.target==UI_RESIZE_TIMELINE_MIXER);
    assert(!daw_workspace_authoring_host_active(&state->workspace_authoring));
}
// Drains the canceled press before permitting a fresh divider gesture.
static void drain(AppState* state) {
    int x=state->panes[1].rect.x+100,y=state->panes[2].rect.y;
    input_manager_update_layout_pointer(state,0,SDL_BUTTON_LMASK,x,y);
    assert(!state->layout_runtime.drag.active);
    input_manager_update_layout_pointer(state,SDL_BUTTON_LMASK,0,x,y);
    assert(!state->layout_runtime.divider_wait_release);
}
// Exercises subtle resizing, focus/size/modal cancellation and the release latch.
static void resizing(AppState* state) {
    begin(state);
    int y=state->panes[2].rect.y,x=state->panes[1].rect.x+100;
    float before=state->layout_runtime.mixer_ratio;
    input_manager_update_layout_pointer(state,SDL_BUTTON_LMASK,SDL_BUTTON_LMASK,x,y-40);
    assert(state->layout_runtime.mixer_ratio>before);
    assert(!daw_workspace_authoring_host_active(&state->workspace_authoring));
    SDL_Event event={.type=SDL_WINDOWEVENT}; event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input_manager_handle_event(&state->input_manager,state,&event);
    assert(!state->layout_runtime.drag.active); drain(state);
    begin(state); ui_layout_panes(state,1200,800); assert(!state->layout_runtime.drag.active); drain(state);
    begin(state); event.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
    input_manager_handle_event(&state->input_manager,state,&event); assert(!state->layout_runtime.drag.active); drain(state);
    begin(state); project_modal_input_open_save_prompt(state); input_manager_update(&state->input_manager,state);
    assert(!state->layout_runtime.drag.active); project_modal_input_close_save_prompt(state); drain(state);
    // Generous hitboxes stay separate from authoring; ordinary edges are not hard to grab.
    ui_layout_update_zones(state); bool found=false;
    for (int i=0;i<state->layout_runtime.zone_count;++i)
        if (state->layout_runtime.zones[i].target==UI_RESIZE_LIBRARY) {
            assert(state->layout_runtime.zones[i].rect.w>=14); found=true;
        }
    assert(found);
}
// Tests the real native clip state and nested intersection without requiring a GPU fixture.
static void clips(AppState* state) {
    VkRenderer native={0}; SDL_Renderer* renderer=(SDL_Renderer*)&native;
    SDL_Rect outer={10,10,600,80};
    assert(ui_set_clip_rect(renderer,&outer)==0 && ui_clip_is_enabled(renderer));
    DawPaneClip saved; assert(daw_pane_clip_begin(renderer,state,0,KIT_PANE_REGION_CONTENT,&saved));
    SDL_Rect current, expected;
    assert(SDL_IntersectRect(&outer,&state->panes[0].rect,&expected));
    ui_get_clip_rect(renderer,&current); assert(!memcmp(&current,&expected,sizeof(current)));
    SDL_Rect whole={0,0,1600,1000}; assert(!ui_set_content_clip_rect(renderer,&whole));
    ui_get_clip_rect(renderer,&current); assert(!memcmp(&current,&expected,sizeof(current)));
    daw_pane_clip_end(renderer,&saved); ui_get_clip_rect(renderer,&current);
    assert(!memcmp(&current,&outer,sizeof(current)));
    state->panes[0].visible=false;
    assert(!daw_pane_clip_begin(renderer,state,0,KIT_PANE_REGION_CONTENT,&saved));
    ui_get_clip_rect(renderer,&current); assert(!memcmp(&current,&outer,sizeof(current)));
    state->panes[0].visible=true;
}
// Qualifies the fixed product composition with an offline engine and actual font metrics.
int main(void) {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state=calloc(1,sizeof(*state)); assert(state);
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine=engine_create(&config); assert(state->engine);
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager); ui_init_panes(state);
    composition(state); resizing(state); clips(state);
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("shared_pane_composition_test: success (product geometry, visibility, subtle resize and owner cancellation)");
}
