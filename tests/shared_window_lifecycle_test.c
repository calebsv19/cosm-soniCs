#include "app_state.h"
#include "ui/layout.h"
#include "ui/pane_composition.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "vk_renderer.h"
#include "input/project_modal_input.h"
#include "kit_ui_window_sdl.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Geometry mapping retains logical UI coordinates even at high drawable scale.
static void window_contract(AppState* state) {
    KitUiWindowState w={.logical_width=800,.logical_height=600,.drawable_width=1600,.drawable_height=1200};
    int x,y;assert(kit_ui_window_map_point_sdl(&w,800,600,400,300,&x,&y) && x==400 && y==300);
    assert(kit_ui_window_map_point_sdl(&w,800,600,-20,620,&x,&y) && x==-20 && y==620);
    ui_layout_panes(state,1600,1000);
    SDL_Event e={.type=SDL_WINDOWEVENT};
    const Uint8 kinds[]={SDL_WINDOWEVENT_FOCUS_LOST,SDL_WINDOWEVENT_SIZE_CHANGED,SDL_WINDOWEVENT_DISPLAY_CHANGED,SDL_WINDOWEVENT_MINIMIZED,SDL_WINDOWEVENT_HIDDEN,SDL_WINDOWEVENT_RESTORED};
    for(unsigned i=0;i<sizeof(kinds);++i) {
        state->layout_runtime.drag.active=true;state->dragging_library=true;
        state->timeline_controls.adjusting_loop_start=true;
        state->editor_controls.surface.interaction.pointer_owned=1;
        state->transport_ui.controls.interaction.pointer_owned=1;
        e.window.event=kinds[i];input_manager_handle_event(&state->input_manager,state,&e);
        assert(!state->layout_runtime.drag.active && !state->dragging_library);
        assert(!state->timeline_controls.adjusting_loop_start);
        assert(!state->editor_controls.surface.interaction.pointer_owned && !state->transport_ui.controls.interaction.pointer_owned);
        assert(state->input_manager.window_wait_release);
        SDL_Event held={.type=SDL_MOUSEBUTTONDOWN};held.button.button=SDL_BUTTON_LEFT;
        input_manager_handle_event(&state->input_manager,state,&held);
        assert(state->input_manager.window_wait_release);
        held.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&held);
        assert(!state->input_manager.window_wait_release);
    }
}
// Qualifies the fixed product composition with an offline engine and actual font metrics.
int main(void) {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state=calloc(1,sizeof(*state)); assert(state);
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine=engine_create(&config); assert(state->engine);
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager); ui_init_panes(state);
    window_contract(state);
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("shared_window_lifecycle_test: success (logical mapping, native cancellation, held-release latch)");
}
