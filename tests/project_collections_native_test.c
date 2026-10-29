#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "ui/font.h"
#include "ui/layout_modal_overlays.h"
#include "ui/project_modal_controls.h"
#include "input/project_modal_input.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Captures the actual native modal painters and requires clean Vulkan validation.
static void frame(VkRenderer* r,AppState* s,const char* prefix,const char* name) {
    char path[4096];snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);assert(vk_renderer_request_capture(r,path)==VK_SUCCESS);
    VkCommandBuffer cmd;VkFramebuffer fb;VkExtent2D extent;assert(vk_renderer_begin_frame(r,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(r,s->window_width,s->window_height);
    if(s->project_load.active)ui_render_project_load_overlay((SDL_Renderer*)r,s);else ui_render_project_prompt_overlay((SDL_Renderer*)r,s);
    assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS);vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Activates a native button through its real modal release owner.
static void click(AppState* s,int index) {
    DawProjectModalLayout l;daw_project_modal_layout(s,&l);SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;
    e.button.x=l.buttons[index].x+5;e.button.y=l.buttons[index].y+5;project_modal_input_handle_event(s,&e);e.type=SDL_MOUSEBUTTONUP;project_modal_input_handle_event(s,&e);
}
// Proves actual scoped browsing, version saves and empty-folder UI without touching user songs.
int main(int argc,char** argv) {
    assert(argc==3);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    SDL_Window* window=SDL_CreateWindow("soniCs collection proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1000,700,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
    VkRenderer renderer={0};VkRendererConfig cfg;vk_renderer_config_set_defaults(&cfg);cfg.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&cfg)==VK_SUCCESS);
    char font[4096],cwd[4096];assert(getcwd(cwd,sizeof(cwd)));snprintf(font,sizeof(font),"%s/include/fonts/Montserrat/Montserrat-Regular.ttf",cwd);
    assert(ui_font_set(font,9));AppState* s=calloc(1,sizeof(*s));assert(s);
    config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);undo_manager_init(&s->undo);
    s->timeline_visible_seconds=10;s->timeline_vertical_scale=1;s->selected_track_index=s->selected_clip_index=-1;s->window_width=1000;s->window_height=700;
    assert(!chdir(argv[2]));assert(!mkdir("config",0700));assert(!mkdir("config/projects",0700));assert(!mkdir("music_projects",0700));
    const char* folders[]={"music_projects/classical_conversations","music_projects/classical_conversations/projects","music_projects/night_pocket","music_projects/night_pocket/projects"};
    for(int i=0;i<4;++i)assert(!mkdir(folders[i],0700));
    assert(project_collections_refresh(s,"music_projects"));assert(project_collections_select(s,1));
    project_modal_input_open_save_prompt(s);strcpy(s->project_prompt.buffer,"Classical_Conversations_v001_ThemeStudy");frame(&renderer,s,argv[1],"save-version");click(s,0);
    assert(strstr(s->project.path,"v001"));project_modal_input_open_save_prompt(s);click(s,0);assert(strstr(s->project.path,"v002"));
    char path[SESSION_PATH_MAX];strcpy(path,s->project.path);assert(project_modal_input_open_load_modal(s) && s->project_load.count==2);
    frame(&renderer,s,argv[1],"classical-list");click(s,3);assert(!s->project_load.count && !strcmp(path,s->project.path));frame(&renderer,s,argv[1],"empty-collection");
    click(s,4);assert(s->project_prompt.folder_mode);frame(&renderer,s,argv[1],"choose-folder");click(s,1);assert(s->project_load.active);
    click(s,2);assert(s->project_load.count==2);s->window_width=600;s->window_height=420;frame(&renderer,s,argv[1],"compact-list");
    ui_font_invalidate_cache((SDL_Renderer*)&renderer);ui_font_shutdown();engine_destroy(s->engine);undo_manager_free(&s->undo);
    tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();
    puts("project_collections_native_test: passed (actual collection/save controls, v001/v002, scoped and empty lists, folder cancel, compact geometry and clean Vulkan frames)");return 0;
}
