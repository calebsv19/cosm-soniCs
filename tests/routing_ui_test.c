#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "ui/routing_panel.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "input/effects_panel_input.h"
#include "test_assert.h"
#include <SDL2/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("routing_ui_test",(v),(m))
// Sends matching pointer events through the real routing modal owner.
static void click(AppState* state, SDL_Rect rect) {
    SDL_Event event = {.type=SDL_MOUSEBUTTONDOWN};
    event.button.button=SDL_BUTTON_LEFT; event.button.x=rect.x+rect.w/2; event.button.y=rect.y+rect.h/2;
    CHECK(daw_routing_panel_event(state,&event),"press consumed");
    event.type=SDL_MOUSEBUTTONUP; CHECK(daw_routing_panel_event(state,&event),"release consumed");
}
// Replaces a focused percentage through shared native text events.
static void percentage(AppState* state, SDL_Rect rect, const char* value) {
    click(state,rect);
    SDL_Event key={.type=SDL_KEYDOWN}; key.key.keysym.sym=SDLK_a; key.key.keysym.mod=KMOD_CTRL;
    CHECK(daw_routing_panel_event(state,&key),"select text");
    SDL_Event text={.type=SDL_TEXTINPUT}; SDL_strlcpy(text.text.text,value,sizeof(text.text.text));
    CHECK(daw_routing_panel_event(state,&text),"replace percentage");
}
// Verifies guarded native routing creation, staged fields, stale refusal and lifecycle cancellation.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER) && !TTF_Init(),"runtime");
    AppState* state=calloc(1,sizeof(*state)); CHECK(state!=NULL,"state");
    EngineRuntimeConfig config; config_set_defaults(&config); state->engine=engine_create(&config); CHECK(state->engine!=NULL,"engine");
    state->tempo=tempo_state_default(config.sample_rate); undo_manager_init(&state->undo);
    state->selected_track_index=state->active_track_index=0; state->selected_clip_index=-1;
    effects_panel_input_init(state); ui_init_panes(state); state->layout_runtime.mixer_ratio=.55f; ui_layout_panes(state,1440,900);
    CHECK(daw_routing_panel_open(state),"open"); DawRoutingLayout layout; daw_routing_panel_layout(state,&layout); CHECK(layout.editable,"complete form fits");
    int count=state->routing_ui.captured.track_count, history=state->undo.undo_count;
    click(state,layout.buttons[2]); CHECK(state->routing_ui.captured.track_count==count+1 && state->routing_ui.staged_return && state->undo.undo_count==history+1,"create return once");
    uint64_t destination=state->routing_ui.captured.tracks[count].track_id;
    click(state,layout.buttons[0]); CHECK(state->routing_ui.selected==count-1 && !state->routing_ui.staged_return,"previous source");
    click(state,layout.destinations[0]); CHECK(state->routing_ui.destinations[0]==destination,"stage destination");
    percentage(state,layout.gains[0],"50"); CHECK(!strcmp(state->routing_ui.gains[0],"50"),"shared percentage editing");
    EngineRoutingSnapshot actual; CHECK(daw_action_get_routing(state,&actual)==DAW_ACTION_UNCHANGED && !actual.tracks[count-1].sends[0].return_id,"staging does not publish");
    click(state,layout.buttons[6]); CHECK(state->routing_ui.captured.tracks[count-1].sends[0].gain==.5f && state->undo.undo_count==history+2,"apply one complete step");
    history=state->undo.undo_count; state->bounce_active=true; percentage(state,layout.gains[0],"60"); click(state,layout.buttons[6]);
    CHECK(state->undo.undo_count==history && strstr(state->routing_ui.message,"Stop"),"busy refuses without publication"); state->bounce_active=false; click(state,layout.buttons[3]);
    EngineRoutingSnapshot before=state->routing_ui.captured, next=before, after; next.tracks[count-1].sends[0].gain=.25f;
    CHECK(daw_action_set_routing(state,&before,&next,&after)==DAW_ACTION_APPLIED,"external edit");
    history=state->undo.undo_count; percentage(state,layout.gains[0],"75"); click(state,layout.buttons[6]);
    CHECK(state->undo.undo_count==history && strstr(state->routing_ui.message,"Refresh"),"stale input refuses");
    click(state,layout.buttons[3]); CHECK(state->routing_ui.captured.tracks[count-1].sends[0].gain==.25f,"explicit refresh");
    percentage(state,layout.gains[0],"101"); click(state,layout.buttons[6]); CHECK(state->undo.undo_count==history && strstr(state->routing_ui.message,"0 to 100"),"invalid percentage refuses");
    click(state,layout.buttons[3]); click(state,layout.destinations[1]); percentage(state,layout.gains[1],"25"); click(state,layout.buttons[6]);
    CHECK(state->undo.undo_count==history && strstr(state->routing_ui.message,"Invalid routing"),"duplicate destination refuses whole graph");
    click(state,layout.buttons[3]);
    SDL_Event event={.type=SDL_MOUSEBUTTONDOWN}; event.button.button=SDL_BUTTON_LEFT; event.button.x=layout.buttons[2].x+1; event.button.y=layout.buttons[2].y+1;
    daw_routing_panel_event(state,&event); event.type=SDL_MOUSEBUTTONUP; event.button.x=-1; event.button.y=-1; daw_routing_panel_event(state,&event);
    CHECK(state->undo.undo_count==history,"mismatched release cancels");
    Pane* pane=(Pane*)ui_layout_get_pane(state,2); CHECK(pane!=NULL,"effects pane"); SDL_Rect ordinary=pane->rect; pane->rect.h=100;
    daw_routing_panel_layout(state,&layout); CHECK(!layout.editable,"compact form disabled"); click(state,layout.buttons[2]); CHECK(state->undo.undo_count==history,"compact creation refused"); pane->rect=ordinary;
    daw_routing_panel_layout(state,&layout); percentage(state,layout.gains[0],"30"); event=(SDL_Event){.type=SDL_WINDOWEVENT}; event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    daw_routing_panel_event(state,&event); CHECK(!state->routing_ui.open && state->routing_ui.focused==-1 && state->routing_ui.pressed==-1,"focus releases modal and text");
    daw_routing_panel_release(state); undo_manager_free(&state->undo); engine_destroy(state->engine);
    state->engine=engine_create(&config); CHECK(state->engine && engine_remove_track(state->engine,0),"fresh empty project"); undo_manager_init(&state->undo);
    CHECK(daw_routing_panel_open(state) && state->routing_ui.selected==-1,"empty modal"); daw_routing_panel_layout(state,&layout); click(state,layout.buttons[2]);
    CHECK(state->routing_ui.captured.track_count==1 && state->routing_ui.staged_return && state->undo.undo_count==1,"native creation from empty");
    daw_routing_panel_release(state); undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("routing_ui_test: success (creation, shared fields, staging, exact guards, graph refusals, compact and focus lifecycle)"); return 0;
}
