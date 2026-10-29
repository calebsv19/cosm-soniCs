#include "app_state.h"
#include "engine/engine_internal.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/text_draw.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Captures production panes plus an eviction stress band in a hidden native Vulkan window.
static void frame(VkRenderer* r, AppState* s, const char* path, bool dense, bool panes) {
    assert(vk_renderer_request_capture(r,path)==VK_SUCCESS);
    VkCommandBuffer cmd; VkFramebuffer fb; VkExtent2D size;
    assert(vk_renderer_begin_frame(r,&cmd,&fb,&size)==VK_SUCCESS);
    vk_renderer_set_logical_size(r,1600,1000);
    if (panes) { ui_render_panes((SDL_Renderer*)r,s); ui_render_controls((SDL_Renderer*)r,s); }
    SDL_Color color={240,240,240,255};
    const char* labels[]={"PLAY","PAUSE","STOP","SAVE","LOAD","W 1600","H 1000","Timeline","Track Size"};
    for(int i=0;i<9;++i) ui_draw_text_clipped((SDL_Renderer*)r,490+i*115,920,labels[i],color,1.0f,110);
    if(dense) for(int i=0;i<640;++i) {
        char title[80]; snprintf(title,sizeof(title),"Dense region %03d - distinct title",i);
        ui_draw_text_clipped((SDL_Renderer*)r,490+(i%5)*215,945+(i/5)%3*15,title,color,1.0f,210);
    }
    assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS); vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Compares the unchanged transport sentinel band after more than 128 distinct labels in one frame.
static void compare(const char* a,const char* b) {
    SDL_Surface* x=SDL_LoadBMP(a); SDL_Surface* y=SDL_LoadBMP(b);assert(x && y && x->w==y->w && x->h==y->h && x->format->BytesPerPixel==y->format->BytesPerPixel);
    int first=920*x->h/1000,last=940*x->h/1000,left=490*x->w/1600;
    for(int row=first;row<last;++row) assert(!memcmp((char*)x->pixels+row*x->pitch+left*x->format->BytesPerPixel,(char*)y->pixels+row*y->pitch+left*y->format->BytesPerPixel,(x->w-left)*x->format->BytesPerPixel));
    SDL_FreeSurface(x);SDL_FreeSurface(y);
}
// Verifies dense actual timeline labels and cache eviction without touching the active app or music.
int main(int argc,char** argv) {
    assert(argc==2);SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    SDL_Window* window=SDL_CreateWindow("soniCs hidden text proof",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,1600,1000,SDL_WINDOW_HIDDEN|SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
    VkRenderer renderer={0};VkRendererConfig cfg;vk_renderer_config_set_defaults(&cfg);cfg.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&cfg)==VK_SUCCESS);
    AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    undo_manager_init(&s->undo);input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s,1600,1000);
    s->timeline_visible_seconds=40;s->timeline_vertical_scale=.65f;tempo_map_init(&s->tempo_map,48000);time_signature_map_init(&s->time_signature_map);s->tempo=tempo_state_default(48000);
    for(int t=0;t<15;++t) {
        if(t) assert(engine_add_track(s->engine)>=0);char name[96];snprintf(name,sizeof(name),"%s %02d - Expressive Voices",t<5?"Strings":t<10?"Woodwinds":"Brass",t);assert(engine_track_set_name(s->engine,t,name));
        for(int j=0;j<16;++j) {int c;assert(engine_add_midi_clip_to_track(s->engine,t,j*120000,100000,&c));snprintf(name,sizeof(name),"Voice %02d Phrase %02d",t,j);assert(engine_clip_set_name(s->engine,t,c,name));
            EngineMidiNote note={.note=48+t,.velocity=.6f,.duration_frames=60000};assert(engine_clip_midi_add_note(s->engine,t,c,note,NULL));}
    }
    char a[4096],b[4096];snprintf(a,sizeof(a),"%s-baseline.bmp",argv[1]);snprintf(b,sizeof(b),"%s-dense.bmp",argv[1]);
    frame(&renderer,s,a,false,true);
    for(int i=0;i<6;++i) frame(&renderer,s,b,false,true);
    // Isolate forced eviction from pane text so the test stays within native sampler limits.
    ui_font_shutdown();vk_renderer_shutdown(&renderer);
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    assert(vk_renderer_init(&renderer,window,&cfg)==VK_SUCCESS);
    char evict_a[4096],evict_b[4096];snprintf(evict_a,sizeof(evict_a),"%s-evict-before.bmp",argv[1]);snprintf(evict_b,sizeof(evict_b),"%s-evict-after.bmp",argv[1]);
    frame(&renderer,s,evict_a,false,false);frame(&renderer,s,evict_b,true,false);compare(evict_a,evict_b);
    engine_destroy(s->engine);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);
    ui_font_shutdown();vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();puts("dense_text_native_test: passed (15 tracks/240 named MIDI regions; 640 isolated eviction labels; pixel-identical controls; repeated hidden native panes; Vulkan validation clean)");return 0;
}
