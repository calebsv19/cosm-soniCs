#include "app_state.h"
#include "ui/editor_controls.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input.h"
#include "input/timeline_selection.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Captures the actual product painters with shared focus and validation readback.
static void capture(VkRenderer* renderer, AppState* state, const char* prefix, const char* name) {
    char path[4096]; snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);
    assert(vk_renderer_request_capture(renderer,path)==VK_SUCCESS);
    VkCommandBuffer cmd; VkFramebuffer fb; VkExtent2D extent;
    assert(vk_renderer_begin_frame(renderer,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(renderer,1600,1000);
    ui_render_panes((SDL_Renderer*)renderer,state);
    ui_render_controls((SDL_Renderer*)renderer,state);
    ui_render_overlays((SDL_Renderer*)renderer,state);
    assert(vk_renderer_end_frame(renderer,cmd)==VK_SUCCESS); vk_renderer_wait_idle(renderer);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&renderer->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
    assert(renderer->draw_state.draw_call_count>20);
}
// Sets focus through application input without applying a release action.
static void focus(AppState* state, unsigned domain, uint64_t key) {
    daw_editor_controls_sync(state); KitUiSurface* s=&state->editor_controls.surface;
    assert(s->collection_result.code==CORE_OK);
    for(unsigned i=0;i<s->count;++i) if(s->keys[i].domain==domain && s->keys[i].value==key) {
        KitRenderRect r=s->controls[i].bounds; SDL_Event down={.type=SDL_MOUSEBUTTONDOWN};
        down.button.button=SDL_BUTTON_LEFT; down.button.x=r.x+r.width/2; down.button.y=r.y+r.height/2;
        input_manager_handle_event(&state->input_manager,state,&down);
        SDL_Event up=down; up.type=SDL_MOUSEBUTTONUP; up.button.x=-100; up.button.y=-100;
        input_manager_handle_event(&state->input_manager,state,&up); return;
    }
    assert(!"native control missing");
}
// Runs finite native frames without personal projects or audio device activation.
int main(int argc,char** argv) {
    assert(argc==2); assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    SDL_Window* window=SDL_CreateWindow("Sonics editor controls qualification",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI); assert(window);
    VkRenderer renderer={0}; VkRendererConfig config; vk_renderer_config_set_defaults(&config); config.enable_validation=SDL_TRUE;
    assert(vk_renderer_init(&renderer,window,&config)==VK_SUCCESS);
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state=calloc(1,sizeof(*state)); assert(state); EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    state->engine=engine_create(&cfg); assert(state->engine); state->runtime_cfg=cfg;
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager);
    state->active_track_index=0; state->selected_track_index=-1; state->selected_clip_index=-1;
    ui_init_panes(state); ui_layout_panes(state,1600,1000); effects_panel_input_init(state);
    assert(undo_manager_add_effect(state,-1,1)); effects_panel_sync_from_engine(state);
    focus(state,11,4); capture(&renderer,state,argv[1],"effects-stack");
    state->effects_panel.view_mode=FX_PANEL_VIEW_LIST; focus(state,10,1); capture(&renderer,state,argv[1],"effects-list");
    int clip=-1; assert(engine_add_midi_clip_to_track(state->engine,0,0,96000,&clip));
    assert(engine_clip_midi_add_note(state->engine,0,clip,(EngineMidiNote){0,12000,60,0.8f},NULL));
    timeline_selection_set_single(state,0,clip); state->inspector.visible=true;
    focus(state,12,7); capture(&renderer,state,argv[1],"midi-editor");
    state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    focus(state,13,11); capture(&renderer,state,argv[1],"instrument");
    ui_font_invalidate_cache((SDL_Renderer*)&renderer); ui_font_shutdown(); undo_manager_free(&state->undo);
    engine_destroy(state->engine); free(state); vk_renderer_shutdown(&renderer);
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    puts("native_editor_controls_test: success (actual five-group painters, native focus and validation-clean frames)");
}
