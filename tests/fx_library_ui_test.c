#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "ui/fx_library_panel.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "input/effects_panel_input.h"
#include "test_assert.h"
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(v,m) daw_test_expect("fx_library_ui_test",(v),(m))
// Activates only a matching press/release pair through the real modal owner.
static void click(AppState* state,SDL_Rect rect) {
    SDL_Event event={.type=SDL_MOUSEBUTTONDOWN};event.button.button=SDL_BUTTON_LEFT;event.button.x=rect.x+rect.w/2;event.button.y=rect.y+rect.h/2;
    CHECK(daw_fx_library_panel_event(state,&event),"press");event.type=SDL_MOUSEBUTTONUP;CHECK(daw_fx_library_panel_event(state,&event),"release");
}
// Exercises shared text input, exact chain guards, revision updates and cancelled pointer activation.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER) && !TTF_Init(),"runtime");char root[]="/tmp/daw-fx-library-ui-XXXXXX";CHECK(mkdtemp(root)!=NULL && !setenv("DAW_PRESET_LIBRARY",root,1),"private library");
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig config;config_set_defaults(&config);state->engine=engine_create(&config);CHECK(state->engine!=NULL,"engine");undo_manager_init(&state->undo);state->tempo=tempo_state_default(config.sample_rate);state->selected_track_index=state->active_track_index=0;state->selected_clip_index=-1;
    effects_panel_input_init(state);ui_init_panes(state);ui_layout_panes(state,1440,900);DawFxSnapshot before,after;uint64_t track=engine_get_tracks(state->engine)[0].runtime_id;
    CHECK(daw_action_get_fx(state,engine_instance_id(state->engine),track,&before)==DAW_ACTION_UNCHANGED,"chain");EngineFxChainRequest edit={.op=ENGINE_FX_CHAIN_ADD,.type=1};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"gain");before=after;
    edit=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_PARAM,.id=before.chain.items[0].id,.parameter=0,.value=-6};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"gain value");effects_panel_sync_from_engine(state);
    CHECK(daw_fx_library_panel_open(state) && state->fx_library_ui.captured.track_id==track,"open correct track library");DawFxLibraryLayout layout;daw_fx_library_panel_layout(state,&layout);click(state,layout.buttons[0]);CHECK(state->fx_library_ui.saving,"save as modal");
    SDL_Event text={.type=SDL_TEXTINPUT};SDL_strlcpy(text.text.text,"Studio Warmth",sizeof(text.text.text));CHECK(daw_fx_library_panel_event(state,&text),"shared text input");int history=state->undo.undo_count;
    SDL_Event enter={.type=SDL_KEYDOWN};enter.key.keysym.sym=SDLK_RETURN;CHECK(daw_fx_library_panel_event(state,&enter) && !state->fx_library_ui.saving && state->fx_library_ui.has_loaded,"native save");
    CHECK(state->fx_library_ui.library->count==1 && !strcmp(state->fx_library_ui.loaded.name,"Studio Warmth") && state->undo.undo_count==history,"save does not edit project");
    before=after;edit.value=-12;CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"outside change");daw_fx_library_panel_layout(state,&layout);CHECK(layout.count==1,"saved row visible");history=state->undo.undo_count;click(state,layout.rows[0]);CHECK(state->undo.undo_count==history && strstr(state->fx_library_ui.message,"Chain changed"),"stale UI recall refuses");
    click(state,layout.buttons[2]);daw_fx_library_panel_layout(state,&layout);click(state,layout.rows[0]);CHECK(state->undo.undo_count==history+1 && state->fx_library_ui.captured.chain.items[0].params[0]==-6,"refresh then atomic recall");
    FxInstId recalled=state->fx_library_ui.captured.chain.items[0].id;CHECK(recalled!=after.chain.items[0].id,"new independent instance");
    char revision[33];strcpy(revision,state->fx_library_ui.loaded.revision);click(state,layout.buttons[1]);CHECK(state->fx_library_ui.saving && state->fx_library_ui.update,"explicit update modal");CHECK(daw_fx_library_panel_event(state,&enter) && strcmp(revision,state->fx_library_ui.loaded.revision),"revision-bound update");
    daw_fx_library_panel_layout(state,&layout);SDL_Event down={.type=SDL_MOUSEBUTTONDOWN};down.button.button=SDL_BUTTON_LEFT;down.button.x=layout.buttons[0].x+1;down.button.y=layout.buttons[0].y+1;CHECK(daw_fx_library_panel_event(state,&down),"capture save press");
    SDL_Event focus={.type=SDL_WINDOWEVENT};focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;daw_fx_library_panel_event(state,&focus);down.type=SDL_MOUSEBUTTONUP;CHECK(daw_fx_library_panel_event(state,&down) && !state->fx_library_ui.saving,"focus cancellation prevents activation");
    // A clipped control must never activate through its invisible portion outside the pane.
    Pane* mixer=(Pane*)ui_layout_get_pane(state,2);CHECK(mixer!=NULL,"mixer pane");SDL_Rect ordinary=mixer->rect;mixer->rect.h=65;daw_fx_library_panel_layout(state,&layout);
    CHECK(layout.buttons[0].y+layout.buttons[0].h>layout.panel.y+layout.panel.h,"compact fixture clips buttons");
    down.type=SDL_MOUSEBUTTONDOWN;down.button.x=layout.buttons[0].x+1;down.button.y=layout.buttons[0].y+layout.buttons[0].h-1;
    CHECK(daw_fx_library_panel_event(state,&down),"clipped press consumed");down.type=SDL_MOUSEBUTTONUP;
    CHECK(daw_fx_library_panel_event(state,&down) && !state->fx_library_ui.saving,"invisible button cannot activate");
    mixer->rect=ordinary;daw_fx_library_panel_layout(state,&layout);click(state,layout.buttons[3]);CHECK(!state->fx_library_ui.open && !state->fx_library_ui.editing,"close releases text");
    daw_fx_library_panel_release(state);CHECK(!state->fx_library_ui.library,"release cache");char directory[1024];snprintf(directory,sizeof(directory),"%s/FxChains",root);DIR* dir=opendir(directory);CHECK(dir!=NULL,"cleanup");struct dirent* entry;while((entry=readdir(dir))){if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;char path[2048];snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name);CHECK(!unlink(path),"retire private files");}closedir(dir);CHECK(!rmdir(directory) && !rmdir(root),"retire private dirs");unsetenv("DAW_PRESET_LIBRARY");
    undo_manager_free(&state->undo);engine_destroy(state->engine);free(state);ui_font_shutdown();TTF_Quit();SDL_Quit();puts("fx_library_ui_test: success (shared name editing, revision save/update, stale refusal, atomic recall, pointer/focus cancellation and cache teardown)");return 0;
}
