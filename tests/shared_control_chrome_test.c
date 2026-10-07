#include "app_state.h"
#include "ui/layout.h"
#include "ui/pane_composition.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "vk_renderer.h"
#include "input/project_modal_input.h"
#include "kit_ui_window_sdl.h"
#include "ui/control_chrome.h"
#include "ui/transport.h"
#include "config.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Checks broad click geometry independently from thin visuals, then routes real zoom drags.
static void chrome_contract(AppState* state) {
    SDL_Rect track={100,50,300,7};DawSliderChrome view=daw_slider_chrome(track,200);
    assert(view.rail.h==2 && view.marker.w==2 && view.marker.h==9 && view.hit.h==16);
    assert(daw_slider_hit(track,200,track.y-3));
    assert(!daw_slider_hit(track,99,track.y) && !daw_slider_hit(track,400,track.y));
    view=daw_slider_chrome(track,-100);assert(view.marker.x==track.x);
    view=daw_slider_chrome(track,1000);assert(view.marker.x+view.marker.w==track.x+track.w);
    ui_layout_panes(state,1600,1000);transport_ui_sync(&state->transport_ui,state);
    SDL_Rect zoom=state->transport_ui.horiz_track_rect;
    SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;
    e.button.x=zoom.x+zoom.w/2;e.button.y=zoom.y-3;
    input_manager_handle_event(&state->input_manager,state,&e);
    assert(state->input_manager.prev_horiz_slider_down);
    assert(state->timeline_visible_seconds>TIMELINE_MIN_VISIBLE_SECONDS);
    e.type=SDL_MOUSEMOTION;e.motion.x=zoom.x+zoom.w+100;e.motion.y=zoom.y;
    input_manager_handle_event(&state->input_manager,state,&e);
    assert(state->timeline_visible_seconds==TIMELINE_MAX_VISIBLE_SECONDS);
    e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;
    input_manager_handle_event(&state->input_manager,state,&e);
    assert(!state->input_manager.prev_horiz_slider_down);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.x=zoom.x+zoom.w/2;e.button.y=zoom.y;
    input_manager_handle_event(&state->input_manager,state,&e);
    assert(state->input_manager.prev_horiz_slider_down);
    e.type=SDL_WINDOWEVENT;e.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
    input_manager_handle_event(&state->input_manager,state,&e);
    assert(!state->input_manager.prev_horiz_slider_down && state->input_manager.window_wait_release);
}
// Qualifies the fixed product composition with an offline engine and actual font metrics.
int main(void) {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state=calloc(1,sizeof(*state)); assert(state);
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine=engine_create(&config); assert(state->engine);
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager); ui_init_panes(state);
    chrome_contract(state);
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("shared_control_chrome_test: success (thin visual geometry, generous routed slider hit, domain limits and lifecycle release)");
}
