#include "app_state.h"
#include "engine/groups.h"
#include "ui/track_group_panel.h"
#include "ui/editor_controls.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "input/effects_panel_input.h"
#include "input/timeline/timeline_geometry.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
// Advances actual product painters and optionally captures one native validation-clean frame.
static void frame(VkRenderer* r,AppState* s,const char* prefix,const char* name) {
    if(name){char path[4096];snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);assert(vk_renderer_request_capture(r,path)==VK_SUCCESS);}
    VkCommandBuffer cmd;VkFramebuffer fb;VkExtent2D extent;assert(vk_renderer_begin_frame(r,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(r,s->window_width,s->window_height);ui_render_panes((SDL_Renderer*)r,s);ui_render_controls((SDL_Renderer*)r,s);ui_render_overlays((SDL_Renderer*)r,s);
    assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS);vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Sends matching press/release through the actual application routing owner.
static void click(AppState* s,SDL_Rect rect) {
    SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.x=rect.x+rect.w/2;e.button.y=rect.y+rect.h/2;
    s->mouse_x=e.button.x;s->mouse_y=e.button.y;input_manager_handle_event(&s->input_manager,s,&e);e.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&s->input_manager,s,&e);
}
// Uses the public product manager geometry and presents after every accepted change.
static void button(VkRenderer* r,AppState* s,int index,const char* prefix) {click(s,daw_group_panel_layout(s).buttons[index]);frame(r,s,prefix,NULL);}
// Parents a lane using native row selection, parent cycling and guarded application.
static void parent(VkRenderer* r,AppState* s,int track,int cycles,const char* prefix) {
    click(s,daw_group_panel_layout(s).rows[track]);frame(r,s,prefix,NULL);
    for(int i=0;i<cycles;++i)button(r,s,5,prefix);button(r,s,6,prefix);
}
// Qualifies real native creation, nesting, release guards, folding and group effect selection.
int main(int argc,char** argv) {assert(argc==2);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    SDL_Window* window=SDL_CreateWindow("soniCs nested group proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
    VkRenderer renderer={0};VkRendererConfig config;vk_renderer_config_set_defaults(&config);config.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&config)==VK_SUCCESS);
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);
    s->engine=engine_create(&s->runtime_cfg);assert(s->engine);undo_manager_init(&s->undo);input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s,1600,1000);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    s->timeline_visible_seconds=10;s->timeline_vertical_scale=1;effects_panel_input_init(s);
    while(engine_get_track_count(s->engine)<4)assert(engine_add_track(s->engine)>=0);
    const char* names[]={"Violin","Viola","Cello","Bass"};for(int t=0;t<4;++t){assert(engine_track_set_name(s->engine,t,names[t]));assert(engine_add_midi_clip_to_track(s->engine,t,0,48000,NULL));}
    frame(&renderer,s,argv[1],NULL);daw_editor_controls_sync(s);click(s,s->timeline_controls.groups_rect);assert(s->group_panel.open);frame(&renderer,s,argv[1],NULL);
    for(int i=0;i<5;++i)button(&renderer,s,i,argv[1]);assert(engine_get_track_count(s->engine)==9);
    parent(&renderer,s,7,1,argv[1]);parent(&renderer,s,8,1,argv[1]);parent(&renderer,s,0,4,argv[1]);
    for(int t=1;t<4;++t)parent(&renderer,s,t,1,argv[1]);
    const EngineTrack* tracks=engine_get_tracks(s->engine);assert(tracks[0].parent_id==tracks[7].runtime_id && tracks[7].parent_id==tracks[4].runtime_id);
    frame(&renderer,s,argv[1],"manager-nested");
    int history=s->undo.undo_count;GroupPanelLayout l=daw_group_panel_layout(s);SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.x=l.buttons[0].x+5;e.button.y=l.buttons[0].y+5;
    input_manager_handle_event(&s->input_manager,s,&e);e.type=SDL_MOUSEBUTTONUP;e.button.x=l.buttons[11].x+5;e.button.y=l.buttons[11].y+5;input_manager_handle_event(&s->input_manager,s,&e);assert(s->undo.undo_count==history && s->group_panel.open);
    button(&renderer,s,11,argv[1]);assert(!s->group_panel.open);uint64_t group=tracks[4].runtime_id;
    timeline_reveal_track(s,4);frame(&renderer,s,argv[1],"expanded");daw_editor_controls_sync(s);
    SDL_Rect fold={0};for(unsigned i=0;i<s->editor_controls.surface.count;++i)if(s->editor_controls.surface.keys[i].domain==19 && s->editor_controls.surface.keys[i].value==group){KitRenderRect r=s->editor_controls.surface.controls[i].bounds;fold=(SDL_Rect){r.x,r.y,r.width,r.height};}
    assert(fold.w);click(s,fold);assert(engine_get_tracks(s->engine)[4].folded);frame(&renderer,s,argv[1],"folded");
    daw_editor_controls_sync(s);for(unsigned i=0;i<s->editor_controls.surface.count;++i)if(s->editor_controls.surface.keys[i].domain==15)
        assert(s->editor_controls.surface.keys[i].value!=tracks[0].runtime_id*2 && s->editor_controls.surface.keys[i].value!=tracks[1].runtime_id*2);
    assert(undo_manager_undo(&s->undo,s) && !engine_get_tracks(s->engine)[4].folded);assert(undo_manager_redo(&s->undo,s) && engine_get_tracks(s->engine)[4].folded);
    FxInstId id=undo_manager_add_effect(s,4,1);assert(id);effects_panel_sync_from_engine(s);frame(&renderer,s,argv[1],"group-effects");
    ui_layout_panes(s,1100,800);timeline_reveal_track(s,4);frame(&renderer,s,argv[1],"resized");
    ui_font_invalidate_cache((SDL_Renderer*)&renderer);ui_font_shutdown();tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);
    undo_manager_free(&s->undo);engine_destroy(s->engine);free(s);vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();
    puts("track_groups_native_test: passed (actual manager create/reparent, mismatched release refusal, nested headers, folded hit isolation, undo/redo, selected group effects and validation-clean resizing)");
}
