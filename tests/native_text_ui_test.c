#include "app_state.h"
#include "ui/text_edit.h"
#include "ui/font.h"
#include "ui/layout_modal_overlays.h"
#include "ui/project_modal_controls.h"
#include "input/project_modal_input.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Captures the actual product overlay and verifies that native text restores enclosing clips.
static void capture(VkRenderer* renderer, AppState* state, const char* path, bool load) {
    assert(vk_renderer_request_capture(renderer,path) == VK_SUCCESS);
    VkCommandBuffer commands; VkFramebuffer framebuffer; VkExtent2D extent;
    assert(vk_renderer_begin_frame(renderer,&commands,&framebuffer,&extent) == VK_SUCCESS);
    vk_renderer_set_logical_size(renderer,800,600);
    if (load) ui_render_project_load_overlay((SDL_Renderer*)renderer,state);
    else ui_render_project_prompt_overlay((SDL_Renderer*)renderer,state);
    SDL_Rect enclosing = {50,30,75,20}, view = {48,28,150,26};
    assert(SDL_RenderSetClipRect((SDL_Renderer*)renderer,&enclosing) == 0);
    daw_text_edit_draw((SDL_Renderer*)renderer,&state->project_prompt.text_edit,
        state->project_prompt.buffer,sizeof(state->project_prompt.buffer),
        state->project_prompt.cursor,view,(SDL_Color){255,255,255,255},(SDL_Color){100,150,240,255},2);
    SDL_Rect restored; SDL_RenderGetClipRect((SDL_Renderer*)renderer,&restored);
    assert(SDL_RenderIsClipEnabled((SDL_Renderer*)renderer) && !memcmp(&enclosing,&restored,sizeof(restored)));
    SDL_RenderSetClipRect((SDL_Renderer*)renderer,NULL);
    assert(vk_renderer_end_frame(renderer,commands) == VK_SUCCESS);
    vk_renderer_wait_idle(renderer);
    assert(renderer->draw_state.draw_call_count > 10);
    const VkRuntimeCapabilityReport* report = vk_runtime_get_capability_report(&renderer->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}

// Runs a finite native render proof with offline engine state and no project save/load effects.
int main(int argc, char** argv) {
    assert(argc == 3);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0 && TTF_Init() == 0);
    SDL_Window* window = SDL_CreateWindow("Sonics text/modal qualification",SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,800,600,SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);
    assert(window);
    VkRenderer renderer = {0}; VkRendererConfig config;
    vk_renderer_config_set_defaults(&config); config.enable_validation = SDL_TRUE;
    assert(vk_renderer_init(&renderer,window,&config) == VK_SUCCESS);
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state = calloc(1,sizeof(*state)); assert(state);
    state->window_width=800; state->window_height=600;
    EngineRuntimeConfig cfg; config_set_defaults(&cfg); state->engine=engine_create(&cfg); assert(state->engine);
    project_modal_input_open_save_prompt(state);
    strcpy(state->project_prompt.buffer,"Rehearsal café — bounded text stays inside this field");
    state->project_prompt.cursor=9;
    daw_text_edit_begin(&state->project_prompt.text_edit,state->project_prompt.buffer,
        sizeof(state->project_prompt.buffer),&state->project_prompt.cursor,KIT_UI_TEXT_SINGLE_LINE);
    assert(kit_ui_text_position(&state->project_prompt.text_edit,9,0).code == CORE_OK);
    assert(kit_ui_text_compose(&state->project_prompt.text_edit,"été",0,3).code == CORE_OK);
    capture(&renderer,state,argv[1],false);
    project_modal_input_close_save_prompt(state);
    state->project_load.active=true; state->project_load.count=1; state->project_load.selected_index=0;
    strcpy(state->project_load.entries[0].name,"Rehearsal café");
    strcpy(state->project_load.entries[0].path,"/isolated-proof/rehearsal.json");
    int action; SDL_Event tab={.type=SDL_KEYDOWN}; tab.key.keysym.sym=SDLK_TAB;
    assert(daw_project_modal_controls_event(state,&tab,&action));
    capture(&renderer,state,argv[2],true);
    ui_font_invalidate_cache((SDL_Renderer*)&renderer); ui_font_shutdown();
    engine_destroy(state->engine); free(state); vk_renderer_shutdown(&renderer);
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    puts("native_text_ui_test: success (real overlays, staged composition, selection, modal focus, native enclosing clip and validation-clean frames)");
    return 0;
}
